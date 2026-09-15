#!/usr/bin/env python3
"""Add the Latency control to the shipping Settings panel.

The render mode is saved, recalled, migrated and wired to the renderer, and
the About guide explains what the two modes are for. None of that is reachable:
until this patch there is no control anywhere in the shipping editor that
changes it, so the mode a project recalls is whatever it was born with.

WHY THIS IS A PATCH SCRIPT AND NOT AN EDIT TO A SOURCE FILE
native-ui/materialized/materialized-document.runtime.json IS the shipping
editor -- the native host loads it, every native test drives it, and no recipe
in this repo reproduces it (danielraffel/spectr#48). native-ui/src/editor.tsx
is an unrelated stub with no Settings panel, and resources/editor.html has
diverged. So an editor-behaviour change is applied to the committed blob by
hand, and this script is the durable record of what was applied.

THE CONTROL IS NOT A HOST PARAMETER, and that shapes every read below. Pulp
cannot register a non-automatable parameter, so the mode lives in the
supplemental plugin-state blob and reaches the panel through the hydration
payload only. A live automation frame therefore OMITS it. Every read here
updates on PRESENCE rather than on truthiness, exactly as morph_applies_viewport
does: treating an absent block as "no mode selected" would reset the control on
the next automation write and make it unusable.

The option list, both labels, both guidance lines and both millisecond figures
all come from the payload. Nothing is written here. A figure typed into the
panel would be wrong at 96 kHz and wrong again the day a mode's geometry moves,
and a label typed here would drift from the one the About guide and its
detector agree on.

Exit codes: 0 applied or already applied, 1 a patch point is missing or
ambiguous. Idempotent: re-running after a successful pass reports
"already applied".
"""
import json
import os
import sys

PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                    "native-ui", "materialized",
                    "materialized-document.runtime.json")

# Presence of this marker means the document is already patched.
MARKER = "__spectrLatency"

_HYDRATE_OLD = "    const projections = payload.snapshots || {};"
_HYDRATE_NEW = (
    "    // The Latency control is not a host parameter and rides the\n"
    "    // hydration payload only, so a live automation frame omits it.\n"
    "    // Update on PRESENCE, never on truthiness: reading an absent block\n"
    "    // as \"no mode\" would reset the control on the next automation write,\n"
    "    // which is the same defect morph_applies_viewport guards against.\n"
    "    if (payload && payload.latency\n"
    "        && typeof payload.latency.mode === 'string')\n"
    "      (globalThis.__spectrLatency || (globalThis.__spectrLatency = {}))\n"
    "        .state = payload.latency;\n"
    "    const projections = payload.snapshots || {};")

_COMPONENT = (
    "function SpectrLatencySettings() {\n"
    "  const store = globalThis.__spectrLatency\n"
    "    || (globalThis.__spectrLatency = {});\n"
    "  const hydrated = store.state;\n"
    "  const [pending, setPending] = React.useState(null);\n"
    "  // Render nothing rather than a broken control when the payload has not\n"
    "  // arrived or carries no options. An empty chip row reads as a bug; an\n"
    "  // absent group reads as a panel that has not finished loading, which is\n"
    "  // what is actually true.\n"
    "  if (!hydrated || !Array.isArray(hydrated.options)\n"
    "      || hydrated.options.length === 0) return null;\n"
    "  const options = hydrated.options;\n"
    "  const active = pending || hydrated.mode;\n"
    "  const matches = options.filter(function (option) {\n"
    "    return option.mode === active;\n"
    "  });\n"
    "  const current = matches.length ? matches[0] : options[0];\n"
    "  const millis = function (ms) {\n"
    "    return (typeof ms === 'number')\n"
    "      ? (ms < 10 ? ms.toFixed(1) : String(Math.round(ms))) + \" ms\"\n"
    "      : \"\";\n"
    "  };\n"
    "  // Optimistic locally so the chips do not lag a round trip, then tell the\n"
    "  // processor, which owns the value and persists it. The token travels,\n"
    "  // never an index: an index would make this list's order part of the wire\n"
    "  // contract, so reordering the chips would silently change what they do.\n"
    "  const publish = function (next) {\n"
    "    if (!next || next === active) return;\n"
    "    setPending(next);\n"
    "    store.state = Object.assign({}, store.state, { mode: next });\n"
    "    if (!window.pulp || !window.pulp.postMessage) return;\n"
    "    Promise.resolve(window.pulp.postMessage(\"render_mode_set\","
    " { mode: next }, \"spectr-render-mode\")).catch(function (error) {\n"
    "      console.error(\"[Spectr] latency mode write failed\", error);\n"
    "    });\n"
    "  };\n"
    "  const hint = current\n"
    "    ? (current.description + \" (\" + millis(current.ms) + \")\")\n"
    "    : \"\";\n"
    "  return /* @__PURE__ */ React.createElement(SpectrSettingsGroup,"
    " { marker: \"latency\", title: \"LATENCY\","
    " subtitle: \"Changing this rebuilds the processor and moves your DAW's"
    " delay compensation, so choose it before a take rather than during one.\" },"
    " /* @__PURE__ */ React.createElement(SpectrSettingsField,"
    " { label: hydrated.control_label || \"Latency\", hint: hint },"
    " /* @__PURE__ */ React.createElement(SpectrSettingsChips, {\n"
    "    value: active,\n"
    "    onChange: publish,\n"
    "    opts: options.map(function (option) {\n"
    "      return [option.mode, option.label];\n"
    "    })\n"
    "  })));\n"
    "}\n")

_MODAL_OLD = ("function SettingsModal({ settings, setSettings, onClose,"
              " open = true })")
_MODAL_NEW = _COMPONENT + _MODAL_OLD

_BODY_OLD = ('React.createElement("div", { "data-spectr-settings-body": true,'
             ' style: { flex: 1, minHeight: 0, overflowY: "auto",'
             ' marginRight: -20, paddingRight: 38 } },'
             ' /* @__PURE__ */ React.createElement(SpectrSettingsGroup,'
             ' { marker: "general", title: "APPEARANCE",')
_BODY_NEW = ('React.createElement("div", { "data-spectr-settings-body": true,'
             ' style: { flex: 1, minHeight: 0, overflowY: "auto",'
             ' marginRight: -20, paddingRight: 38 } },'
             ' /* @__PURE__ */ React.createElement(SpectrLatencySettings, null),'
             ' /* @__PURE__ */ React.createElement(SpectrSettingsGroup,'
             ' { marker: "general", title: "APPEARANCE",')

PATCHES = [
    ("hydrate the latency block on presence", _HYDRATE_OLD, _HYDRATE_NEW),
    ("define the Latency settings group", _MODAL_OLD, _MODAL_NEW),
    ("render Latency first in the settings body", _BODY_OLD, _BODY_NEW),
]


def main():
    with open(PATH, encoding="utf-8") as handle:
        document = json.load(handle)
    html = document["html"]

    if MARKER in html:
        print("patch_materialized_latency_mode: already applied")
        return 0

    # Every patch point must be unambiguous BEFORE anything is written.
    # Ambiguity is a failure, not a best-effort apply: a needle that matches
    # twice would put the control somewhere nobody chose.
    failures = []
    for name, old, new in PATCHES:
        count = html.count(old)
        if count != 1:
            failures.append("%s: patch point occurs %d times" % (name, count))
        if old in new.replace(old, "", 1):
            failures.append("%s: patch point survives its own replacement, so "
                            "re-running would apply it again" % name)
    if failures:
        for line in failures:
            print("patch_materialized_latency_mode: " + line, file=sys.stderr)
        return 1

    for name, old, new in PATCHES:
        html = html.replace(old, new, 1)

    # Re-check: every replacement landed, and the marker the idempotence guard
    # keys on is present. A patch that silently applied nothing would otherwise
    # write an unchanged document and report success.
    for name, old, new in PATCHES:
        if html.count(new) != 1:
            print("patch_materialized_latency_mode: %s did not apply" % name,
                  file=sys.stderr)
            return 1
    if MARKER not in html:
        print("patch_materialized_latency_mode: marker missing after patch",
              file=sys.stderr)
        return 1

    document["html"] = html
    with open(PATH, "w", encoding="utf-8") as handle:
        json.dump(document, handle, ensure_ascii=False, separators=(",", ":"))
    print("patch_materialized_latency_mode: applied %d patches" % len(PATCHES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
