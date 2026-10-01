#!/usr/bin/env python3
"""Settings > FREEZE KEYS: the "Restart loop on note" switch.

Freeze Keys plays the held sound from MIDI. On a loop hold its notes either
start the loop from its top (ON, the default) or join it where it is playing
(OFF). The switch:

- is its own Settings group, after every other Freeze row and before
  APPEARANCE, anchored on APPEARANCE rather than on the FREEZE group, so it
  stays put whether or not that group or its Hold length row is there;
- is not a host parameter: it reads `freeze_keys.restart_loop` from the
  hydration payload, which the processor sends only in a Freeze Keys build,
  so a shipping build renders nothing here; and writes through
  `freeze_keys_restart_set`;
- keeps its own store (`globalThis.__spectrFreezeKeys`), apart from the
  freeze store;
- names the root key (the key that plays the held sound at its own pitch)
  in its subtitle from `freeze_keys.root_name` (Logic naming, MIDI 60 = C3).

The component is defined inside the Settings surface, beside the other
Settings groups, so the Settings panel's own browser suite, which evaluates
that span alone, carries it.

WHY A SCRIPT: the shipping document is one minified line and the materialized
generator cannot rebuild it. Each patch point is asserted to occur exactly
once, a half-patched document is refused, and a second run reports "already
applied".

Exit codes: 0 applied or already applied, 1 a patch point is missing or
ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

MARKER = "function SpectrFreezeKeysSettings()"

FUNCTION = '''// Settings > FREEZE KEYS. Present only where the processor plays Freeze Keys:
// it reads freeze_keys from the hydration payload and renders nothing until
// (or unless) that arrives. Not a host parameter. The root key's name is the
// processor's (Logic naming, MIDI 60 = C3).
function spectrFreezeKeysStore() {
  const store = globalThis.__spectrFreezeKeys
    || (globalThis.__spectrFreezeKeys = { restartLoop: null, rootName: "C3", listeners: [] });
  if (!store.subscribed && window.pulp && typeof window.pulp.on === "function") {
    store.subscribed = true;
    window.pulp.on("processing_state_hydrate", (message) => {
      const keys = message && message.payload && message.payload.freeze_keys;
      if (!keys || typeof keys.restart_loop !== "boolean") return;
      if (typeof keys.root_name === "string") store.rootName = keys.root_name;
      if (keys.restart_loop === store.restartLoop) return;
      store.restartLoop = keys.restart_loop;
      store.listeners.slice().forEach((fn) => {
        try { fn(); } catch (error) {
          console.error("[Spectr] freeze keys listener failed", error);
        }
      });
    });
  }
  return store;
}
function SpectrFreezeKeysSettings() {
  const store = spectrFreezeKeysStore();
  const [, setRevision] = React.useState(0);
  React.useEffect(() => {
    const sync = () => setRevision((n) => n + 1);
    store.listeners.push(sync);
    return () => {
      const at = store.listeners.indexOf(sync);
      if (at >= 0) store.listeners.splice(at, 1);
    };
  }, []);
  if (typeof store.restartLoop !== "boolean") return null;
  const write = (next) => {
    store.restartLoop = next === true;
    setRevision((n) => n + 1);
    if (!window.pulp || typeof window.pulp.postMessage !== "function") return;
    Promise.resolve(window.pulp.postMessage("freeze_keys_restart_set",
      { enabled: store.restartLoop }, "spectr-freeze-keys-restart")).catch((error) =>
        console.error("[Spectr] restart loop write failed", error));
  };
  return /* @__PURE__ */ React.createElement(SpectrSettingsGroup, { marker: "freeze-keys", title: "FREEZE KEYS", subtitle: "While frozen, MIDI notes play the held sound up and down the keyboard; " + store.rootName + " plays it at its own pitch." }, /* @__PURE__ */ React.createElement(SpectrSettingsField, { label: "Restart loop on note", hint: "Each note plays a looped hold from its start" }, /* @__PURE__ */ React.createElement("div", { "data-spectr-freeze-keys-restart": store.restartLoop ? "on" : "off", "data-spectr-freeze-keys-root": store.rootName }, /* @__PURE__ */ React.createElement(SpectrSettingsToggle, { value: store.restartLoop, onChange: write }))));
}
'''
FUNCTION_ANCHOR = "// The close button owns its hover and press look, so pointer-enter and\n"

GROUP_OLD = ('/* @__PURE__ */ React.createElement(SpectrSettingsGroup, '
             '{ marker: "general", title: "APPEARANCE"')
GROUP_NEW = ('/* @__PURE__ */ React.createElement(SpectrFreezeKeysSettings, null), '
             + GROUP_OLD)


def once(html, text, label):
    count = html.count(text)
    if count != 1:
        sys.exit("FAIL %s: patch point occurs %d times, expected 1" % (label, count))


def main():
    raw = open(PATH, encoding="utf-8").read()
    document = json.loads(raw)
    html = document["html"]

    if MARKER in html:
        for marker in (FUNCTION, GROUP_NEW):
            if html.count(marker) != 1:
                sys.exit("FAIL: the Freeze Keys Settings group is present but %r "
                         "is not; the document is half patched" % marker[:60])
        print("already applied  Settings > FREEZE KEYS > Restart loop on note")
        return 0

    once(html, FUNCTION_ANCHOR, "Settings close button")
    once(html, GROUP_OLD, "Settings APPEARANCE group")
    html = html.replace(GROUP_OLD, GROUP_NEW, 1)
    html = html.replace(FUNCTION_ANCHOR, FUNCTION + FUNCTION_ANCHOR, 1)

    document["html"] = html
    # Keep every byte outside the html payload as it was, and encode the html
    # the way tools/git/merge_materialized_runtime.py does (ensure_ascii=False).
    head, tail = raw.split('"html":"', 1)
    rest = tail.split('","mime_type":', 1)
    if len(rest) != 2:
        sys.exit("FAIL: the html payload is not followed by mime_type")
    out = (head + '"html":' + json.dumps(html, ensure_ascii=False)
           + ',"mime_type":' + rest[1])
    if json.loads(out) != document:
        sys.exit("FAIL: re-encoding the document changed more than its html")
    open(PATH, "w", encoding="utf-8").write(out)
    print("applied          Settings > FREEZE KEYS > Restart loop on note")
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
