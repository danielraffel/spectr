#!/usr/bin/env python3
"""Settings gains an UPDATES group in the standalone app (never in a plug-in).

Spectr.app checks for updates with Sparkle. Pulp gives the app menu its
"Check for Updates..." item (directly under "About Spectr") by itself; this
adds the matching Settings group, laid out like the rest of the panel:

    UPDATES          Version 1.0.7 . Last checked: 2026-10-04 14:03
    Check automatically   Automatically check for updates      [switch]
    [CHECK FOR UPDATES...]
    Updates download from Spectr's GitHub releases. Installing an update
    quits and reopens Spectr, briefly stopping its audio, and asks for an
    administrator password. Updates are never installed automatically.
    VIEW RELEASES

Everything shown comes from Pulp's update service through the editor bridge
(`pulp_updates_get` / `_check` / `_set_automatic` / `_open_releases`, which
src/editor_bridge.cpp registers with pulp::format::add_app_update_handlers):
the toggle is Sparkle's own persisted automatic-check setting, the note is
generated natively from what cmake/SpectrSparkle.cmake declares, and the
button runs Sparkle's own check. The editor code is the same in every format,
so the group asks rather than assumes: a plug-in has no update service and
answers `available: false`, an SDK without the bridge answers "unknown
message type", and a dev identity built without Sparkle has no service
either -- in all three the group renders nothing. It re-reads the status
once a second while Settings is open, and commits only when it changed.

It sits after MODULATION and before ABOUT.

Idempotent: exact substitutions, each asserted unique.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")


def escaped(value):
    # ensure_ascii=False: the artifact stores non-ASCII literally (see
    # tools/git/merge_materialized_runtime.py), so must every edit.
    return json.dumps(value, ensure_ascii=False)[1:-1]


COMPONENT = r'''function spectrUpdatesCall(type, payload) {
  const dispatch = globalThis.__spectrEditorDispatch;
  if (typeof dispatch !== "function") return null;
  try {
    const response = JSON.parse(dispatch(JSON.stringify({ type, payload: payload || {}, id: "spectr-" + type })));
    return response && response.ok === true ? response : null;
  } catch (error) {
    return null;
  }
}
function SpectrUpdatesSettings({ listening }) {
  const [status, setStatus] = React.useState(() => spectrUpdatesCall("pulp_updates_get"));
  const lastJson = React.useRef(JSON.stringify(status));
  const take = (response) => {
    if (!response) return;
    // Drop the per-request flags so the poll compares the status alone.
    const { started, applied, opened, ...next } = response;
    const json = JSON.stringify(next);
    if (json === lastJson.current) return;
    lastJson.current = json;
    setStatus(next);
  };
  React.useEffect(() => {
    if (!listening) return void 0;
    take(spectrUpdatesCall("pulp_updates_get"));
    const timer = setInterval(() => take(spectrUpdatesCall("pulp_updates_get")), 1000);
    return () => clearInterval(timer);
  }, [listening]);
  if (!status || status.available !== true) return null;
  const subtitle = [status.versionText, status.lastCheckText].filter(Boolean).join(" · ");
  return /* @__PURE__ */ React.createElement(
    SpectrSettingsGroup,
    { marker: "updates", title: "UPDATES", subtitle },
    /* @__PURE__ */ React.createElement(SpectrSettingsField, { label: "Check automatically", hint: "Automatically check for updates" }, /* @__PURE__ */ React.createElement("div", { "data-spectr-updates-automatic": status.automaticChecks ? "on" : "off" }, /* @__PURE__ */ React.createElement(SpectrSettingsToggle, { value: status.automaticChecks === true, onChange: (v) => take(spectrUpdatesCall("pulp_updates_set_automatic", { on: v === true })) }))),
    /* @__PURE__ */ React.createElement("button", { "data-spectr-check-for-updates": true, onClick: () => take(spectrUpdatesCall("pulp_updates_check")), disabled: status.canCheckNow !== true, style: { alignSelf: "flex-start", width: 176, height: 26, padding: "0 10px", borderRadius: 3, border: "1px solid rgba(180,210,255,0.3)", background: "rgba(120,180,255,0.10)", color: "rgba(220,235,255,0.95)", fontFamily: "var(--mono)", fontSize: 9.5, letterSpacing: 0.8, cursor: "pointer", display: "inline-flex", alignItems: "center", justifyContent: "center", lineHeight: 1 } }, /* @__PURE__ */ React.createElement("span", { style: { display: "inline-flex", alignItems: "center", justifyContent: "center", width: "100%", height: "100%", lineHeight: 1, textAlign: "center", pointerEvents: "none" } }, "CHECK FOR UPDATES…")),
    status.note && /* @__PURE__ */ React.createElement("div", { "data-spectr-updates-note": true, style: { fontSize: 9.5, opacity: 0.62, fontFamily: "var(--sans)", lineHeight: 1.45 } }, status.note),
    status.releasesUrl && /* @__PURE__ */ React.createElement("button", { "data-spectr-updates-releases": true, title: status.releasesUrl, onClick: () => take(spectrUpdatesCall("pulp_updates_open_releases")), style: { alignSelf: "flex-start", height: 18, padding: 0, border: "none", background: "transparent", color: "hsl(200,70%,65%)", fontFamily: "var(--mono)", fontSize: 9, letterSpacing: 0.8, cursor: "pointer" } }, "VIEW RELEASES")
  );
}
/* materialized-updates-owner */
'''

EDITS = [
    ("the UPDATES group component sits before the ABOUT one",
     "function SpectrBuildInfo({ showGpuStats = true }) {",
     COMPONENT + "function SpectrBuildInfo({ showGpuStats = true }) {"),
    ("Settings shows it after MODULATION and before ABOUT",
     "React.createElement(SpectrModulationSettings, { listening: open }), settings.showBuildInfo !== false && ",
     "React.createElement(SpectrModulationSettings, { listening: open }), React.createElement(SpectrUpdatesSettings, { listening: open }), settings.showBuildInfo !== false && "),
]


def main():
    raw = open(PATH, encoding="utf-8").read()
    changed = False
    for label, old, new in EDITS:
        old_e, new_e = escaped(old), escaped(new)
        if raw.count(new_e) == 1:
            print("already applied ", label)
            continue
        count = raw.count(old_e)
        if count != 1:
            sys.exit("FAIL %s: patch point occurs %d times, expected 1" % (label, count))
        raw = raw.replace(old_e, new_e, 1)
        changed = True
        print("applied         ", label)
    html = json.loads(raw)["html"]
    for needle in ("function SpectrUpdatesSettings(",
                   "React.createElement(SpectrUpdatesSettings, { listening: open })"):
        if html.count(needle) != 1:
            sys.exit("FAIL: %r is not present exactly once" % needle)
    if not changed:
        print("no change needed")
        return 0
    open(PATH, "w", encoding="utf-8").write(raw)
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
