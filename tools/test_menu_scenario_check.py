#!/usr/bin/env python3
"""Prove `menu_scenario_check.refuse_reason` refuses the runs it was written for.

A guard that has never been watched rejecting the real thing is a guard nobody
knows the shape of. Each case below is driven by RECORDED ARTIFACTS from an
actual standalone run, not by crafted dicts, because the defect this exists to
catch is one where every field is well formed and only their combination is
wrong.

The two fixtures under `fixtures/menu-scenario-runs/`:

  dead-2026-09-19/  The run that reported 42 failing assertions. Its menu never
                    opened: a JS exception fired after the first frame, 7 of 8
                    context presses came back `not-handled`, all 8 left the menu
                    unmounted, and every row step read `menu-absent`. None of
                    that was a product failure.

  healthy/          The same scenario on `origin/main` at 3e94f30. 91 steps, 35
                    context presses, every one of them mounting the menu. This
                    is the POSITIVE CONTROL, and it is the half that matters
                    most: a guard that refuses everything would pass the dead
                    case and is useless.

  bands64-2026-09-21/
                    The `--bands64` scenario against the current document. It
                    is NOT a green run -- everything from the `Modulation`
                    press onward fails, because a lifted submenu claiming the
                    native overlay dismisses the menu underneath it -- but its
                    ROOT snapshots are healthy, and they are what the
                    root-row case below reads. Recorded here because
                    `ROOT_SELECTION_ROWS` is a claim about rows the menu draws
                    at the root, and the only way to be wrong about that
                    silently is to have no recording of the root to check
                    against. That is exactly what happened: the set named
                    `Assign selection to Macro 1/4`, those rows moved behind a
                    `Macros \u203a` entry, and six reopen assertions went red
                    on rows that were one press further in.

Exit: 0 all cases hold, 1 one did not.
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from menu_scenario_check import (  # noqa: E402
    ROOT_SELECTION_ROWS, labels, refuse_reason, step)

RUNS = os.path.join(HERE, "fixtures", "menu-scenario-runs")


def load(name):
    base = os.path.join(RUNS, name)
    with open(os.path.join(base, "menu-scenario-log.txt")) as handle:
        text = handle.read()
    with open(os.path.join(base, "menu-scenario.json")) as handle:
        steps = json.load(handle)["steps"]
    return text, steps


def main():
    failures = []

    def check(what, ok, reading):
        print("%-58s %s  %s" % (what, "PASS" if ok else "FAIL", reading))
        if not ok:
            failures.append(what)

    dead_text, dead_steps = load("dead-2026-09-19")
    live_text, live_steps = load("healthy")

    # ── the positive control, first: a good run must be reportable ──
    #
    # Written first and deliberately: every case below asserts a REFUSAL, and a
    # refuse_reason that returned a string unconditionally would satisfy all of
    # them. This is the only case that can catch that.
    reason = refuse_reason(live_text, live_steps)
    check("a healthy run is reportable", reason is None,
          "%d steps, %d presses, reason=%r"
          % (len(live_steps),
             sum(1 for s in live_steps if s["kind"] == "rpress"), reason))

    # ...and it must be a run with something in it. A zero-step run is
    # trivially reportable, so the control above would pass on an empty file.
    check("the positive control is not vacuous",
          len(live_steps) > 50
          and sum(1 for s in live_steps if s["kind"] == "rpress") > 10,
          "%d steps, %d rpress"
          % (len(live_steps),
             sum(1 for s in live_steps if s["kind"] == "rpress")))

    # ── the dead run must be refused, and for the right reason ──
    reason = refuse_reason(dead_text, dead_steps)
    check("the 2026-09-19 dead run is refused", reason is not None,
          repr(reason)[:96])
    check("...naming the JS error, which is the first thing that went wrong",
          reason is not None and "JS error" in reason,
          repr(reason)[:96])

    # ── each guard in isolation, so a later edit cannot silently lose one ──

    # The JS-error guard is position-based, not text-based: the fatal line is
    # byte-identical to the benign one. Prove the boundary is what discriminates
    # by moving the SAME line to the other side of the first frame.
    fatal_line = "[pulp:info]  script-ui[error] {}"
    at = live_text.find("first frame")
    before_only = live_text[:at] + live_text[at:].replace(fatal_line, "")
    check("a healthy log with no post-first-frame error is accepted",
          refuse_reason(before_only, live_steps) is None,
          "benign errors before the frame: %d"
          % before_only[:at].count("script-ui[error]"))
    after_too = live_text[:at] + live_text[at:] + "\n" + fatal_line + "\n"
    reason = refuse_reason(after_too, live_steps)
    check("the identical line AFTER the first frame is refused",
          reason is not None and "JS error" in reason, repr(reason)[:96])

    # The unknown-step guard. `resize` is the verb this actually happened to:
    # the scenario emitted it and nothing implemented it.
    doctored = [dict(s) for s in live_steps]
    for s in doctored:
        if s["kind"] == "resize":
            s["result"] = "unknown-step"
    n_resize = sum(1 for s in doctored if s["result"] == "unknown-step")
    reason = refuse_reason(live_text, doctored)
    check("an unimplemented verb is refused",
          n_resize > 0 and reason is not None and "does not implement" in reason,
          "%d unknown-step; %s" % (n_resize, repr(reason)[:64]))

    # The mount guard, on its own: a run with no JS error and no unknown verb
    # whose presses simply never opened the menu.
    unmounted = [dict(s) for s in live_steps]
    for s in unmounted:
        if s["kind"] == "rpress":
            s["menu_mounted"] = False
    reason = refuse_reason(live_text, unmounted)
    check("context presses that open nothing are refused",
          reason is not None and "did not open the menu" in reason,
          repr(reason)[:96])

    # ── ROOT_SELECTION_ROWS names rows the ROOT panel actually draws ──
    #
    # Positive first, and on a recorded root snapshot rather than a crafted
    # one: the drift that this catches produced perfectly well-formed rows,
    # just not the ones the set asked for.
    _, b64_steps = load("bands64-2026-09-21")
    root = step(b64_steps, "o_re1") or {}
    root_labels = labels(root)
    missing = sorted(ROOT_SELECTION_ROWS - root_labels)
    check("every ROOT_SELECTION_ROWS row is drawn at the root",
          bool(root_labels) and not missing,
          "%d root rows recorded; missing %r" % (len(root_labels), missing))

    # And the negative: drop one required row from the recorded snapshot and
    # the subset test must go red. Without this the check above passes just as
    # well on an empty set, which would assert nothing at all.
    for dropped in sorted(ROOT_SELECTION_ROWS):
        thinned = {label for label in root_labels if label != dropped}
        if ROOT_SELECTION_ROWS <= thinned:
            check("a missing root row is caught (%s)" % dropped, False,
                  "the subset test still held without it")
            break
    else:
        check("a missing root row is caught",
              True, "each of the %d required rows fails the subset test when "
              "removed" % len(ROOT_SELECTION_ROWS))

    print()
    if failures:
        print("%d failure(s): %s" % (len(failures), "; ".join(failures)))
        return 1
    print("all %d cases hold" % 10)
    return 0


if __name__ == "__main__":
    sys.exit(main())
