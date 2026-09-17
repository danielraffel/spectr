#!/usr/bin/env python3
"""Undo/redo and select-all/none, reachable from the keyboard.

Applied by hand to the shipping materialized document, for the reason
tools/patch_materialized_selection_shortcuts.py records at length: no recipe
in this repo reproduces the checked-in materialized document and both
generators are broken on main, so this script IS the durable record of the
change. Re-running it after a successful pass reports "already applied".

WHAT IT CHANGES

1. THE BANK'S IMPERATIVE SURFACE (`sharedState.current`).
   `onSelectAll` / `onSelectNone` existed only as ContextMenu PROPS, so the
   only way to reach them was to open the menu -- which is exactly the
   complaint. They move onto the bank handle the global key handler already
   holds (`bankRef`), alongside `undo`/`redo` which post to the native
   authority. The menu keeps calling the same functions, so the menu rows and
   the shortcuts cannot drift apart.

2. GESTURE BRACKETS AROUND A DRAG (`undo_gesture_start` / `undo_gesture_end`).
   This editor republishes the COMPLETE processing state on every pointer
   sample (`processing_state_set`), so without a bracket one drag across 32
   bands lands as dozens of separate undo steps. The pair is posted from
   `onPointerDown`/`onPointerUp` -- deliberately around EVERY pointer
   interaction rather than only the editing modes, because the native side
   records nothing for a gesture that changed nothing, so bracketing a click
   that only opens a menu is free. That is proved, not assumed: see
   `undo: a gesture that changed nothing records no step` in
   test/test_editor_authority.cpp.

3. THE CHORDS THEMSELVES, ahead of the bare-letter guard.
   The existing `onKey` guard rejects `e.metaKey || e.ctrlKey || e.altKey ||
   e.shiftKey` and returns, so a Cmd chord could never have reached a handler
   placed after it. The chord block therefore runs BEFORE that guard and
   re-applies the two protections that still matter -- a focused text field,
   and `overlayBlocksShortcut()` -- rather than inheriting them. It does not
   inherit the `shiftKey` rejection, because SHIFT is load-bearing here: it is
   what distinguishes select-none from select-all and redo from undo.

   `Cmd+,` is untouched. It never reaches this handler at all: it is a
   registered native CommandRegistry command consumed in the platform host's
   performKeyEquivalent: before any script dispatch.

WHY THESE KEYS

   CMD+A       select all    -- the platform convention, and unclaimed here:
                              the standalone app menu builds only Quit plus
                              the app's own registered commands, so nothing
                              else answers to it.
   CMD+SHIFT+A select none   -- the counterpart that reads as the inverse of
                              the row above it in the SHORTCUTS panel. ESCAPE
                              was the alternative and is deliberately NOT
                              used: it already dismisses overlays, and a key
                              that both closes a menu and clears a selection
                              does the wrong one half the time. CMD+D was the
                              other candidate and is widely "duplicate".
   CMD+Z       undo
   CMD+SHIFT+Z redo          -- the macOS spelling. CMD+Y is the Windows one
                              and is not bound, rather than bound to a second
                              meaning on a Mac-only product.

KNOWN LIMIT, recorded here because it is invisible from this file: in a
PLUGIN (AU/VST3/CLAP) none of these fire, and neither do the existing
single-letter shortcuts. Pulp's plugin view host routes keys only to focused
C++ widgets and the CommandRegistry; `script_events::dispatch_global_key` is
called from the standalone window host and from nowhere else, so no key
reaches this document's `keydown` listeners inside a DAW. That is a framework
gap, not something this document can fix.
"""
import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCUMENT = os.path.join(REPO,
                        'native-ui/materialized/materialized-document.runtime.json')

# (anchor, replacement). Every anchor MUST occur exactly once, before and
# after. A minified document is one line, so a silently-multiple anchor would
# patch an arbitrary one of them; asserting the count is the only defence.
EDITS = []

# ── 1. bank surface ────────────────────────────────────────────────────
EDITS.append((
    "    sharedState.current = {\n      reset: () => {",
    """    sharedState.current = {
      // Selection and history live on the bank handle so the GLOBAL key
      // handler can reach them. They were menu props only, which is why
      // every one of these actions used to require opening the menu.
      selectAll: () => {
        const nxt = /* @__PURE__ */ new Set();
        for (let i = 0; i < N; i++) nxt.add(i);
        setSelection(nxt);
        return N;
      },
      selectNone: () => {
        let had = 0;
        setSelection((prev) => { had = prev ? prev.size : 0; return /* @__PURE__ */ new Set(); });
        return had;
      },
      selectionSize: () => selectionRef.current ? selectionRef.current.size : 0,
      undo: () => postNative("undo", {}),
      redo: () => postNative("redo", {}),
      reset: () => {"""))

# ── 2. gesture brackets ────────────────────────────────────────────────
EDITS.append((
    "  const onPointerDown = (e) => {\n",
    """  const onPointerDown = (e) => {
    // Opens the undo gesture. Everything this pointer does before the
    // matching end folds into ONE history entry, however many complete
    // state publications the drag streams. A gesture that changes nothing
    // records nothing, so bracketing every press costs nothing.
    postNative("undo_gesture_start", { name: "Edit bands" });
"""))

EDITS.append((
    "  const onPointerUp = (e) => {\n    const p = pointerRef.current;\n    pointerRef.current = { mode: null };\n",
    """  const onPointerUp = (e) => {
    const p = pointerRef.current;
    pointerRef.current = { mode: null };
    // Closes the entry opened on pointer down.
    postNative("undo_gesture_end", {});
"""))

# ── 3. the chords ──────────────────────────────────────────────────────
EDITS.append((
    "    const onKey = (e) => {\n      const t = e.target;\n      if (typeof document.hasFocus === \"function\"",
    """    const onKey = (e) => {
      const t = e.target;
      // Cmd/Ctrl chords, handled BEFORE the bare-letter guard below --
      // that guard returns on any modifier, so nothing placed after it
      // could ever see one. The two protections that still apply are
      // re-stated rather than inherited: a focused text field, and any
      // overlay that owns the keyboard. SHIFT is deliberately NOT
      // rejected here -- it is what separates select-none from
      // select-all and redo from undo.
      if ((e.metaKey || e.ctrlKey) && !e.altKey && !e.repeat) {
        const typing = t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA"
          || t.tagName === "SELECT" || t.isContentEditable);
        if (!typing && !overlayBlocksShortcut()) {
          const ck = typeof e.key === "string" ? e.key.toLowerCase() : e.key;
          const dismiss = window.spectrDismissBandMenu;
          const bank = bankRef.current;
          if (ck === "a") {
            e.preventDefault();
            if (typeof dismiss === "function") dismiss();
            if (e.shiftKey) {
              const had = bank && typeof bank.selectNone === "function"
                ? bank.selectNone() : 0;
              fireStatus(had ? "SELECTION CLEARED" : "NO SELECTION");
            } else {
              const n = bank && typeof bank.selectAll === "function"
                ? bank.selectAll() : 0;
              fireStatus(n ? n + " BANDS SELECTED" : "NO BANDS");
            }
            return;
          }
          if (ck === "z") {
            e.preventDefault();
            if (typeof dismiss === "function") dismiss();
            if (e.shiftKey) {
              if (bank && typeof bank.redo === "function") bank.redo();
              fireStatus("REDO");
            } else {
              if (bank && typeof bank.undo === "function") bank.undo();
              fireStatus("UNDO");
            }
            return;
          }
        }
        return;
      }
      if (typeof document.hasFocus === "function\""""))


def main():
    with open(DOCUMENT, 'r', encoding='utf-8') as f:
        doc = json.load(f)
    html = doc['html']

    applied = 0
    already = 0
    for anchor, replacement in EDITS:
        n_anchor = html.count(anchor)
        n_done = html.count(replacement)
        # The REPLACEMENT's presence is the only sound "already applied"
        # test. Three of these replacements begin with their own anchor, so
        # the anchor still matches after a successful pass -- keying on the
        # anchor being gone would re-apply those every run and nest the
        # inserted block inside itself. Caught by re-running this script,
        # which is the whole reason it reports rather than assumes.
        if n_done >= 1:
            if n_done != 1:
                print("REFUSING: replacement present %d times, expected 1:\n  %r"
                      % (n_done, replacement[:90]), file=sys.stderr)
                return 1
            already += 1
            continue
        if n_anchor != 1:
            print("REFUSING: anchor occurs %d times, expected exactly 1:\n  %r"
                  % (n_anchor, anchor[:90]), file=sys.stderr)
            return 1
        html = html.replace(anchor, replacement, 1)
        applied += 1

    if applied == 0:
        print("already applied (%d/%d edits present)" % (already, len(EDITS)))
        return 0
    if already:
        print("REFUSING: %d edits already present and %d not -- the document is "
              "half-patched and must be inspected by hand"
              % (already, applied), file=sys.stderr)
        return 1

    # Every replacement must now be present exactly once.
    for _, replacement in EDITS:
        if html.count(replacement) != 1:
            print("REFUSING: replacement did not land exactly once",
                  file=sys.stderr)
            return 1

    doc['html'] = html
    with open(DOCUMENT, 'w', encoding='utf-8') as f:
        json.dump(doc, f, ensure_ascii=False, separators=(',', ':'))
    print("applied %d edits" % applied)
    return 0


if __name__ == '__main__':
    sys.exit(main())
