# Pulp framework bug repros

Minimal reproductions for two @pulp/react framework bugs filed from
Spectr's native-react editor work. Each repro is a single < 100-line
file that compiles into the same IIFE bundle shape as the main
`editor-port.tsx` build, so the framework team can reproduce in one
command.

Build status note: each `npm run build:repro-*` produces a runnable
`dist/repro-<NNNN>.js` bundle. Running it through a Pulp standalone
host requires a one-line CMake change to embed that bundle instead of
`dist/editor.js` (or just temporarily renaming the artifact). The
buildable bundle alone reproduces the symptom under
`pulp-screenshot --script dist/repro-<NNNN>.js` once binary embedding
is wired — no Spectr-specific bridge wiring is needed.

## #1147 — popover content doesn't render (`repro-1147-popover-render.tsx`)

**Build:** `npm run build:repro-1147`

**What to see:** two absolutely-positioned popovers stacked vertically
inside a 1200×800 viewport. Both are `position: absolute, bottom: 34,
left: 0` inside a `position: relative` parent — the same shape as
Spectr's `EditModePopover` and `AnalyzerPopover` (see
`spectr-editor-extracted.js` lines 3123-3218).

- Case (A) — `AnalyzerPopover` pattern: a flex-column with three
  `<button>` rows, each containing a couple of plain `<span>` children.
  No `<svg>`. In the live Spectr build, content escapes the popover
  panel and renders elsewhere.
- Case (B) — `EditModePopover` pattern: same flex-column shell, but
  each row is a flex-row with an inline `<svg>` icon and a `flex: 1`
  text column nested inside. In the live Spectr build, this popover
  renders empty.

**What's broken:** content inside the absolutely-positioned popover
either escapes the panel (A) or fails to render at all (B). Working
around the position+flex combo in app code didn't help — looks like a
framework-side absolute-positioning + nested-flex issue.

## #1148 — overlay clicks / outside-close / ESC don't dispatch (`repro-1148-overlay-clicks.tsx`)

**Build:** `npm run build:repro-1148`

**What to see:** a single trigger button. Clicking it opens an absolutely-
positioned overlay panel containing three counter buttons, plus a
transparent full-window backdrop behind it.

**Test affordances** (all should work — none do today):

1. Click any of the three buttons inside the panel → should bump the
   count shown in the header. Today: count stays at 0.
2. Click the transparent backdrop → should close the panel
   (`setOpen(false)`). Today: nothing.
3. Press <kbd>Esc</kbd> → a `useEffect` registers a `keydown` listener
   on `document`; the handler calls `setOpen(false)`. Today: panel
   stays open.

**What's broken:** clicks on children of an absolutely-positioned
panel never reach the child's `onClick`, transparent backdrops never
receive their click, and document-level keydown listeners registered
inside a popover-open `useEffect` never fire. All three are needed for
a normal popover/overlay UX.
