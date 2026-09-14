#!/usr/bin/env python3
"""An LFO must modulate LEVELS, never a mute the user authored.

THE DEFECT, as reported: "if muted these jiggle/kinda glitch when LFO
modulating morph". A run of bands carrying mute badges painted at visibly
different heights, moving with the LFO.

The badge and the height come from different sources, which is why the symptom
looks cosmetic and is not. In the shipping editor `drawBands` reads the mute
badge from `targetGainsRef` (the AUTHORED field) and the bar height from
`renderGainsRef`, which `applyModulationFrame` fills from the native frame's
`muted[]`/`gains[]`. So a frame that reports a muted band as un-muted paints a
badge over a moving bar.

That frame is `slot.field = audible` in Spectr::process -- the SAME BandField
handed to the DSP. `BandField::linear_gain()` gates on `Band::muted`, so a
dropped mute flag is not a paint bug at all: the band is HEARD. A muted band
measured at linear gain 0.84..1.19 instead of 0.

The cause is one line of ownership. `apply_modulation_to_target` seeds
`out = canonical`, then every destination reaches its field through
`morph_fields`, which writes BOTH gain and mute for all 64 slots -- mute picked
wholesale from whichever endpoint dominates at t. For the Morph destination the
two endpoints are the snapshots, so the canonical field's mute is not even an
input: a band muted after the captures comes back un-muted at every depth. For
the snapshot destinations canonical IS the a-side, so the mute survives only
while the unipolar amount stays below the t = 0.5 dominance flip -- i.e. only
at depth < 0.5.

And the dominance flip is itself a defect under a modulator. A user dragging
the morph slider controls that crossing; an LFO crosses it twice per cycle, so
any band whose mute differs between the endpoints strobes at LFO rate. Both
directions are therefore asserted.

This compiles a probe against the real headers and RUNS it -- the invariant is
arithmetic over floats, and a source-text check could not tell a working guard
from a guard applied to the wrong operand. Each plant installs a DIFFERENT
WRONG IMPLEMENTATION into a temp copy of the header, so a suite that cannot
reject them does not cover the defect.

Plants:
  no-preservation   the shipped defect -- the guard is simply gone
  morph-only        guard the Morph branch only; snapshot lanes stay broken
                    at depth >= 0.5 (the plausible partial fix)
  flag-only         re-flag the mute but let the MODULATED gain survive, so
                    unmuting mid-sweep reveals an LFO-phase-dependent level
  strobe            hold canonical mutes, but still let modulation MUTE a band
                    the user left un-muted (the LFO-rate pop)
  dead-modulation   satisfy the mute rule by making modulation do nothing --
                    must fail the positive control, not pass it

Exit codes: 0 the invariant holds, 1 it is violated, 2 NO VERDICT (no C++
compiler, missing source, probe would not build) -- never a pass.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEADER = os.path.join(REPO, "include", "spectr", "modulation.hpp")
SNAPSHOT_CPP = os.path.join(REPO, "src", "snapshot.cpp")

# The three guard calls do not share an indentation -- two sit inside a
# branch, the third at function scope -- and an anchor that assumed one
# depth silently matched only two of them, which made the headline plant
# reproduce only PART of the shipped defect while still exiting 1. The
# count is asserted below so that cannot recur quietly.
GUARD_RE = re.compile(r"^[ \t]*preserve_authored_mutes\(out, canonical\);[ \t]*\n",
                      re.MULTILINE)
GUARD_TEXT = "preserve_authored_mutes(out, canonical);"

PROBE = r"""
#include "spectr/modulation.hpp"
#include <cstdio>
using namespace spectr;

// Band 5: muted by the user AFTER both snapshots were captured un-muted.
//         Must stay silent and still at every depth, on every destination.
// Band 9: un-muted by the user, but MUTED in snapshot A.
//         Its mute must not toggle -- an LFO crossing t=0.5 twice per cycle
//         would otherwise strobe it.
// Band 3: plain un-muted band. POSITIVE CONTROL -- must still be modulated,
//         or the rule above is satisfied by a dead feature.
int main() {
    SnapshotBank snaps;
    BandField a, b; a.reset(); b.reset();
    for (std::size_t i = 0; i < kMaxBands; ++i) {
        a.bands[i].gain_db = -6.0f;
        b.bands[i].gain_db = +6.0f;
    }
    a.bands[9].muted = true;
    snaps.capture_into(SnapshotBank::Slot::A, a, Viewport{}, Layout::Bands32);
    snaps.capture_into(SnapshotBank::Slot::B, b, Viewport{}, Layout::Bands32);

    BandField canonical; canonical.reset();
    canonical.bands[5].gain_db = -3.0f;   // authored level, held under mute
    canonical.bands[5].muted = true;

    const char* names[] = {"WholeBank", "SnapshotA", "SnapshotB", "Morph"};
    ModulationTarget targets[] = {
        ModulationTarget::WholeBank, ModulationTarget::SnapshotA,
        ModulationTarget::SnapshotB, ModulationTarget::Morph};
    const float depths[] = {0.25f, 0.5f, 1.0f};

    for (int ti = 0; ti < 4; ++ti) {
        for (float depth : depths) {
            ModulationSettings s;
            s.enabled = true; s.depth = depth; s.target = targets[ti];
            bool unmuted5 = false, audible5 = false, gain5_moved = false;
            int toggles9 = 0; bool prev9 = false; bool first = true;
            float lo3 = 1e9f, hi3 = -1e9f;
            for (int step = 0; step < 256; ++step) {
                const float wave = lfo_value(LfoShape::Sine, step / 256.0);
                const BandField out =
                    apply_internal_modulation(canonical, snaps, 0.5f, s, wave);
                if (!out.bands[5].muted) unmuted5 = true;
                if (out.bands[5].linear_gain_unused()) {}
                if (out.linear_gain(5) != 0.0f) audible5 = true;
                if (out.bands[5].gain_db != canonical.bands[5].gain_db)
                    gain5_moved = true;
                const bool m9 = out.bands[9].muted;
                if (!first && m9 != prev9) ++toggles9;
                prev9 = m9; first = false;
                const float g3 = out.bands[3].gain_db;
                if (g3 < lo3) lo3 = g3;
                if (g3 > hi3) hi3 = g3;
            }
            printf("%s %.2f unmuted5=%d audible5=%d gain5moved=%d toggles9=%d swing3=%.4f\n",
                   names[ti], depth, unmuted5 ? 1 : 0, audible5 ? 1 : 0,
                   gain5_moved ? 1 : 0, toggles9, hi3 - lo3);
        }
    }
    return 0;
}
"""
# `linear_gain_unused` does not exist; the call above is removed before build.
PROBE = PROBE.replace("                if (out.bands[5].linear_gain_unused()) {}\n", "")


def no_verdict(message: str) -> int:
    print("NO VERDICT: %s" % message, file=sys.stderr)
    return 2


def guard_sites(text: str) -> list:
    return list(GUARD_RE.finditer(text))


def plant_header(text: str, plant: str) -> str:
    """Install a different WRONG implementation of the guard."""
    if plant in ("no-preservation", "morph-only"):
        sites = guard_sites(text)
        if len(sites) != 3:
            raise SystemExit(
                "plant %s: expected 3 guard calls (WholeBank, Morph, snapshot "
                "lanes), saw %d -- this control is dead, not the code clean"
                % (plant, len(sites)))
        # Source order: WholeBank, Morph, snapshot lanes. `morph-only` keeps
        # the middle one, which is the plausible partial fix -- patch the
        # destination the bug was reported against and leave the snapshot
        # lanes broken at depth >= 0.5.
        keep = 1 if plant == "morph-only" else -1
        out, cut = [], 0
        for index, site in enumerate(sites):
            if index == keep:
                continue
            out.append(text[cut:site.start()])
            cut = site.end()
        out.append(text[cut:])
        return "".join(out)
    if plant == "flag-only":
        return text.replace(
            "        if (canonical.bands[i].muted) out.bands[i] = canonical.bands[i];",
            "        if (canonical.bands[i].muted) out.bands[i].muted = true;")
    if plant == "strobe":
        return text.replace(
            "        else                          out.bands[i].muted = false;\n", "")
    if plant == "dead-modulation":
        return text.replace(
            "    BandField out = canonical;\n"
            "    const float wave = std::clamp(bipolar_lfo, -1.0f, 1.0f);",
            "    BandField out = canonical;\n    return out;\n"
            "    const float wave = std::clamp(bipolar_lfo, -1.0f, 1.0f);")
    raise SystemExit("unknown plant %r" % plant)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--plant", choices=("no-preservation", "morph-only",
                                        "flag-only", "strobe",
                                        "dead-modulation"))
    args = ap.parse_args()

    compiler = os.environ.get("CXX") or shutil.which("c++") or shutil.which("g++")
    if not compiler:
        return no_verdict("no C++ compiler on PATH, so the invariant is "
                          "UNMEASURED here")
    for path in (HEADER, SNAPSHOT_CPP):
        if not os.path.exists(path):
            return no_verdict("missing %s" % path)

    header_text = open(HEADER, encoding="utf-8").read()
    if GUARD_TEXT not in header_text and not args.plant:
        return no_verdict(
            "modulation.hpp carries no `preserve_authored_mutes(out, canonical)` "
            "call, so there is no guard to measure -- this detector is pointed "
            "at the wrong revision")

    with tempfile.TemporaryDirectory() as tmp:
        include_root = os.path.join(tmp, "include")
        shutil.copytree(os.path.join(REPO, "include"), include_root)
        if args.plant:
            planted = plant_header(header_text, args.plant)
            if planted == header_text:
                return no_verdict("plant %s changed nothing -- its anchor has "
                                  "moved, so this control is dead"
                                  % args.plant)
            with open(os.path.join(include_root, "spectr", "modulation.hpp"),
                      "w", encoding="utf-8") as fh:
                fh.write(planted)

        probe_cpp = os.path.join(tmp, "probe.cpp")
        with open(probe_cpp, "w", encoding="utf-8") as fh:
            fh.write(PROBE)
        binary = os.path.join(tmp, "probe")
        build = subprocess.run(
            [compiler, "-std=c++20", "-O1", "-I", include_root,
             probe_cpp, SNAPSHOT_CPP, "-o", binary],
            capture_output=True, text=True)
        if build.returncode != 0:
            return no_verdict("probe did not build:\n%s"
                              % build.stderr.strip()[:2000])
        run = subprocess.run([binary], capture_output=True, text=True)
        if run.returncode != 0:
            return no_verdict("probe exited %d" % run.returncode)

    failures: list[str] = []
    rows = 0
    for line in run.stdout.strip().splitlines():
        name, depth, unmuted5, audible5, gain5, toggles9, swing3 = line.split()
        rows += 1
        where = "%s at depth %s" % (name, depth)
        if unmuted5 != "unmuted5=0":
            failures.append("%s: a band muted in the authored field came back "
                            "UN-MUTED -- an LFO cleared the user's mute" % where)
        if audible5 != "audible5=0":
            failures.append("%s: that band is AUDIBLE (linear gain != 0). This "
                            "is the DSP, not the paint." % where)
        if gain5 != "gain5moved=0":
            failures.append("%s: the muted band's authored gain was overwritten "
                            "by the modulator, so unmuting mid-sweep would "
                            "reveal an LFO-phase-dependent level" % where)
        if toggles9 != "toggles9=0":
            failures.append("%s: mute TOGGLED %s across one LFO cycle -- a "
                            "discrete pop at LFO rate"
                            % (where, toggles9.split("=")[1]))
        swing = float(swing3.split("=")[1])
        if swing <= 0.01:
            failures.append("%s: POSITIVE CONTROL FAILED -- an un-muted band "
                            "was not modulated at all (swing %.4f dB). The mute "
                            "rule above is being satisfied by a dead feature."
                            % (where, swing))

    if rows != 12:
        return no_verdict("probe reported %d rows, expected 12" % rows)

    if failures:
        print("FAIL: an LFO changed a mute the user authored")
        for f in failures:
            print("  - %s" % f)
        print("\n%d measurement(s) violated the invariant across %d rows."
              % (len(failures), rows))
        return 1

    print("OK: %d destination x depth rows -- authored mute topology survives "
          "modulation, muted bands stay silent and still, and un-muted bands "
          "are still modulated." % rows)
    return 0


if __name__ == "__main__":
    sys.exit(main())
