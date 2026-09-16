#!/usr/bin/env python3
"""Coverage for the materialized-runtime merge driver.

A merge tool is exactly the kind of thing that passes while broken -- a driver
that simply took one side would exit 0 on every positive case. So every
positive here asserts the *specific document content each side contributed*,
and is paired with a control:

  * every mergeable fixture is first shown to CONFLICT under git's default
    content merge, so the fixtures are proven to be real conflicts rather than
    pairs git would have resolved anyway;
  * every merged result is asserted to differ from ours, from theirs and from
    base, so "took one side" and "emitted base" both fail;
  * the negative cases assert the driver REFUSES, and that its refusal leaves
    ordinary conflict markers behind.

Run: python3 tools/git/test_merge_materialized_runtime.py
Exit 0 all passed, 1 a failure, 2 the harness could not run.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
DRIVER = os.path.join(HERE, "merge_materialized_runtime.py")
ARTIFACT = os.path.join(
    REPO, "native-ui", "materialized", "materialized-document.runtime.json")
REL = "native-ui/materialized/materialized-document.runtime.json"

DETERMINISTIC_MINIMUM = 46

# (status, label) where status is "PASS", "FAIL" or "SKIP".
RESULTS: list[tuple[str, str]] = []


def check(label: str, ok: bool, detail: str = "") -> None:
    RESULTS.append(("PASS" if ok else "FAIL",
                    label if not detail else f"{label} -- {detail}"))


def skip(label: str, reason: str) -> None:
    """Record a check that could not run.

    Kept distinct from PASS on purpose: a suite that quietly counts an
    un-runnable check as passing is the exact failure this file exists to
    prevent elsewhere. Skips are printed, summarised separately, and never
    folded into the passed count -- but they do not fail the run either, so
    the deterministic core below is what actually gates. DETERMINISTIC_MINIMUM
    is the control that keeps that honest.
    """
    RESULTS.append(("SKIP", f"{label} -- {reason}"))


def run_driver(base: str, ours: str, theirs: str) -> int:
    proc = subprocess.run(
        [sys.executable, DRIVER, base, ours, theirs, "7", REL],
        capture_output=True, text=True)
    return proc.returncode


def default_git_conflicts(base: str, ours: str, theirs: str) -> bool:
    """True when git's ordinary content merge cannot resolve these three."""
    with tempfile.TemporaryDirectory() as tmp:
        cur = os.path.join(tmp, "cur")
        shutil.copyfile(ours, cur)
        proc = subprocess.run(["git", "merge-file", "-p", cur, base, theirs],
                              capture_output=True, text=True)
    return proc.returncode != 0


def default_git_merges_decoded_html(base: str, ours: str, theirs: str) -> bool:
    """True when git's ordinary merge of the DECODED html resolves without conflict.

    Deliberately not `default_git_conflicts`, which merges the artifact as it is
    stored -- one physical line, so it conflicts unconditionally and could never
    show anything. The hazards worth testing live in the decoded content, which
    is what this driver actually merges.
    """
    with tempfile.TemporaryDirectory() as tmp:
        paths = {}
        for name, src in (("base", base), ("ours", ours), ("theirs", theirs)):
            paths[name] = os.path.join(tmp, name)
            with open(paths[name], "w", encoding="utf-8") as handle:
                handle.write(load_html(src))
        proc = subprocess.run(
            ["git", "merge-file", "-p", paths["ours"], paths["base"],
             paths["theirs"]], capture_output=True, text=True)
    return proc.returncode == 0


def load_html(path: str) -> str:
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)["html"]


def write_variant(src: str, dst: str, edits: list[tuple[str, str]],
                  mutate: dict | None = None) -> None:
    """Copy `src` to `dst`, applying exact-substring edits to its html."""
    with open(src, encoding="utf-8") as handle:
        doc = json.load(handle)
    html = doc["html"]
    for old, new in edits:
        if html.count(old) != 1:
            raise SystemExit(
                f"fixture anchor is not unique ({html.count(old)}x): {old[:70]!r}")
        html = html.replace(old, new)
    doc["html"] = html
    if mutate:
        doc.update(mutate)
    with open(dst, "w", encoding="utf-8") as handle:
        json.dump(doc, handle, ensure_ascii=False)


# --------------------------------------------------------------------------
# 1. Round-trip control: re-emission must not move a single byte.
# --------------------------------------------------------------------------
def test_round_trip(tmp: str) -> None:
    sys.path.insert(0, HERE)
    import merge_materialized_runtime as drv

    with open(ARTIFACT, encoding="utf-8") as handle:
        raw = handle.read()
    start, end = drv.locate_html_token(raw)
    value, _ = json.decoder.scanstring(raw, start + 1)
    re_emitted = json.dumps(value, ensure_ascii=False)
    check("round-trip: html token re-encodes byte-identically",
          re_emitted == raw[start:end],
          f"{len(re_emitted)} vs {len(raw[start:end])} bytes")

    spliced = raw[:start] + re_emitted + raw[end:]
    check("round-trip: splicing html back yields a byte-identical file",
          spliced == raw)

    # And through the driver itself: a merge where theirs == base must leave
    # ours' bytes exactly as they were.
    base = os.path.join(tmp, "rt_base.json")
    ours = os.path.join(tmp, "rt_ours.json")
    theirs = os.path.join(tmp, "rt_theirs.json")
    shutil.copyfile(ARTIFACT, base)
    shutil.copyfile(ARTIFACT, theirs)
    write_variant(ARTIFACT, ours, [("</body></html>", "</body></html>")])
    before = open(ours, encoding="utf-8").read()
    rc = run_driver(base, ours, theirs)
    after = open(ours, encoding="utf-8").read()
    check("round-trip: no-op merge exits 0 and changes nothing",
          rc == 0 and before == after)


# --------------------------------------------------------------------------
# 2. Synthetic disjoint pair built from the live artifact (always runs).
# --------------------------------------------------------------------------
OURS_MARK = "/* spectr-merge-test: OURS sentinel */"
THEIRS_MARK = "/* spectr-merge-test: THEIRS sentinel */"


def test_disjoint_pair(tmp: str) -> None:
    html = load_html(ARTIFACT)
    lines = html.splitlines()
    # Two anchors far apart, each unique in the document.
    early = next(l for l in lines[:1200] if l.strip().startswith("function ")
                 and html.count(l) == 1)
    late = next(l for l in reversed(lines[-1500:])
                if l.strip().startswith("function ") and html.count(l) == 1)
    if early == late:
        raise SystemExit("could not find two distinct anchors")

    base = os.path.join(tmp, "d_base.json")
    ours = os.path.join(tmp, "d_ours.json")
    theirs = os.path.join(tmp, "d_theirs.json")
    shutil.copyfile(ARTIFACT, base)
    write_variant(ARTIFACT, ours, [(early, OURS_MARK + "\n" + early)])
    write_variant(ARTIFACT, theirs, [(late, THEIRS_MARK + "\n" + late)])

    check("disjoint pair: CONTROL -- default git merge conflicts on it",
          default_git_conflicts(base, ours, theirs))

    work = os.path.join(tmp, "d_work.json")
    shutil.copyfile(ours, work)
    rc = run_driver(base, work, theirs)
    check("disjoint pair: driver merges (exit 0)", rc == 0)
    if rc != 0:
        return
    merged = load_html(work)
    check("disjoint pair: OURS' edit survives", OURS_MARK in merged)
    check("disjoint pair: THEIRS' edit survives", THEIRS_MARK in merged)
    check("disjoint pair: result is neither side alone",
          merged != load_html(ours) and merged != load_html(theirs)
          and merged != load_html(base))
    check("disjoint pair: line count is exactly additive",
          len(merged.splitlines()) == len(load_html(base).splitlines()) + 2)
    check("disjoint pair: no conflict markers",
          "<<<<<<<" not in merged and ">>>>>>>" not in merged)


# --------------------------------------------------------------------------
# 2b. The driver's own output must be a fixed point, and order-symmetric.
# --------------------------------------------------------------------------
def test_output_is_stable(tmp: str) -> None:
    """A driver that churns the artifact on every merge makes things worse.

    Two properties, both about the merged document rather than the inputs:
    re-merging the driver's own output against itself must not move a byte,
    and merging the two sides in the opposite order must reach the same
    document. Without the second, two lanes landing in a different order would
    produce different artifacts from the same pair of changes.
    """
    html = load_html(ARTIFACT)
    lines = html.splitlines()
    early = next(l for l in lines[:1200] if l.strip().startswith("function ")
                 and html.count(l) == 1)
    late = next(l for l in reversed(lines[-1500:])
                if l.strip().startswith("function ") and html.count(l) == 1)

    base = os.path.join(tmp, "s_base.json")
    ours = os.path.join(tmp, "s_ours.json")
    theirs = os.path.join(tmp, "s_theirs.json")
    shutil.copyfile(ARTIFACT, base)
    write_variant(ARTIFACT, ours, [(early, OURS_MARK + "\n" + early)])
    write_variant(ARTIFACT, theirs, [(late, THEIRS_MARK + "\n" + late)])

    forward = os.path.join(tmp, "s_fwd.json")
    shutil.copyfile(ours, forward)
    if run_driver(base, forward, theirs) != 0:
        check("stability: forward merge succeeded", False)
        return
    reverse = os.path.join(tmp, "s_rev.json")
    shutil.copyfile(theirs, reverse)
    if run_driver(base, reverse, ours) != 0:
        check("stability: reverse merge succeeded", False)
        return

    check("stability: merging in either order reaches the same document",
          sorted(load_html(forward).splitlines())
          == sorted(load_html(reverse).splitlines()))

    # Fixed point: re-merging the driver's own output must not move a byte.
    before = open(forward, encoding="utf-8").read()
    fp_base = os.path.join(tmp, "s_fp_base.json")
    fp_theirs = os.path.join(tmp, "s_fp_theirs.json")
    shutil.copyfile(forward, fp_base)
    shutil.copyfile(forward, fp_theirs)
    rc = run_driver(fp_base, forward, fp_theirs)
    check("stability: re-merging the driver's output changes nothing",
          rc == 0 and open(forward, encoding="utf-8").read() == before)


# --------------------------------------------------------------------------
# 3. Negative: both sides edit the SAME line -- must refuse.
# --------------------------------------------------------------------------
def test_overlap_refused(tmp: str) -> None:
    html = load_html(ARTIFACT)
    anchor = next(l for l in html.splitlines()
                  if l.strip().startswith("function ") and html.count(l) == 1)
    base = os.path.join(tmp, "o_base.json")
    ours = os.path.join(tmp, "o_ours.json")
    theirs = os.path.join(tmp, "o_theirs.json")
    shutil.copyfile(ARTIFACT, base)
    write_variant(ARTIFACT, ours, [(anchor, OURS_MARK + "\n" + anchor)])
    write_variant(ARTIFACT, theirs, [(anchor, THEIRS_MARK + "\n" + anchor)])

    work = os.path.join(tmp, "o_work.json")
    shutil.copyfile(ours, work)
    rc = run_driver(base, work, theirs)
    check("overlap: driver REFUSES (exit 1)", rc == 1)
    body = open(work, encoding="utf-8").read()
    check("overlap: refusal leaves ordinary conflict markers",
          "<<<<<<<" in body and ">>>>>>>" in body)


# --------------------------------------------------------------------------
# 3b. Negative: two lanes inserting a child at the SAME positional path.
# --------------------------------------------------------------------------
def test_same_point_insertion_refused(tmp: str) -> None:
    """The case that must never silently compose.

    The document's bindings address DOM nodes by positional path, so two lanes
    each inserting a child at the same point in the same parent's child list is
    the edit that cannot be composed: both children would survive, and every
    binding indexing a later sibling would be off by a different amount than
    either lane assumed. A driver that merged this would produce a document
    that is plausible, loads, renders -- and is wrong in a way nothing
    downstream reports. That is strictly worse than a conflict.

    It refuses because two insertions at one point are, in the decoded source,
    two different edits to the same line -- which the three-way text merge
    reports as a conflict rather than guessing an order.
    """
    html = load_html(ARTIFACT)
    # A real element in the document's own child-list style, unique so the
    # fixture cannot accidentally patch two places.
    anchor_line = next(
        l for l in html.splitlines()
        if "React.createElement(" in l and html.count(l) == 1 and len(l) < 200)

    ours_child = ('    /* @__PURE__ */ React.createElement("span", '
                  '{ "data-spectr-test-ours": true }, "OURS"),')
    theirs_child = ('    /* @__PURE__ */ React.createElement("span", '
                    '{ "data-spectr-test-theirs": true }, "THEIRS"),')

    base = os.path.join(tmp, "p_base.json")
    ours = os.path.join(tmp, "p_ours.json")
    theirs = os.path.join(tmp, "p_theirs.json")
    shutil.copyfile(ARTIFACT, base)
    write_variant(ARTIFACT, ours, [(anchor_line, ours_child + "\n" + anchor_line)])
    write_variant(ARTIFACT, theirs, [(anchor_line, theirs_child + "\n" + anchor_line)])

    # Control: both fixtures really do each add one element, at the same point.
    bh, oh, th = load_html(base), load_html(ours), load_html(theirs)
    marker = "React.createElement("
    check("same-point insertion: CONTROL -- ours adds exactly one element",
          oh.count(marker) == bh.count(marker) + 1)
    check("same-point insertion: CONTROL -- theirs adds exactly one element",
          th.count(marker) == bh.count(marker) + 1)

    work = os.path.join(tmp, "p_work.json")
    shutil.copyfile(ours, work)
    rc = run_driver(base, work, theirs)
    check("same-point insertion: driver REFUSES to compose them", rc == 1)
    body = open(work, encoding="utf-8").read()
    check("same-point insertion: refusal leaves ordinary conflict markers",
          "<<<<<<<" in body and ">>>>>>>" in body)
    check("same-point insertion: it did NOT emit a document carrying both",
          not ("data-spectr-test-ours" in body
               and "data-spectr-test-theirs" in body
               and "<<<<<<<" not in body))


# --------------------------------------------------------------------------
# 3c. Negative: a counter both sides advance to the SAME value.
# --------------------------------------------------------------------------
def test_converging_counter_refused(tmp: str) -> None:
    """The hazard where "both sides agree" is exactly what makes it wrong.

    When `main` and a PR each add one item and each bump the same tally from N
    to N+1, a three-way merge sees both sides change one line to the *same*
    value, takes it once, and produces N+1. The correct answer is N+2. Nothing
    conflicts, both sides "agree", and the result is confidently wrong -- this
    is how a header census counter was corrupted on 2026-09-16.

    It is caught by the same line-count invariant that catches a dropped hunk,
    because arithmetically it IS one: both sides added the line, the merge has
    it once. The driver refuses rather than pick an interpretation, because
    "both incremented, so sum" and "both made the same edit, so take it once"
    are indistinguishable from the text.

    The fixture uses a constant that really is in the shipping document rather
    than an invented one, so this is a live instance and not a hypothetical.
    """
    html = load_html(ARTIFACT)
    counter = "const PATTERN_SCHEMA_VERSION = 1;"
    if html.count(counter) != 1:
        skip("converging counter", f"anchor {counter!r} is not unique in the "
             "document; the constant it used was renamed or removed")
        return

    lines = html.splitlines()
    counter_line = next(i for i, l in enumerate(lines) if l.strip() == counter)
    # Deliberately far from the counter AND from each other, so git's own merge
    # has no reason to conflict on the unrelated edits. The counter is then the
    # only line both sides touch, which is the whole point of the fixture.
    uniq = [(i, l) for i, l in enumerate(lines)
            if l.strip().startswith("function ") and html.count(l) == 1
            and abs(i - counter_line) > 200]
    if len(uniq) < 2:
        skip("converging counter", "too few unique anchors far enough from the "
             "counter to place the fixture's edits")
        return
    a_anchor, b_anchor = uniq[0][1], uniq[-1][1]
    bumped = "const PATTERN_SCHEMA_VERSION = 2;"

    base = os.path.join(tmp, "c_base.json")
    ours = os.path.join(tmp, "c_ours.json")
    theirs = os.path.join(tmp, "c_theirs.json")
    shutil.copyfile(ARTIFACT, base)
    write_variant(ARTIFACT, ours,
                  [(counter, bumped), (a_anchor, "// side A\n" + a_anchor)])
    write_variant(ARTIFACT, theirs,
                  [(counter, bumped), (b_anchor, "// side B\n" + b_anchor)])

    check("converging counter: CONTROL -- the two sides are not identical",
          load_html(ours) != load_html(theirs))
    # The control that gives this test its point: git's ordinary merge accepts
    # this silently. If it ever conflicts on its own, the fixture stopped
    # reproducing the hazard and this test would be proving nothing.
    check("converging counter: CONTROL -- git's own merge of the decoded html "
          "accepts it WITHOUT conflict (this is the trap)",
          default_git_merges_decoded_html(base, ours, theirs))

    work = os.path.join(tmp, "c_work.json")
    shutil.copyfile(ours, work)
    rc = run_driver(base, work, theirs)
    check("converging counter: driver REFUSES the silent convergence", rc == 1)
    body = open(work, encoding="utf-8").read()
    check("converging counter: refusal leaves ordinary conflict markers",
          "<<<<<<<" in body and ">>>>>>>" in body)


def test_top_level_counter_refused(tmp: str) -> None:
    """The same hazard one level up: a count in the JSON envelope.

    `runtime_canonicalization.jsx_scripts_compiled` is a real tally in this
    document. Two sides each advancing it is refused by the rule that `html`
    must be the only key either side changed -- asserted here so that rule is
    tied to this hazard and cannot be loosened without turning this red.
    """
    with open(ARTIFACT, encoding="utf-8") as handle:
        doc = json.load(handle)
    rc_block = doc.get("runtime_canonicalization")
    if not isinstance(rc_block, dict) or "jsx_scripts_compiled" not in rc_block:
        skip("top-level counter", "runtime_canonicalization.jsx_scripts_compiled "
             "is no longer present in the document")
        return

    bumped = dict(rc_block)
    bumped["jsx_scripts_compiled"] = rc_block["jsx_scripts_compiled"] + 1
    html = load_html(ARTIFACT)
    lines = html.splitlines()
    a = next(l for l in lines[:1200]
             if l.strip().startswith("function ") and html.count(l) == 1)
    b = next(l for l in reversed(lines[-1500:])
             if l.strip().startswith("function ") and html.count(l) == 1)

    base = os.path.join(tmp, "tc_base.json")
    ours = os.path.join(tmp, "tc_ours.json")
    theirs = os.path.join(tmp, "tc_theirs.json")
    shutil.copyfile(ARTIFACT, base)
    write_variant(ARTIFACT, ours, [(a, OURS_MARK + "\n" + a)],
                  mutate={"runtime_canonicalization": bumped})
    write_variant(ARTIFACT, theirs, [(b, THEIRS_MARK + "\n" + b)],
                  mutate={"runtime_canonicalization": bumped})

    work = os.path.join(tmp, "tc_work.json")
    shutil.copyfile(ours, work)
    rc = run_driver(base, work, theirs)
    check("top-level counter: driver REFUSES when both sides advance a tally "
          "in the JSON envelope", rc == 1)


# --------------------------------------------------------------------------
# 3d. Coverage control: prove the driver is actually WIRED to this path.
# --------------------------------------------------------------------------
def test_driver_is_actually_invoked(tmp: str) -> None:
    """A merge driver's coverage can silently be zero.

    Everything else here calls the driver directly, which proves the driver
    works but proves nothing about whether git would ever reach it. A sibling
    ledger in this repo was found not to be covered by .gitattributes at all,
    so its merge driver could never have fired. These checks close that gap:
    the path must actually resolve to this driver, the path must exist, and a
    real `git merge` must be shown to invoke it -- paired with a control that
    the same merge conflicts when the driver is not configured.
    """
    # 1. .gitattributes really covers the path git will see.
    proc = subprocess.run(["git", "-C", REPO, "check-attr", "merge", "--", REL],
                          capture_output=True, text=True)
    check("coverage: .gitattributes routes the artifact to this driver",
          proc.returncode == 0 and "merge: materialized-runtime" in proc.stdout,
          proc.stdout.strip()[:90])

    # 2. The path it names is not stale.
    check("coverage: the path named in .gitattributes exists on disk",
          os.path.exists(os.path.join(REPO, REL)))

    # 3. A real `git merge` invokes it -- with the negative control first.
    repo = os.path.join(tmp, "cov")
    os.makedirs(os.path.join(repo, os.path.dirname(REL)))
    os.makedirs(os.path.join(repo, "tools", "git"))
    run = lambda *a: subprocess.run(["git", "-C", repo, *a],
                                    capture_output=True, text=True)
    subprocess.run(["git", "init", "-q", repo], capture_output=True)
    run("config", "user.email", "t@t"), run("config", "user.name", "t")
    shutil.copyfile(os.path.join(REPO, ".gitattributes"),
                    os.path.join(repo, ".gitattributes"))
    shutil.copyfile(DRIVER, os.path.join(repo, "tools", "git",
                                         os.path.basename(DRIVER)))
    html = load_html(ARTIFACT)
    lines = html.splitlines()
    early = next(l for l in lines[:1200]
                 if l.strip().startswith("function ") and html.count(l) == 1)
    late = next(l for l in reversed(lines[-1500:])
                if l.strip().startswith("function ") and html.count(l) == 1)
    target = os.path.join(repo, REL)
    shutil.copyfile(ARTIFACT, target)
    run("add", "-f", ".gitattributes", REL,
        f"tools/git/{os.path.basename(DRIVER)}")
    run("commit", "-qm", "base")
    run("checkout", "-q", "-b", "theirs")
    write_variant(ARTIFACT, target, [(late, THEIRS_MARK + "\n" + late)])
    run("commit", "-qm", "t", REL)
    run("checkout", "-q", "-")
    run("checkout", "-q", "-b", "ours")
    write_variant(ARTIFACT, target, [(early, OURS_MARK + "\n" + early)])
    run("commit", "-qm", "o", REL)

    # CONTROL: unconfigured, this same merge must conflict.
    control = run("merge", "--no-edit", "theirs")
    check("coverage: CONTROL -- unconfigured, a real git merge conflicts",
          control.returncode != 0)
    run("merge", "--abort")

    run("config", "merge.materialized-runtime.name", "test")
    run("config", "merge.materialized-runtime.driver",
        f"{sys.executable} {os.path.join(repo, 'tools', 'git', os.path.basename(DRIVER))}"
        " %O %A %B %L %P")
    merged = run("merge", "--no-edit", "theirs")
    check("coverage: a real git merge INVOKES the driver and resolves",
          merged.returncode == 0)
    check("coverage: git actually ran THIS driver (its own report appears)",
          "merge-materialized-runtime" in (merged.stderr + merged.stdout))
    if merged.returncode == 0:
        final = load_html(target)
        check("coverage: the committed result carries BOTH sides",
              OURS_MARK in final and THEIRS_MARK in final)


# --------------------------------------------------------------------------
# 4. Negative: the positional-binding hazard -- must refuse.
# --------------------------------------------------------------------------
def test_binding_change_refused(tmp: str) -> None:
    html = load_html(ARTIFACT)
    lines = html.splitlines()
    early = next(l for l in lines[:1200] if l.strip().startswith("function ")
                 and html.count(l) == 1)
    late = next(l for l in reversed(lines[-1500:])
                if l.strip().startswith("function ") and html.count(l) == 1)

    with open(ARTIFACT, encoding="utf-8") as handle:
        doc = json.load(handle)
    bindings = json.loads(json.dumps(doc["text_bindings"]))
    # Simulate a side that inserted a DOM node and renumbered a binding path.
    bindings[0]["path"][-1]["index"] += 1

    base = os.path.join(tmp, "b_base.json")
    ours = os.path.join(tmp, "b_ours.json")
    theirs = os.path.join(tmp, "b_theirs.json")
    shutil.copyfile(ARTIFACT, base)
    write_variant(ARTIFACT, ours, [(early, OURS_MARK + "\n" + early)],
                  mutate={"text_bindings": bindings})
    write_variant(ARTIFACT, theirs, [(late, THEIRS_MARK + "\n" + late)])

    work = os.path.join(tmp, "b_work.json")
    shutil.copyfile(ours, work)
    rc = run_driver(base, work, theirs)
    check("positional bindings: driver REFUSES when a binding list also differs",
          rc == 1)
    body = open(work, encoding="utf-8").read()
    check("positional bindings: refusal leaves ordinary conflict markers",
          "<<<<<<<" in body and ">>>>>>>" in body)


# --------------------------------------------------------------------------
# 5. Negative: unparseable input -- must refuse, not traceback.
# --------------------------------------------------------------------------
def test_unparseable_refused(tmp: str) -> None:
    base = os.path.join(tmp, "u_base.json")
    ours = os.path.join(tmp, "u_ours.json")
    theirs = os.path.join(tmp, "u_theirs.json")
    shutil.copyfile(ARTIFACT, base)
    shutil.copyfile(ARTIFACT, ours)
    with open(theirs, "w", encoding="utf-8") as handle:
        handle.write('{"html": "unterminated')
    rc = run_driver(base, ours, theirs)
    check("unparseable theirs: driver REFUSES (exit 1)", rc == 1)


# --------------------------------------------------------------------------
# 6. The additive self-check must catch a dropped / duplicated hunk.
# --------------------------------------------------------------------------
def test_no_fabricated_content() -> None:
    """Nothing may appear in the output that was in none of the inputs.

    A stronger guarantee than "both sides' changes survive": it catches the
    merge inventing material rather than losing it. A three-way merge elsewhere
    in this organisation produced 204 placeholder entries in a file that had
    zero on both the PR head and the base, so this is a real failure mode.
    """
    sys.path.insert(0, HERE)
    import merge_materialized_runtime as drv

    base = "a\nb\nc\n"
    ours = "a\nOURS\nb\nc\n"
    theirs = "a\nb\nc\nTHEIRS\n"

    check("fabrication: accepts a merge built only from the inputs",
          drv.fabricated_content(base, ours, theirs,
                                 "a\nOURS\nb\nc\nTHEIRS\n") is None)
    check("fabrication: REJECTS a line present in no input",
          drv.fabricated_content(base, ours, theirs,
                                 "a\nOURS\nINVENTED\nb\nc\nTHEIRS\n") is not None)
    check("fabrication: REJECTS invented content even when nothing was lost",
          drv.fabricated_content(base, ours, theirs,
                                 "a\nOURS\nb\nc\nTHEIRS\nEXTRA\n") is not None)
    check("fabrication: a line from ANY single input is not fabrication",
          drv.fabricated_content(base, ours, theirs, "a\nOURS\nTHEIRS\n") is None)


def test_fabrication_refused_end_to_end(tmp: str) -> None:
    """The driver must refuse a fabricating merge, not just detect it.

    Exercised by pointing the driver at a merge engine that injects a line, so
    this proves the refusal path rather than the predicate in isolation.
    """
    sys.path.insert(0, HERE)
    import merge_materialized_runtime as drv

    html = load_html(ARTIFACT)
    lines = html.splitlines()
    early = next(l for l in lines[:1200]
                 if l.strip().startswith("function ") and html.count(l) == 1)
    late = next(l for l in reversed(lines[-1500:])
                if l.strip().startswith("function ") and html.count(l) == 1)

    base = os.path.join(tmp, "f_base.json")
    ours = os.path.join(tmp, "f_ours.json")
    theirs = os.path.join(tmp, "f_theirs.json")
    shutil.copyfile(ARTIFACT, base)
    write_variant(ARTIFACT, ours, [(early, OURS_MARK + "\n" + early)])
    write_variant(ARTIFACT, theirs, [(late, THEIRS_MARK + "\n" + late)])

    # Sanity: with the real merge engine this pair merges. If it did not, the
    # negative below would pass for the wrong reason.
    work = os.path.join(tmp, "f_ok.json")
    shutil.copyfile(ours, work)
    check("fabrication e2e: CONTROL -- this pair merges normally",
          run_driver(base, work, theirs) == 0)

    real = drv.three_way_text_merge
    try:
        drv.three_way_text_merge = (
            lambda b, o, t, m, p: (real(b, o, t, m, p) or "").replace(
                "\n", "\n// INVENTED-BY-A-BROKEN-MERGE-ENGINE\n", 1))
        work2 = os.path.join(tmp, "f_bad.json")
        shutil.copyfile(ours, work2)
        rc = drv.main(["driver", base, work2, theirs, "7", REL])
        check("fabrication e2e: driver REFUSES a merge that invented a line",
              rc == 1)
        body = open(work2, encoding="utf-8").read()
        check("fabrication e2e: the invented line is NOT in the written file",
              "INVENTED-BY-A-BROKEN-MERGE-ENGINE" not in body)
        check("fabrication e2e: refusal leaves ordinary conflict markers",
              "<<<<<<<" in body and ">>>>>>>" in body)
    finally:
        drv.three_way_text_merge = real


def test_self_check_catches_loss() -> None:
    sys.path.insert(0, HERE)
    import merge_materialized_runtime as drv

    base = "a\nb\nc\n"
    ours = "a\nOURS\nb\nc\n"
    theirs = "a\nb\nc\nTHEIRS\n"
    good = "a\nOURS\nb\nc\nTHEIRS\n"
    check("self-check: accepts a correctly composed merge",
          drv.composed_additively(base, ours, theirs, good) is None)
    check("self-check: REJECTS a merge that dropped theirs",
          drv.composed_additively(base, ours, theirs, "a\nOURS\nb\nc\n") is not None)
    check("self-check: REJECTS a merge that dropped ours",
          drv.composed_additively(base, ours, theirs, "a\nb\nc\nTHEIRS\n") is not None)
    check("self-check: REJECTS a merge that duplicated a hunk",
          drv.composed_additively(base, ours, theirs,
                                  "a\nOURS\nOURS\nb\nc\nTHEIRS\n") is not None)
    check("self-check: REJECTS a silently reverted region",
          drv.composed_additively(base, ours, theirs, base) is not None)


# --------------------------------------------------------------------------
# 7. Replay the real conflicting pairs from git history, when reachable.
# --------------------------------------------------------------------------
HISTORICAL = [
    # (label, base, ours, theirs, [(assert-label, needle, must_be_present)])
    ("#154 branch x #150",
     "e367c2a", "43a005a", "76a7284",
     [("#154 branch's Output-trim type declaration",
       'style: { width: 34, textAlign: "right", whiteSpace: "nowrap", '
       'flexShrink: 0, fontFamily: "var(--mono)"', True),
      ("#150's framework-owned menu dismissal",
       "Outside-press and Escape dismissal are the framework's", True)]),
    ("#159 x #160",
     "946cd2e", "0c21229499", "6743fd895d",
     [("#159 adds the OUTPUT trim label",
       "data-spectr-output-trim-label", True),
      ("#159 widens the trim track to 156",
       "width: 156, flexShrink: 0, accentColor", True),
      ("#159 removes the old 58pt track",
       "width: 58, flexShrink: 0, accentColor", False),
      ("#160 adds sameBandSet()", "function sameBandSet(a, b) {", True),
      ("#160 switches marquee to a ref",
       "const marqueeRef = useRef(null);", True),
      ("#160 drops marquee from the render deps (a DELETION)",
       "theme, hover, marquee, selection", False)]),
]


def test_historical(tmp: str) -> None:
    for label, b, o, t, asserts in HISTORICAL:
        paths = {}
        ok = True
        for name, rev in (("base", b), ("ours", o), ("theirs", t)):
            p = os.path.join(tmp, f"h_{label.replace(' ', '')}_{name}.json")
            proc = subprocess.run(["git", "-C", REPO, "show", f"{rev}:{REL}"],
                                  capture_output=True, text=True)
            if proc.returncode != 0:
                ok = False
                break
            with open(p, "w", encoding="utf-8") as handle:
                handle.write(proc.stdout)
            paths[name] = p
        if not ok:
            # CI checks out a shallow exact head, so these commits are usually
            # absent there. That is a limit of the checkout, not a defect, and
            # the deterministic fixtures above cover every behaviour this
            # replay exercises -- it exists for the extra credibility of real
            # edits, not to be the only evidence.
            skip(f"history {label}: replay",
                 "commit not present in this checkout (shallow clone); "
                 "runs where full history is available")
            continue

        check(f"history {label}: CONTROL -- default git merge conflicts",
              default_git_conflicts(paths["base"], paths["ours"], paths["theirs"]))

        work = os.path.join(tmp, f"w_{label.replace(' ', '')}.json")
        shutil.copyfile(paths["ours"], work)
        rc = run_driver(paths["base"], work, paths["theirs"])
        check(f"history {label}: driver merges (exit 0)", rc == 0)
        if rc != 0:
            continue
        merged = load_html(work)
        for alabel, needle, present in asserts:
            hit = needle in merged
            check(f"history {label}: {alabel}", hit is present)
        check(f"history {label}: result is neither side alone",
              merged != load_html(paths["ours"])
              and merged != load_html(paths["theirs"])
              and merged != load_html(paths["base"]))
        bh = load_html(paths["base"])
        oh = load_html(paths["ours"])
        th = load_html(paths["theirs"])
        check(f"history {label}: line count is exactly additive",
              len(merged.splitlines()) ==
              len(bh.splitlines())
              + (len(oh.splitlines()) - len(bh.splitlines()))
              + (len(th.splitlines()) - len(bh.splitlines())))
        check(f"history {label}: no conflict markers leaked",
              "<<<<<<<" not in merged and ">>>>>>>" not in merged)
        with open(work, encoding="utf-8") as handle:
            full = json.load(handle)
        with open(paths["base"], encoding="utf-8") as handle:
            basedoc = json.load(handle)
        check(f"history {label}: every non-html key untouched",
              {k: v for k, v in full.items() if k != "html"}
              == {k: v for k, v in basedoc.items() if k != "html"})


def main() -> int:
    if not os.path.exists(ARTIFACT):
        sys.stderr.write(f"artifact not found: {ARTIFACT}\n")
        return 2
    with tempfile.TemporaryDirectory() as tmp:
        test_round_trip(tmp)
        test_disjoint_pair(tmp)
        test_output_is_stable(tmp)
        test_overlap_refused(tmp)
        test_same_point_insertion_refused(tmp)
        test_converging_counter_refused(tmp)
        test_top_level_counter_refused(tmp)
        test_driver_is_actually_invoked(tmp)
        test_binding_change_refused(tmp)
        test_unparseable_refused(tmp)
        test_no_fabricated_content()
        test_fabrication_refused_end_to_end(tmp)
        test_self_check_catches_loss()
        test_historical(tmp)

    for status, label in RESULTS:
        print(f"  {status}  {label}")

    failed = [l for st, l in RESULTS if st == "FAIL"]
    skipped = [l for st, l in RESULTS if st == "SKIP"]
    passed = [l for st, l in RESULTS if st == "PASS"]

    print(f"\n{len(passed)} passed, {len(failed)} failed, {len(skipped)} skipped")
    if skipped:
        print("  skipped (NOT counted as passing):")
        for l in skipped:
            print(f"    - {l}")

    # The control. Every check above the history section is deterministic: it
    # builds its fixtures from the checked-in artifact and needs no git history,
    # no binary and no network. If fewer than that many ran, the harness broke
    # rather than the driver being fine -- and a suite reporting "0 failed"
    # because it executed nothing is precisely the reading this repo does not
    # accept. Raise this number when adding deterministic checks.
    if len(passed) + len(failed) < DETERMINISTIC_MINIMUM:
        print(f"\nHARNESS ERROR: only {len(passed) + len(failed)} checks ran; "
              f"at least {DETERMINISTIC_MINIMUM} are deterministic and must "
              "always run. Reporting failure rather than a green run that "
              "measured nothing.")
        return 1

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
