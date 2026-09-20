#!/usr/bin/env python3
"""Guard the portal on the CAPABILITY, not on the layer.

`spectrBandMenuPortal` checked that the global menu layer exists and then
called `ReactDOM.createPortal`. Pulp's ReactDOM surface does not have that
method -- `createRoot`, `flushSync`, `render` and `unmountComponentAtNode`
are the whole surface -- so `undefined(menu, layer)` threw "not a function",
the band menu never mounted, and every context press came back not-handled.

The inline fallback was already the right behaviour; it was simply
unreachable for this failure mode, because the guard tested the wrong thing.
The comment claimed a graceful fallback for headless probes, which is what
made the breakage look intentional.

Idempotent: a second run reports already-applied and writes nothing.
Exit: 0 applied or already applied, 1 the anchor is missing or ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"

OLD = '''function spectrBandMenuPortal(menu) {
  const layer = document.querySelector("[data-spectr-global-menu-layer]");
  // The layer is mounted by App before a user can open a context menu.  Keep
  // a graceful inline fallback for headless first-commit probes and hot reload.
  return layer ? ReactDOM.createPortal(menu, layer) : menu;
}'''

NEW = '''function spectrBandMenuPortal(menu) {
  const layer = document.querySelector("[data-spectr-global-menu-layer]");
  // TWO things have to hold, and only one of them used to be checked.
  //
  // The layer is mounted by App before a user can open a context menu, so a
  // missing layer is the hot-reload / first-commit case the inline fallback
  // was written for.
  //
  // The METHOD is the one that actually bit. Pulp's ReactDOM surface is
  // createRoot / flushSync / render / unmountComponentAtNode -- there is no
  // `createPortal`. Calling it threw "not a function" before any menu could
  // mount, so every context press came back not-handled and the whole band
  // menu was dead wherever this shim runs, standalone and AU alike. The
  // fallback below was correct the entire time and simply unreachable,
  // because the guard tested the layer instead of the capability.
  const canPortal = typeof ReactDOM !== "undefined" && ReactDOM !== null
    && typeof ReactDOM.createPortal === "function";
  return (layer && canPortal) ? ReactDOM.createPortal(menu, layer) : menu;
}'''


def main():
    document = json.loads(PATH.read_text())
    html = document["html"]
    if html.count(NEW) == 1 and OLD not in html:
        print("portal capability guard already applied")
        return 0
    if html.count(OLD) != 1 or NEW in html:
        sys.exit("FAIL: portal anchor occurs %d times, expected 1" % html.count(OLD))
    document["html"] = html.replace(OLD, NEW, 1)
    PATH.write_text(json.dumps(document, separators=(",", ":"), ensure_ascii=False) + "\n")
    print("portal now guards the capability, not just the layer")
    return 0


if __name__ == "__main__":
    sys.exit(main())
