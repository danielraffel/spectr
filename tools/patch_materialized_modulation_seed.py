#!/usr/bin/env python3
"""Let a late-mounting consumer start from the last modulation frame.

`useSpectrModulationState` is a HOOK, so every consumer owns its state. The
band menu mounts a fresh instance each time it opens -- long after the editor
did -- and that instance started at `ready: false` holding a DEFAULT value,
then waited for its own round trip. Six menu rows carry
`disabled: !modulationReady`, so opening the menu and tapping `LFO 1` hit a
disabled row and did nothing. The Settings panel, which destructures the same
hook and never reads `ready`, worked the whole time; that difference is the
discriminator.

`ready` is NOT dropped. It appears exactly twice in the hook -- its
declaration and its return -- so it guards no publisher, but the reason it
exists is real and is asserted by `test_context_modulation_behavior.mjs`: the
menu toggles with `!modulation.enabled`, so a consumer that has never seen a
frame would invert a DEFAULT rather than the truth. The fix is to stop being
uninformed, not to act while uninformed.

So the last frame is remembered process-wide and seeds the next consumer:
`ready` is now false only when nothing is known at all, instead of on every
mount. The mapping is lifted to a module-level function so a frame can be
normalized without a component instance, which is what lets `App` -- always
mounted, already subscribed to both hydration and live frames -- record one
before any menu exists. That matters beyond the race: if the menu's own
round trip is slow or never resolves in a host, the seed is the only thing
that makes the rows live.

`globalThis.__spectrMorphViewport` in this same hook is the existing
precedent for a process-wide cache.

Idempotent. Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = (Path(__file__).resolve().parents[1]
        / "native-ui/materialized/materialized-document.runtime.json")

SENTINEL = "__spectrModulationLast"

STATE_OLD = '''  const [ready, setReady] = React.useState(false);
  const [value, setValue] = React.useState({ enabled: false, shape: 0, rate: 4, depth: 0.5, target: 0, lfo2Enabled: false, lfo2Shape: 0, lfo2Rate: 4, lfo2Depth: 0, targetSelection: "all" });'''

STATE_NEW = '''  // Seeded from the last frame native sent, so a consumer that mounts late
  // does not start stale by construction. `ready` is false only when nothing
  // is known at all -- which is still the right moment to refuse a toggle,
  // because the menu inverts `!modulation.enabled` and would otherwise invert
  // a default.
  const [ready, setReady] = React.useState(() => globalThis.__spectrModulationLast != null);
  const [value, setValue] = React.useState(() => ({ enabled: false, shape: 0, rate: 4, depth: 0.5, target: 0, lfo2Enabled: false, lfo2Shape: 0, lfo2Rate: 4, lfo2Depth: 0, targetSelection: "all", ...(globalThis.__spectrModulationLast || {}) }));'''

READ_START = '  const readNativeModulation = React.useCallback((modulation) => {'
# `const next = {` alone also matches FilterBank's history object, so the
# anchor carries the first field of the modulation mapping.
MAP_START = '    const next = {\n      enabled: modulation.enabled === true,'
PENDING = '    const pending = pendingWritesRef.current;'
READ_END = '''    setValue((current) => ({ ...current, ...next }));
  }, []);'''

TARGET_SELECTION = ('    next.targetSelection = next.targetMask === 15 ? "all" '
                    ': next.targetMask === 0 ? "none" : "custom";')

# Placed AFTER the hook, not before it: `test_context_modulation_behavior`
# extracts the slice from the hook's head to `SpectrModulationSettings`, so
# anything inserted above the head is invisible to that test and the helper
# would be undefined there. A function declaration hoists, so defining it
# below the hook that calls it is fine.
HOOK_TAIL = 'window.useSpectrModulationState = useSpectrModulationState;'

RECORDER = '''
// Normalizing a frame must not need a component, because the consumer that
// needs the answer most -- the band menu -- does not exist yet when the
// editor hydrates.
function spectrModulationFromNative(modulation) {
  if (!modulation || typeof modulation !== "object") return null;
%(mapping)s%(target_selection)s
  return next;
}
// `App` is mounted for the whole session and already receives both frames;
// this is how the cache is warm before any menu is opened for the first time.
window.spectrRecordModulationFrame = (modulation) => {
  const next = spectrModulationFromNative(modulation);
  if (next) globalThis.__spectrModulationLast = next;
};
'''

APP_EDITS = [
    ('hydration frame',
     '''    const unsubscribeHydration = window.pulp.on("processing_state_hydrate", (message) => {
      const state = window.SpectrNativeState.parse(message && message.payload);''',
     '''    const unsubscribeHydration = window.pulp.on("processing_state_hydrate", (message) => {
      if (window.spectrRecordModulationFrame)
        window.spectrRecordModulationFrame(message && message.payload && message.payload.modulation);
      const state = window.SpectrNativeState.parse(message && message.payload);'''),
    ('live frame',
     '''    const unsubscribeLiveState = window.pulp.on("processing_state_live", (message) => {
      const state = parseSpectrNativeLiveState(message && message.payload);''',
     '''    const unsubscribeLiveState = window.pulp.on("processing_state_live", (message) => {
      if (window.spectrRecordModulationFrame)
        window.spectrRecordModulationFrame(message && message.payload && message.payload.modulation);
      const state = parseSpectrNativeLiveState(message && message.payload);'''),
]


def once(html, anchor, label):
    if html.count(anchor) != 1:
        sys.exit("FAIL %s: anchor occurs %d times, expected 1"
                 % (label, html.count(anchor)))


def main():
    document = json.loads(PATH.read_text())
    html = document["html"]

    if SENTINEL in html:
        print("modulation seed already applied")
        return 0

    for label, anchor in (("hook state", STATE_OLD), ("hook tail", HOOK_TAIL),
                          ("reader start", READ_START), ("reader end", READ_END),
                          ("mapping start", MAP_START), ("pending merge", PENDING),
                          ("target selection", TARGET_SELECTION)):
        once(html, anchor, label)
    for label, old, _ in APP_EDITS:
        once(html, old, label)

    start = html.index(READ_START)
    end = html.index(READ_END, start) + len(READ_END)
    block = html[start:end]
    map_start = block.index(MAP_START)
    map_end = block.index(PENDING)
    mapping = block[map_start:map_end].rstrip("\n")

    reader_new = '''  const readNativeModulation = React.useCallback((modulation) => {
    const next = spectrModulationFromNative(modulation);
    if (!next) return;
    // Remember it for whatever mounts next, BEFORE the pending merge below
    // rewrites this object with values native has not acknowledged yet.
    globalThis.__spectrModulationLast = { ...next };
    setReady(true);
%(pending)s
    Object.keys(pending).forEach((key) => {
      if (pending[key] === next[key]) delete pending[key];
      else next[key] = pending[key];
    });
%(target_selection)s
%(read_end)s''' % {"pending": PENDING, "target_selection": TARGET_SELECTION,
                   "read_end": READ_END}

    html = html[:start] + reader_new + html[end:]
    html = html.replace(
        HOOK_TAIL,
        HOOK_TAIL + "\n"
        + (RECORDER % {"mapping": mapping,
                       "target_selection": "\n" + TARGET_SELECTION}).lstrip("\n"), 1)
    html = html.replace(STATE_OLD, STATE_NEW, 1)
    for _, old, new in APP_EDITS:
        html = html.replace(old, new, 1)

    for token in (SENTINEL, "function spectrModulationFromNative(",
                  "window.spectrRecordModulationFrame"):
        if token not in html:
            sys.exit("FAIL: %r absent after patching" % token)
    # The modulation mapping specifically -- `const next = {` on its own also
    # matches FilterBank's history object and would count two forever.
    if html.count("      enabled: modulation.enabled === true,") != 1:
        sys.exit("FAIL: the modulation mapping was duplicated rather than moved")
    reader = html[html.index(READ_START):
                  html.index(READ_END, html.index(READ_START))]
    if "enabled: modulation.enabled === true," in reader:
        sys.exit("FAIL: the mapping is still inline in the reader")

    document["html"] = html
    PATH.write_text(json.dumps(document, separators=(",", ":"),
                               ensure_ascii=False) + "\n")
    print("a late consumer now seeds from the last modulation frame")
    return 0


if __name__ == "__main__":
    sys.exit(main())
