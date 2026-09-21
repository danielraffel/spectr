# A lifted submenu dismisses the menu it belongs to

Status: open, and open **upstream**. Nothing in this repo should be changed to
route around it.

## What a user sees

Right-click a band, press `Modulation ›` (or `Macros ›`). The submenu appears
and the whole context menu disappears in the same frame, submenu included.
Every row behind either submenu — the LFO toggles, the shared-target rows,
`‹ Back`, all eight macro assign/clear rows — is unreachable.

## The mechanism

`ContextMenu` returns a fragment: the menu panel, then `macrosPanel`, then
`modulationPanel`. The two panels are `position: fixed` at coordinates computed
off the root, so they are deliberately **lifted** — siblings of the menu, not
children of its box. Each carries `role="menu"`.

Two Pulp behaviours meet there:

1. `core/view/js/web-compat-style-decl.js` treats `role="menu"` as an author
   *statement* that the node is a dismissable overlay, and calls
   `claimOverlay(id, /*consume=*/true)` for it. The app never asked for that
   claim; correct ARIA is the whole trigger.
2. `View::claim_overlay()` (`core/view/src/view.cpp`) stacks a new claim only
   when the claimant `is_overlay_descendant_of(top)` — a walk up `parent_`.
   Anything else is "a different menu", so the open overlay is popped and its
   `on_overlay_dismissed` callback fires. For the band menu that callback is
   `onDismiss: onClose`.

A lifted submenu can never satisfy a parent-chain descendant test, and
`claim_overlay()` takes no arguments, so there is no way for an author to say
"this overlay stacks on that one". The app has no correct way to express a
`position: fixed` submenu today.

## Evidence

Traced on the built standalone, driving the real scenario (`open=rpress:378,400;
mod=row:Modulation`), with `claimOverlay` / `releaseOverlay` wrapped from JS:

```
CLAIM __behavior_pr_bi -> [data-spectr-band-context-menu]
CLAIM __behavior_pr_er -> [data-spectr-modulation-panel]     <- submenu mounts + claims
DISMISS on band menu id=__behavior_pr_bi                      <- fires immediately after
```

The `dismiss` event was read from a listener attached directly to the band-menu
element, so the victim is identified rather than inferred.

The discriminating experiment: stub `claimOverlay` to ignore the two submenu
panels' ids and change nothing else. The `Modulation` press then leaves both
panels up —

```
SUPPRESSED claim for [data-spectr-modulation-panel] (consume=true)
menu=1 modp=1
mod | result= overlay-routed:click=... | mounted= True
```

— which isolates the overlay claim as the sole cause. It is not the press
routing, not `keepOpen` (the row has it), not `onOutsidePress` (the body
capture listener never fires for an overlay-routed press; it logged nothing
across the whole run), and not a JS throw (the scenario's own refusal guard
reports no `script-ui[error]` after the first frame).

## Where the fix goes

Core Pulp, because that is where both halves live and where every consumer
inherits the same trap: any portaled or lifted submenu — the standard way to
place one that must escape its parent's box — is dismissed as a rival.

The shape it needs is a way for a claim to name the overlay it stacks on, so
the stacking test is a declared relationship rather than a tree walk. Whatever
the API, it has to reach the two authoring surfaces that already claim on the
app's behalf: the `overlay` prop and the `role="menu"` statement.

## What measures it here

`tools/menu_scenario_check.py --bands64` (`Spectr-standalone-band-menu-64`) and
the eight macro rows of `tools/menu_scenario_check.py` without it
(`Spectr-standalone-band-menu-rows`). Both are red on exactly this mechanism
and on nothing else; they are the instruments that will show the upstream fix
arriving, so neither the ARIA role, the overlay declaration nor the assertions
should be softened to make them green.
