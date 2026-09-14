#!/usr/bin/env python3
"""Stop a persistent level state from shadowing every transient overlay state.

THE DEFECT

    `resolveCapturedStateFromAtlas()` scans `capturedStates` from the end and
    returns the first state whose `match` selector resolves.  Array order is
    therefore the whole precedence rule, and the atlas mixes two kinds of state
    under it:

      * TRANSIENT overlays -- `edit`, `analyzer`, `pattern`, `bands`,
        `overflow`, `settings`, `help`, ... -- matched by a node that exists
        only while that surface is open.
      * LEVEL states -- `snapshots-morph` -- matched by
        `[data-spectr-snapshots-ready="true"]`, an attribute that rides the
        persistent "SNAPSHOT" label in the transport bar and reads

            snapshotStatus.A && snapshotStatus.B ? "true" : void 0

        so it is true from the moment the second snapshot is captured until the
        app is relaunched.

    `snapshots-morph` sits at index 5, above `bands`(0), `overflow`(1),
    `edit`(2), `analyzer`(3) and `pattern`(4).  Once both snapshot slots fill,
    the reverse scan reaches it before any of those five and returns it every
    time -- so opening one of those menus can never resolve to its own state
    again for the rest of the session.

    Their captured `layout_bindings` are what stacks each option row:
    `edit.materialized.json` pins `button[N]/span[1]/span[0]` at `top:0` and
    `span[1]` at `top:19`.  `snapshots-morph.materialized.json` carries no
    binding inside any popover.  Unpinned, each row falls back to live layout,
    where `<span style={{flex:1}}>` and `<button style={{display:"block"}}>`
    are both created as ROWS -- so the label and the description land
    side-by-side, the label column crushes to a few glyphs, and the row reads
    as "SCULPT / Free-" stacked and overlapping beside a narrow description.

    Nothing reported it: the apply loop counts `layout_applied == expected` and
    `layout_node_miss == 0` in the degraded state, because the bindings are not
    missing -- a different, smaller table was substituted wholesale.

THE FIX

    A level state must never outrank a transient one.  It is a FALLBACK: the
    answer when nothing more specific is open.  The scan now remembers a
    matching level state and keeps looking, returning it only if no transient
    state matched.

    This keeps the atlas order untouched (regeneration-safe) and encodes the
    rule where the precedence decision is actually made.

Idempotent.  Single writer for this region of runtime.js.
Exit: 0 patched or already patched, 1 the expected source was not found.
"""

import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RUNTIME = os.path.join(REPO, "native-ui", "materialized", "runtime.js")

MARKER = "__spectrLevelCapturedStates"

BEFORE = """  function resolveCapturedStateFromAtlas() {
    for (let index = capturedStates.length - 1; index >= 0; --index) {
      const state = capturedStates[index];
      if (state.match) {
        const match = g5.__pulpFindMaterializedElement__(
          state.match.selector,
          state.match.ancestor
        );
        if (state.id === "settings") {
          const liveMatch = match || globalThis.document?.querySelector?.(
            "[data-spectr-settings-panel]");
          const marker = liveMatch?.getAttribute?.("data-spectr-settings-live");
          const owner = liveMatch?.parentElement || liveMatch?._parentElement;
          const hidden = owner?.style?.display === "none"
            || liveMatch?.style?.display === "none";
          if (liveMatch && owner && !hidden && marker === "true") return state.id;
        } else if (match) {
          return state.id;
        }
      }
    }
    return "";
  }"""

AFTER = """  // States whose match selector describes a LEVEL that persists for the rest
  // of the session, not a surface that is currently open. `snapshots-morph`
  // matches [data-spectr-snapshots-ready="true"], which rides the permanent
  // transport label and is true from the second capture until relaunch.
  // Resolution is "last match in array order wins", so without this a level
  // state placed above a transient one shadows it forever: with both snapshots
  // filled, opening EDIT MODE / ANALYZER / PRESET could no longer resolve to
  // its own captured state, its option rows lost the bindings that stack them,
  // and each row re-laid out side-by-side. A level state is a FALLBACK -- the
  // answer when nothing more specific is open -- never a winner over an open
  // surface.
  const __spectrLevelCapturedStates = new Set(["snapshots-morph"]);
  function resolveCapturedStateFromAtlas() {
    let levelFallback = "";
    for (let index = capturedStates.length - 1; index >= 0; --index) {
      const state = capturedStates[index];
      if (state.match) {
        const match = g5.__pulpFindMaterializedElement__(
          state.match.selector,
          state.match.ancestor
        );
        if (state.id === "settings") {
          const liveMatch = match || globalThis.document?.querySelector?.(
            "[data-spectr-settings-panel]");
          const marker = liveMatch?.getAttribute?.("data-spectr-settings-live");
          const owner = liveMatch?.parentElement || liveMatch?._parentElement;
          const hidden = owner?.style?.display === "none"
            || liveMatch?.style?.display === "none";
          if (liveMatch && owner && !hidden && marker === "true") return state.id;
        } else if (match) {
          if (__spectrLevelCapturedStates.has(state.id)) {
            if (levelFallback === "") levelFallback = state.id;
            continue;
          }
          return state.id;
        }
      }
    }
    return levelFallback;
  }"""


def main() -> int:
    with open(RUNTIME, encoding="utf-8") as handle:
        source = handle.read()

    if MARKER in source:
        print("patch_materialized_state_precedence: already applied")
        return 0

    if source.count(BEFORE) != 1:
        print("patch_materialized_state_precedence: expected resolver source not "
              f"found exactly once (found {source.count(BEFORE)})", file=sys.stderr)
        return 1

    with open(RUNTIME, "w", encoding="utf-8") as handle:
        handle.write(source.replace(BEFORE, AFTER))
    print("patch_materialized_state_precedence: applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
