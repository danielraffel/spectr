#!/usr/bin/env python3
"""Prove the Spectr detector suite can still both PASS and FAIL.

Two of these detectors shipped DEAD -- `content_invariants.py` and
`control_invariants.py` both crashed on import of an `appearance_invariants`
function that was never written, from the day they landed. Nothing noticed,
because nothing ran them: the acceptance workflow invoked exactly one detector
out of a dozen, and the rest were manual-only. A detector nobody runs is
indistinguishable from a detector that passes.

So this runs the whole fixture-driven half of the suite on every acceptance
job, and for each one asserts BOTH directions against committed evidence:

  * a case that must come back clean (exit 0), and
  * a case that must come back red -- a known-bad fixture, or the detector's
    own `--plant`, which mutates a healthy input so the rule has to fire.

A detector that only ever passes is not coverage; a plant that stops firing
means the rule rotted. Either is reported here rather than in six months.

Every input is a committed fixture under docs/evidence/, so this needs no
build, no app, no GPU and no network. It is seconds, not minutes.

Exit codes: 0 all cases as expected, 1 at least one detector disagrees,
2 the harness itself could not run (missing fixture, missing Pillow).
"""

from __future__ import annotations

import argparse
import importlib.util
import os
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
E07 = os.path.join("docs", "evidence", "2026-09-07")
E11 = os.path.join("docs", "evidence", "2026-09-11")
E12 = os.path.join("docs", "evidence", "2026-09-12")

D = os.path.join("tools", "spectr-detectors")
T = "tools"


def f(*parts: str) -> str:
    return os.path.join(*parts)


CURSOR_EXPECT = [
    "--expect", "CUR-1-canvas=crosshair",
    "--expect", "CUR-2-viewport=grab",
    "--expect", "CUR-4-trim-left=horizontal-resize",
    "--expect", "CUR-4-trim-right=horizontal-resize",
    "--expect", "CUR-3-viewport-drag=grabbing",
    "--expect", "CONTROL-same-view-off-plot=default",
    "--expect", "CONTROL-toolbar-button=pointer",
]

# (detector, case label, expected exit, argv after `python3`)
# Detectors that need a third-party module, and the module they import. Kept
# beside the cases so adding a detector with a dependency cannot forget it.
DETECTOR_REQUIREMENTS: dict[str, str] = {
    "text_contrast": "PIL",
}

# A case whose entry point is a .mjs runs under node, not under this
# interpreter. Node is how the materialized-document suites execute the
# shipping editor's own script blocks, which is the only way to measure a
# behaviour that has no layout node to look at -- a band's painted value, or
# whether a command reached the bridge at all.
NODE = os.environ.get("SPECTR_NODE_EXECUTABLE") or shutil.which("node")


def interpreter(argv: list[str]) -> list[str] | None:
    """Command prefix for a case, or None when its interpreter is absent."""
    if argv and argv[0].endswith(".mjs"):
        return [NODE] if NODE else None
    return [sys.executable]

CASES: list[tuple[str, str, int, list[str]]] = [
    # --- the two that shipped dead -------------------------------------
    ("control_invariants", "healthy slider surface", 0,
     [f(T, "control_invariants.py"), f(E07, "SLIDER-GREEN-thumb.layout.json")]),
    ("control_invariants", "known-bad fixture (4 thumbless tracks)", 1,
     [f(T, "control_invariants.py"), f(E07, "SLIDER-no-thumb.layout.json")]),
    ("control_invariants", "plant: flatten a healthy track", 1,
     [f(T, "control_invariants.py"), f(E07, "SLIDER-GREEN-thumb.layout.json"),
      "--plant", "flatten-track"]),

    ("content_invariants", "required strings are paintable", 0,
     [f(T, "content_invariants.py"), f(E07, "SLIDER-GREEN-thumb.layout.json"),
      "--present", "APPEARANCE", "--present", "Bands",
      "--absent", "NOT-A-REAL-STRING"]),
    ("content_invariants", "plant: drop a required string", 1,
     [f(T, "content_invariants.py"), f(E07, "SLIDER-GREEN-thumb.layout.json"),
      "--present", "APPEARANCE", "--plant", "drop-present"]),
    ("content_invariants", "plant: surface a string that must stay hidden", 1,
     [f(T, "content_invariants.py"), f(E07, "SLIDER-GREEN-thumb.layout.json"),
      "--absent", "NOT-A-REAL-STRING", "--plant", "add-absent"]),

    # --- the user's named issues ---------------------------------------
    # issue 4: Settings copy button width
    ("copy_button_width_invariance", "width is state-invariant", 0,
     [f(D, "copy_button_width_invariance.py"), f(E11, "COPY-WIDTH-GREEN.layout.json")]),
    ("copy_button_width_invariance", "known-bad fixture (stretched to panel)", 1,
     [f(D, "copy_button_width_invariance.py"), f(E11, "COPY-WIDTH-RED.layout.json")]),
    ("copy_button_width_invariance", "plant", 1,
     [f(D, "copy_button_width_invariance.py"), f(E11, "COPY-WIDTH-GREEN.layout.json"),
      "--plant"]),

    # the hover readout pill after CLEAR: the SAME width-not-centring shape
    # as the copy button above, in the status overlay. The pill's width came
    # from React's copy of the message while the per-frame hover writer put a
    # different string in the box, so a 13-character message (CLEARED GAINS /
    # BAND n MUTED, both 132px) left the next hover reading painting 173px of
    # ink into a 102px content box.
    ("status_pill_width_invariance", "pill contains its ink and stays centred", 0,
     [f(D, "status_pill_width_invariance.py"),
      f(E11, "STATUS-PILL-GREEN-hover.layout.json"),
      f(E11, "STATUS-PILL-GREEN-after-short-message.layout.json")]),
    ("status_pill_width_invariance", "known-bad fixture (ink overhangs the pill)", 1,
     [f(D, "status_pill_width_invariance.py"),
      f(E11, "STATUS-PILL-RED-after-short-message.layout.json")]),
    ("status_pill_width_invariance",
     "known-bad fixture (two widths for one text length)", 1,
     [f(D, "status_pill_width_invariance.py"),
      f(E11, "STATUS-PILL-RED-length-collision-a.layout.json"),
      f(E11, "STATUS-PILL-RED-length-collision-b.layout.json")]),
    ("status_pill_width_invariance", "plant: shrink to a short message's width", 1,
     [f(D, "status_pill_width_invariance.py"),
      f(E11, "STATUS-PILL-GREEN-after-short-message.layout.json"),
      "--plant", "shrink"]),
    ("status_pill_width_invariance", "plant: push the pill off centre", 1,
     [f(D, "status_pill_width_invariance.py"),
      f(E11, "STATUS-PILL-GREEN-after-short-message.layout.json"),
      "--plant", "offset"]),

    # the pill does not MOVE when its content changes (the CLEAR transition
    # the width detector's population could never reach)
    ("status_pill_position_invariance", "pill keeps its place across CLEAR", 0,
     [f(D, "status_pill_position_invariance.py"),
      f(E12, "STATUS-PILL-POS-GREEN-baseline.layout.json"),
      f(E12, "STATUS-PILL-POS-GREEN-after-clear.layout.json")]),
    ("status_pill_position_invariance",
     "known-bad fixture (pill jumps (-120,-44) after CLEAR)", 1,
     [f(D, "status_pill_position_invariance.py"),
      f(E12, "STATUS-PILL-POS-GREEN-baseline.layout.json"),
      f(E12, "STATUS-PILL-POS-RED-after-clear.layout.json")]),
    ("status_pill_position_invariance", "plant: shift the pill off centre", 1,
     [f(D, "status_pill_position_invariance.py"),
      f(E12, "STATUS-PILL-POS-GREEN-baseline.layout.json"),
      f(E12, "STATUS-PILL-POS-GREEN-after-clear.layout.json"),
      "--plant", "shift"]),
    ("status_pill_position_invariance", "plant: raise the pill", 1,
     [f(D, "status_pill_position_invariance.py"),
      f(E12, "STATUS-PILL-POS-GREEN-baseline.layout.json"),
      f(E12, "STATUS-PILL-POS-GREEN-after-clear.layout.json"),
      "--plant", "raise"]),

    # issue 3: settings slider thumb grows on hover
    ("slider_thumb_hover_growth", "thumbs grow under hover", 0,
     [f(D, "slider_thumb_hover_growth.py"),
      "--idle", f(E11, "slider-hover-GREEN-idle.layout.json"),
      "--hover", f(E11, "slider-hover-GREEN-hover.layout.json")]),
    ("slider_thumb_hover_growth", "known-bad fixture (no growth)", 1,
     [f(D, "slider_thumb_hover_growth.py"),
      "--idle", f(E11, "slider-hover-RED-idle.layout.json"),
      "--hover", f(E11, "slider-hover-RED-hover.layout.json")]),
    ("slider_thumb_hover_growth", "plant", 1,
     [f(D, "slider_thumb_hover_growth.py"),
      "--idle", f(E11, "slider-hover-GREEN-idle.layout.json"),
      "--hover", f(E11, "slider-hover-GREEN-hover.layout.json"), "--plant"]),
    # The 2026-09-11 pair above is a CIRCLE-era capture. The thumb is a pill
    # now, and a growth check that had only ever been run against a square
    # thumb would not have proved it still resolves one -- its finder used to
    # require `w == h` and would have found nothing at all. This pair is the
    # same probe re-run on the pill.
    ("slider_thumb_hover_growth", "pill thumb grows under hover", 0,
     [f(D, "slider_thumb_hover_growth.py"),
      "--idle", f(E12, "slider-hover-PILL-idle.layout.json"),
      "--hover", f(E12, "slider-hover-PILL-hover.layout.json")]),
    ("slider_thumb_hover_growth", "plant: pill idle against itself", 1,
     [f(D, "slider_thumb_hover_growth.py"),
      "--idle", f(E12, "slider-hover-PILL-idle.layout.json"),
      "--hover", f(E12, "slider-hover-PILL-hover.layout.json"), "--plant"]),

    # The SHAPE of that thumb, read off the shipping artifact rather than a
    # capture -- every state Spectr-native-shot captures has the morph slider
    # disabled, where its thumb is `opacity: 0`, so no capture can adjudicate
    # the morph half at all.
    ("slider_thumb_pill_shape", "shipping artifact draws pills inside "
     "their tracks", 0,
     [f(D, "slider_thumb_pill_shape.py")]),
    ("slider_thumb_pill_shape", "plant: put the circle back", 1,
     [f(D, "slider_thumb_pill_shape.py"), "--plant", "circle"]),
    ("slider_thumb_pill_shape", "plant: put the overhanging travel back", 1,
     [f(D, "slider_thumb_pill_shape.py"), "--plant", "overhang"]),

    # WHERE that pill is allowed to travel. `slider_thumb_pill_shape` proves
    # the thumb stays inside its track; this proves the TRACK stays off the
    # flanking "A"/"B" labels, which for most of the control's life it did not
    # -- the labels were bare spans with no layout box, so the track began at
    # the "A" glyph's own x and the thumb covered it at value 0. The RED
    # fixture is a real capture of exactly that.
    #
    # `box_intersection` could never have caught it: it compares only nodes
    # carrying TEXT, and the thumb is a text-free div. The one node it could
    # see -- the disabled caption's box, starting on the label -- it DID
    # report, and that finding was waved through as pre-existing because it
    # was identical before and after an unrelated change.
    ("morph_row_clearance", "track clears both flanking labels", 0,
     [f(D, "morph_row_clearance.py"),
      f(E12, "MORPH-ROW-GREEN-track-clear.layout.json")]),
    ("morph_row_clearance", "known-bad fixture (track laid out on the A "
     "label)", 1,
     [f(D, "morph_row_clearance.py"),
      f(E12, "MORPH-ROW-RED-track-on-a-label.layout.json")]),
    ("morph_row_clearance", "plant: put the track back on the A label", 1,
     [f(D, "morph_row_clearance.py"),
      f(E12, "MORPH-ROW-GREEN-track-clear.layout.json"), "--plant", "overlap"]),
    ("morph_row_clearance", "plant: crush the track the way the row once did",
     1,
     [f(D, "morph_row_clearance.py"),
      f(E12, "MORPH-ROW-GREEN-track-clear.layout.json"), "--plant", "crush"]),
    # issue 2 / the preset dropdown: every row caption starts on ONE column.
    # This detector shipped with a docstring that named the leading-edge rule
    # and an implementation that only compared line-box HEIGHT, so it was green
    # through the whole life of the defect below -- SAVE CURRENT... and
    # MANAGE... painting 9px left of the eight factory rows, which a user
    # reported by eye. The RED fixture is the real capture of that shipped
    # state, not a synthetic one.
    ("menu_item_caption_uniformity", "every caption on one column", 0,
     [f(D, "menu_item_caption_uniformity.py"),
      f(E12, "PRESET-CAPTION-GREEN-column.layout.json")]),
    ("menu_item_caption_uniformity",
     "known-bad fixture (SAVE CURRENT / MANAGE 9px flush left)", 1,
     [f(D, "menu_item_caption_uniformity.py"),
      f(E12, "PRESET-CAPTION-RED-flush-left.layout.json")]),
    ("menu_item_caption_uniformity",
     "plant: captions fall back to the row's own left edge", 1,
     [f(D, "menu_item_caption_uniformity.py"),
      f(E12, "PRESET-CAPTION-GREEN-column.layout.json"),
      "--plant", "left-fallback"]),
    ("menu_item_caption_uniformity", "plant: one caption's line box grows", 1,
     [f(D, "menu_item_caption_uniformity.py"),
      f(E12, "PRESET-CAPTION-GREEN-column.layout.json"),
      "--plant", "tall-caption"]),

    # the hit-target lane: a control must be reachable over the area it paints
    ("hit_target_reach", "settings toggles + sliders reach their paint", 0,
     [f(D, "hit_target_reach.py"),
      f(E12, "GREEN-hit-settings-modulation.layout.json")]),
    ("hit_target_reach", "known-bad fixture (settings, pre-fix)", 1,
     [f(D, "hit_target_reach.py"),
      f(E12, "RED-hit-settings-modulation.layout.json")]),
    ("hit_target_reach", "plant: hit rects shrunk onto the paint", 1,
     [f(D, "hit_target_reach.py"),
      f(E12, "GREEN-hit-settings-modulation.layout.json"), "--plant"]),
    ("hit_target_reach", "SNAPSHOT A/B reach their paint", 0,
     [f(D, "hit_target_reach.py"), f(E12, "GREEN-hit-transport.layout.json")]),
    ("hit_target_reach", "known-bad fixture (transport row, pre-fix)", 1,
     [f(D, "hit_target_reach.py"), f(E12, "RED-hit-transport.layout.json")]),

    # issue 6: cursors really change on hover
    ("cursor_invariants", "every region resolves its cursor", 0,
     [f(T, "cursor_invariants.py"), f(E07, "CUR-GREEN.cursor.json")] + CURSOR_EXPECT),
    ("cursor_invariants", "known-bad fixture (crosshair everywhere)", 1,
     [f(T, "cursor_invariants.py"), f(E07, "CUR-RED.cursor.json")] + CURSOR_EXPECT),
    ("cursor_invariants", "plant: wrong cursor", 1,
     [f(T, "cursor_invariants.py"), f(E07, "CUR-GREEN.cursor.json")]
     + CURSOR_EXPECT + ["--plant", "wrong-cursor"]),

    # issue 1/3: a drag pointer sample must not enqueue a React commit.
    # This one reads the checked-in materialized artifact, not a fixture.
    ("drag_hover_no_react_commit", "shipping artifact guards the setter", 0,
     [f(D, "drag_hover_no_react_commit.py")]),
    ("drag_hover_no_react_commit", "plant: strip the drag guard", 1,
     [f(D, "drag_hover_no_react_commit.py"), "--plant", "unguard"]),

    # The Preset Manager's footer advertises DOUBLE-CLICK and Return. Both were
    # dead: the row's `onDoubleClick` registers under the bridge event name
    # `doubleclick`, which nothing in the runtime dispatches, and the keydown
    # handler had no Enter branch at all. Nothing else in the suite can see
    # that -- the footer renders identically whether its handlers fire or not.
    # Reads the checked-in artifact, so it needs no build.
    # The manager's own transient state, its row chrome, and the heading's
    # measured box. Reads the checked-in artifact, so it needs no build, no
    # GPU and no third-party module -- it registers on every runner
    # configuration, including the chrome-less acceptance one.
    ("preset_manager_reset_and_chrome",
     "shipping artifact resets on open, drops the chip, boxes the name", 0,
     [f(D, "preset_manager_reset_and_chrome.py")]),
    # main's behaviour restored exactly: no reset at all. A suite that cannot
    # reject this does not cover what the user reported.
    ("preset_manager_reset_and_chrome", "plant: no reset on open at all", 1,
     [f(D, "preset_manager_reset_and_chrome.py"), "--plant", "no-reset"]),
    # The OTHER wrong implementation, and the one that reads as correct in a
    # diff: keyed on a dep that changes every App render, so it clears the
    # search field while the user is still typing in it.
    ("preset_manager_reset_and_chrome",
     "plant: reset keyed on a per-render identity", 1,
     [f(D, "preset_manager_reset_and_chrome.py"), "--plant", "unstable-deps"]),
    ("preset_manager_reset_and_chrome", "plant: put the F / U chip back", 1,
     [f(D, "preset_manager_reset_and_chrome.py"), "--plant", "chip"]),
    ("preset_manager_reset_and_chrome",
     "plant: heading title back to a span", 1,
     [f(D, "preset_manager_reset_and_chrome.py"), "--plant", "title-span"]),
    ("preset_manager_reset_and_chrome",
     "plant: second child beside the heading name", 1,
     [f(D, "preset_manager_reset_and_chrome.py"), "--plant",
      "title-two-children"]),
    ("preset_manager_reset_and_chrome",
     "plant: let the badge shrink instead of the name", 1,
     [f(D, "preset_manager_reset_and_chrome.py"), "--plant", "badge-shrinks"]),

    ("preset_commit_gestures", "shipping artifact backs every advertised gesture", 0,
     [f(D, "preset_commit_gestures.py")]),
    ("preset_commit_gestures", "plant: re-attach the undispatchable dbl-click prop", 1,
     [f(D, "preset_commit_gestures.py"), "--plant", "dead-prop"]),
    ("preset_commit_gestures", "plant: delete the Enter branch the footer advertises", 1,
     [f(D, "preset_commit_gestures.py"), "--plant", "no-enter"]),
    ("preset_commit_gestures", "plant: leave the user list select-only", 1,
     [f(D, "preset_commit_gestures.py"), "--plant", "one-list"]),

    # --- layout truth ---------------------------------------------------
    ("appearance_invariants", "clean shipping settings dump", 0,
     [f(T, "appearance_invariants.py"), f(E07, "SET-2-shipping-scrolled.layout.json")]),
    ("appearance_invariants", "plant: overlap two painted runs", 1,
     [f(T, "appearance_invariants.py"), f(E07, "SET-2-shipping-scrolled.layout.json"),
      "--plant-overlap"]),
    ("appearance_invariants", "plant: overflow a run past its box", 1,
     [f(T, "appearance_invariants.py"), f(E07, "SET-2-shipping-scrolled.layout.json"),
      "--plant-overflow"]),

    ("centering_invariant", "owner/text pairs stay centred", 0,
     [f(T, "centering_invariant.py"), f(E07, "SLIDER-GREEN-thumb.layout.json")]),
    ("centering_invariant", "plant: shift a run off centre", 1,
     [f(T, "centering_invariant.py"), f(E07, "SLIDER-GREEN-thumb.layout.json"),
      "--plant"]),

    ("box_intersection", "no two painted runs collide", 0,
     [f(D, "box_intersection.py"), f(E07, "SLIDER-GREEN-thumb.layout.json")]),
    ("box_intersection", "built-in positive control fires", 0,
     [f(D, "box_intersection.py"), f(E07, "SLIDER-GREEN-thumb.layout.json"),
      "--self-test", os.path.join(os.environ.get("TMPDIR", "/tmp"), "spectr-selftest-bi")]),

    ("painted_vs_measured_width", "built-in positive control fires", 0,
     [f(D, "painted_vs_measured_width.py"), f(E07, "SLIDER-GREEN-thumb.layout.json"),
      "--self-test", os.path.join(os.environ.get("TMPDIR", "/tmp"), "spectr-selftest-pvm")]),

    ("reachability_census", "every adjudicable control is on screen", 0,
     [f(T, "reachability_census.py"), f(E07, "SET-837-settings-unscrolled")]),
    ("reachability_census", "plant: push one control off screen", 1,
     [f(T, "reachability_census.py"), f(E07, "SET-837-settings-unscrolled"),
      "--plant", "offscreen"]),
    ("reachability_census", "plant: collapse one control to zero", 1,
     [f(T, "reachability_census.py"), f(E07, "SET-837-settings-unscrolled"),
      "--plant", "zero"]),

    # COLLAPSE had no successor after the appearance rewrite dropped it, and a
    # collapsed Settings body is the SET-1 defect itself. The GREEN/RED pair is
    # the same surface captured with and without the defect, so a pass here is
    # a DISCRIMINATION, not one fixture happening to be red.
    ("collapsed_box", "clean shipping settings dump", 0,
     [f(D, "collapsed_box.py"), f(E07, "SET-2-shipping-scrolled.layout.json")]),
    ("collapsed_box", "the healthy half of the SET-1 pair", 0,
     [f(D, "collapsed_box.py"), f(E07, "SET-1-GREEN.layout.json")]),
    ("collapsed_box", "known-bad fixture (SET-1 collapsed settings body)", 1,
     [f(D, "collapsed_box.py"), f(E07, "SET-1-RED.layout.json"), "--quiet"]),
    ("collapsed_box", "plant: collapse a container holding painting strings", 1,
     [f(D, "collapsed_box.py"), f(E07, "SET-2-shipping-scrolled.layout.json"),
      "--plant", "container", "--quiet"]),
    ("collapsed_box", "plant: collapse one label's own box and its clip", 1,
     [f(D, "collapsed_box.py"), f(E07, "SET-2-shipping-scrolled.layout.json"),
      "--plant", "self", "--quiet"]),
    # A snapshot with no depth sidecar cannot be adjudicated at all. The
    # detector must say so (exit 2, "Not Run" in CTest terms) rather than
    # return the clean 0 that an ancestry guess would produce.
    ("collapsed_box", "no depth sidecar is a no-verdict, never a pass", 2,
     [f(D, "collapsed_box.py"), f(E07, "SET-6-shipping-midscroll.layout.json")]),

    # Scoped to the settings panel on purpose: text behind the settings scrim
    # is dimmed BY DESIGN, and measuring it reddens this for the wrong reason.
    ("text_contrast", "settings panel clears its contrast floor", 0,
     [f(T, "text_contrast.py"), f(E07, "SET-837-settings-unscrolled.layout.json"),
      f(E07, "SET-837-settings-unscrolled.png"), "--scale", "1.5",
      "--within", "400,90.5,520,679"]),
    ("text_contrast", "plant: dim the highest-contrast label", 1,
     [f(T, "text_contrast.py"), f(E07, "SET-837-settings-unscrolled.layout.json"),
      f(E07, "SET-837-settings-unscrolled.png"), "--scale", "1.5",
      "--within", "400,90.5,520,679", "--plant"]),

    # A command that reports success must reach the FIELD, and a muted band
    # must paint on its sentinel. Both were reported from the shipped AU and
    # neither has a layout node: CLEAR's status pill said CLEARED GAINS while
    # every band kept its shape, so a detector reading the overlay would have
    # called it green. These cases run the shipping document's own bank block
    # and assert on the payload that crosses the bridge and on the painted
    # value, never on the banner.
    ("materialized_clear_and_mute_overlay",
     "CLEAR reaches the field and a muted band sits on its sentinel", 0,
     [f("test", "test_materialized_clear_and_mute_overlay.mjs"),
      f("native-ui", "materialized", "materialized-document.runtime.json")]),
    ("materialized_clear_and_mute_overlay",
     "plant: the projection re-arms the one-shot echo suppressor", 1,
     [f("test", "test_materialized_clear_and_mute_overlay.mjs"),
      f("native-ui", "materialized", "materialized-document.runtime.json"),
      "--plant-latched-suppressor"]),
    ("materialized_clear_and_mute_overlay",
     "plant: projections flatten a muted band off its sentinel", 1,
     [f("test", "test_materialized_clear_and_mute_overlay.mjs"),
      f("native-ui", "materialized", "materialized-document.runtime.json"),
      "--plant-flatten-muted"]),
    ("materialized_clear_and_mute_overlay",
     "plant: clearGains stops declaring its edit", 1,
     [f("test", "test_materialized_clear_and_mute_overlay.mjs"),
      f("native-ui", "materialized", "materialized-document.runtime.json"),
      "--plant-undeclared-clear"]),

    # The response curve is plotted through band CENTRES, so at the two
    # extremes it began and ended halfway across the first and last band --
    # a visibly half-drawn band at each end of the plot. A canvas stroke has
    # no layout node, so painted_vs_measured_width / box_intersection /
    # ink_extents are structurally blind to it. These cases EXECUTE the
    # shipping document's own bank block against a recording 2D context and
    # assert the emitted first/last x against the document's OWN bandLeftX /
    # getGeom expressions -- never a constant, so 32/64 bands and any window
    # size stay covered.
    ("materialized_curve_edge_span",
     "curves break at mutes and span each run's band edges", 0,
     [f("test", "test_materialized_curve_edge_span.mjs"),
      f("native-ui", "materialized", "materialized-document.runtime.json")]),
    ("materialized_curve_edge_span",
     "plant: the response line goes back to band centres", 1,
     [f("test", "test_materialized_curve_edge_span.mjs"),
      f("native-ui", "materialized", "materialized-document.runtime.json"),
      "--plant-response-centres"]),
    ("materialized_curve_edge_span",
     "plant: the dsp curve goes back to band centres", 1,
     [f("test", "test_materialized_curve_edge_span.mjs"),
      f("native-ui", "materialized", "materialized-document.runtime.json"),
      "--plant-overlay-centres"]),
    ("materialized_curve_edge_span",
     "plant: band columns start following the zoom window", 1,
     [f("test", "test_materialized_curve_edge_span.mjs"),
      f("native-ui", "materialized", "materialized-document.runtime.json"),
      "--plant-geometry-follows-view"]),
    # The exact form the user reported: both outer plot edges reached, but the
    # line still plunges across every interior mute, so an audible group's
    # curve trails past its own edge. A suite that cannot reject this does not
    # cover the defect it was written for.
    ("materialized_curve_edge_span",
     "plant: the response line spans muted bands again", 1,
     [f("test", "test_materialized_curve_edge_span.mjs"),
      f("native-ui", "materialized", "materialized-document.runtime.json"),
      "--plant-response-spans-mutes"]),
    ("materialized_curve_edge_span",
     "plant: the iir fill slides under muted bands again", 1,
     [f("test", "test_materialized_curve_edge_span.mjs"),
      f("native-ui", "materialized", "materialized-document.runtime.json"),
      "--plant-fill-spans-mutes"]),

    # Morph moves the viewport. Four plants rather than one because each is a
    # DIFFERENT wrong implementation that fails a different assertion, and a
    # single plant that trips everything cannot show which check is load
    # bearing. `--plant-snap` is the defect this shipped to fix; `--plant-linear`
    # is the plausible wrong fix, and the only reason the midpoint assertion
    # exists.
    ("materialized_morph_viewport",
     "morph interpolates the window in log space and commits nothing live", 0,
     [f("test", "test_materialized_morph_viewport.mjs"),
      f("native-ui", "materialized", "materialized-document.runtime.json")]),
    ("materialized_morph_viewport",
     "plant: the window snaps at the midpoint instead of interpolating", 1,
     [f("test", "test_materialized_morph_viewport.mjs"),
      f("native-ui", "materialized", "materialized-document.runtime.json"),
      "--plant-snap"]),
    ("materialized_morph_viewport",
     "plant: the window is interpolated linearly in Hz, not in log space", 1,
     [f("test", "test_materialized_morph_viewport.mjs"),
      f("native-ui", "materialized", "materialized-document.runtime.json"),
      "--plant-linear"]),
    ("materialized_morph_viewport",
     "plant: the morph settles the viewport on every pointer sample", 1,
     [f("test", "test_materialized_morph_viewport.mjs"),
      f("native-ui", "materialized", "materialized-document.runtime.json"),
      "--plant-live-commit"]),
    ("materialized_morph_viewport",
     "plant: the morph ignores the viewport playback switch", 1,
     [f("test", "test_materialized_morph_viewport.mjs"),
      f("native-ui", "materialized", "materialized-document.runtime.json"),
      "--plant-ignore-switch"]),
    # The two defects a user hit on an installed build: a shortcut letter that
    # highlighted without committing or closing, and an opened menu showing a
    # selection AND a keyboard cursor on a different row. Both detectors drive
    # the app when handed one; these cases adjudicate their committed readings
    # so the rules are proved live here without a build.
    ("dropdown_shortcut_dismisses_menu",
     "a letter selects its mode and closes the menu", 0,
     [f(D, "dropdown_shortcut_dismisses_menu.py"),
      "--dump-open", f(E12, "DROPDOWN-KEY-open-menu.layout.json"),
      "--dump-after-key", f(E12, "DROPDOWN-KEY-after-letter-b.layout.json"),
      "--expect", "BOOST"]),
    ("dropdown_shortcut_dismisses_menu",
     "plant: the letter changes nothing and the menu stays open", 1,
     [f(D, "dropdown_shortcut_dismisses_menu.py"),
      "--dump-open", f(E12, "DROPDOWN-KEY-open-menu.layout.json"),
      "--expect", "BOOST", "--plant"]),

    ("dropdown_single_selection_indicator",
     "an opened menu shows nothing but its own selection", 0,
     [f(D, "dropdown_single_selection_indicator.py"),
      "--log", f(E12, "DROPDOWN-INDICATOR-open-with-level-selected.probe.txt")]),
    ("dropdown_single_selection_indicator",
     "plant: the cursor sits on a row that is not the selection", 1,
     [f(D, "dropdown_single_selection_indicator.py"),
      "--log", f(E12, "DROPDOWN-INDICATOR-open-with-level-selected.probe.txt"),
      "--plant"]),

    # The marquee's RESULT. A discontiguous selection has no layout node, so
    # this runs the shipping document's own pointer handlers and asserts the
    # selection SET -- both regions present, the gap between them absent, and
    # the drag's answer invariant to how densely the pointer was sampled.
    # Every plant is a defect a "the handler fired" test would have passed.
    # No --expect-fail here: this harness owns the inversion, and passing both
    # would double-invert and quietly assert the opposite of the intent.
    ("materialized_additive_marquee",
     "one drag adds a second region and another takes it back", 0,
     [f("test", "test_materialized_additive_marquee.mjs"),
      f("native-ui", "materialized", "materialized-document.runtime.json")]),
    ("materialized_additive_marquee",
     "plant: the second marquee wipes the first again", 1,
     [f("test", "test_materialized_additive_marquee.mjs"),
      f("native-ui", "materialized", "materialized-document.runtime.json"),
      "--plant-replaces"]),
    ("materialized_additive_marquee",
     "plant: add-only, so the row's label over-promises", 1,
     [f("test", "test_materialized_additive_marquee.mjs"),
      f("native-ui", "materialized", "materialized-document.runtime.json"),
      "--plant-add-only"]),
    ("materialized_additive_marquee",
     "plant: toggle against the live selection, so the drag strobes", 1,
     [f("test", "test_materialized_additive_marquee.mjs"),
      f("native-ui", "materialized", "materialized-document.runtime.json"),
      "--plant-live-toggle"]),

    # Two SHORTCUTS rows wrapped in the shipped build. The RED fixture is that
    # build's own capture, so the known-bad case is evidence rather than a
    # plant; the budget cases read the shipping artifact and need no capture
    # at all, which is the half that catches the row nobody has written yet.
    ("shortcut_panel_single_line", "every rendered row is one line", 0,
     [f(D, "shortcut_panel_single_line.py"),
      "--layout", f(E12, "SHORTCUT-PANEL-GREEN-single-line.layout.json")]),
    ("shortcut_panel_single_line",
     "known-bad fixture (the shipped build's two wrapped rows)", 1,
     [f(D, "shortcut_panel_single_line.py"),
      "--layout", f(E12, "SHORTCUT-PANEL-RED-wrapped.layout.json")]),
    # The half-applied change: labels shortened, the panel's own capture left
    # behind. Every row reports ONE line and still renders with its label
    # floating above its key chip, because the row box is the two-line box it
    # was captured with. Only the pitch sees it.
    ("shortcut_panel_single_line",
     "known-bad fixture (labels fixed, capture left stale)", 1,
     [f(D, "shortcut_panel_single_line.py"),
      "--layout", f(E12, "SHORTCUT-PANEL-RED-stale-capture.layout.json")]),
    ("shortcut_panel_single_line", "plant: stretch the pitch under one row", 1,
     [f(D, "shortcut_panel_single_line.py"),
      "--layout", f(E12, "SHORTCUT-PANEL-GREEN-single-line.layout.json"),
      "--plant", "stretched-row"]),
    ("shortcut_panel_single_line", "plant: double one row's line box", 1,
     [f(D, "shortcut_panel_single_line.py"),
      "--layout", f(E12, "SHORTCUT-PANEL-GREEN-single-line.layout.json"),
      "--plant", "wrapped-row"]),
    ("shortcut_panel_single_line", "budget alone, no capture", 0,
     [f(D, "shortcut_panel_single_line.py"), "--budget-only"]),
    ("shortcut_panel_single_line",
     "plant: restore the wording that wrapped", 1,
     [f(D, "shortcut_panel_single_line.py"), "--budget-only",
      "--plant", "long-label"]),
    ("shortcut_panel_single_line",
     "plant: the pre-change 280px panel, which had no headroom", 1,
     [f(D, "shortcut_panel_single_line.py"), "--budget-only",
      "--plant", "narrow-panel"]),
]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--only", help="run only cases whose detector contains this")
    ap.add_argument("--verbose", action="store_true",
                    help="print each case's output, not only its verdict")
    ap.add_argument("--plant", action="store_true",
                    help="invert one expectation, so this harness MUST fail")
    args = ap.parse_args()

    cases = [c for c in CASES if not args.only or args.only in c[0]]

    # A detector can need a third-party module this environment cannot supply
    # (the shared runner has no PyPI egress: pip reports 403 Forbidden). That is
    # NOT the same as a broken detector, and it must never read as coverage
    # either. Report it loudly, exclude it from the tally, and keep the count in
    # the summary so a green run cannot hide a detector that never ran.
    unavailable: list[tuple[str, str]] = []
    for det, module in sorted(DETECTOR_REQUIREMENTS.items()):
        if any(c[0] == det for c in cases) and not importlib.util.find_spec(module):
            unavailable.append((det, module))
    if NODE is None:
        for det in sorted({c[0] for c in cases
                           if interpreter(c[3]) is None}):
            unavailable.append((det, "node"))
    if unavailable:
        skipped = {d for d, _ in unavailable}
        cases = [c for c in cases if c[0] not in skipped]
        for det, module in unavailable:
            print(f"  UNAVAILABLE  {det:<30} {module!r} is not importable here"
                  f"  -- NOT run, NOT coverage")
    if not cases:
        print(f"no verdict: --only {args.only!r} selected no case", file=sys.stderr)
        return 2

    if args.plant:
        det, label, want, argv = cases[0]
        cases = [(det, label + " [EXPECTATION INVERTED BY --plant]",
                  1 if want == 0 else 0, argv)] + cases[1:]
        print("CONTROL: inverted the expected exit of the first case\n")

    # Prove the fixtures are present before reporting on any of them. A missing
    # fixture makes every detector "fail" for a reason that is about this
    # checkout, not about the product.
    missing = []
    for _, _, _, argv in cases:
        for a in argv[1:]:
            if a.startswith("-") or "=" in a:
                continue
            p = os.path.join(REPO, a)
            if a.endswith((".json", ".png")) and not os.path.exists(p):
                missing.append(a)
    if missing:
        print("no verdict: missing fixture(s): " + ", ".join(sorted(set(missing))),
              file=sys.stderr)
        return 2

    failures = []
    by_detector: dict[str, list[int]] = {}
    for det, label, want, argv in cases:
        prefix = interpreter(argv)
        if prefix is None:
            print(f"  UNAVAILABLE  {det:<30} no node interpreter"
                  "  -- NOT run, NOT coverage")
            continue
        proc = subprocess.run(prefix + argv, cwd=REPO,
                              capture_output=True, text=True)
        got = proc.returncode
        ok = got == want
        by_detector.setdefault(det, []).append(1 if ok else 0)
        print(f"  {'ok  ' if ok else 'FAIL'}  {det:<32} {label}"
              f"  (want exit {want}, got {got})")
        if args.verbose or not ok:
            tail = (proc.stdout + proc.stderr).strip().splitlines()[-12:]
            for line in tail:
                print(f"        | {line}")
        if not ok:
            failures.append((det, label, want, got))

    dets = len(by_detector)
    print(f"\n{len(cases)} case(s) across {dets} detector(s); "
          f"{len(cases) - len(failures)} as expected, {len(failures)} not")
    if unavailable:
        print(f"{len(unavailable)} detector(s) NOT run for a missing dependency: "
              + ", ".join(f"{d} ({m})" for d, m in unavailable))
    if failures:
        print("\nA detector whose clean case fails is broken or the product "
              "regressed; a detector whose PLANTED case passes can no longer "
              "fail and its green runs prove nothing.", file=sys.stderr)
        return 1
    if args.plant:
        print("BROKEN: the inverted expectation was not reported -- this "
              "harness cannot fail", file=sys.stderr)
        return 4
    print("OK: every detector was shown both passing and failing.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
