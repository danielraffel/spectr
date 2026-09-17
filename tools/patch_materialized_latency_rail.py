#!/usr/bin/env python3
"""Put the Latency control in the bottom rail, and on a key.

THE REPORT. "make it easy to see what is set in the home UX and toggle and
maybe use a shortcut command not just in settings". The mode was reachable
only by opening Settings, which meant the answer to "which mode am I in?"
cost a modal -- and it is a mode people switch between takes, so they were
paying that cost repeatedly just to read it.

WHAT THIS ADDS
  1. A chip in the bottom rail's empty gap, left of the gear, reading the
     active mode and what it costs: "MIXING . 213 ms" / "TRACKING . 1.3 ms".
     Pressing it toggles.
  2. `T` on the same App.onKey path every other bare letter uses.

WHY THE CHIP IS POSITIONED ABSOLUTELY RATHER THAN ADDED TO THE RAIL'S FLEX ROW
text_bindings / layout_bindings / paint_bindings address nodes by POSITIONAL
path, so inserting a child anywhere but LAST renumbers every later sibling and
silently re-points those bindings at the wrong nodes -- present text, wrong
box, nothing in the JS looking wrong. The rail's gap is child 14, the gear 15,
the help wrapper 16; an in-flow insertion before the gear would have moved
eight bindings (layout 75-80, paint 14-16, text 22).

So this appends as the LAST child of Chrome's fragment, which renumbers
nothing, and positions into the gap the capture already measures at
left 883 / width 343. That is not an invention: SpectrOutputMeter is mounted
exactly this way over the header's spacer, for exactly this reason. zIndex 6
is load-bearing -- the rail is 5, and without it hit_test resolves every point
in the chip to the rail and the control is visible but dead.

WHY IT DOES NOT WRITE THE MODE ITSELF
It calls the same `render_mode_set` bridge message the Settings group calls,
which reaches `Spectr::set_render_mode`, which raises the host's
latency-changed flag. A mode written any other way would move the plugin's
reported latency without telling the host its delay compensation had moved.
There is deliberately no second write path here.

The two surfaces share `globalThis.__spectrLatency`, so they cannot disagree:
the chip renders from the same store the Settings chips render from, and both
re-render off the same listener list when hydration lands.

NOTHING USER-FACING IS TYPED HERE. The labels and both millisecond figures
come from the payload, which derives them in C++ from the renderer's own
geometry -- a figure typed here would be wrong at 96 kHz and wrong again the
day a mode's geometry moves.

Exit codes: 0 applied or already applied, 1 a patch point is missing or
ambiguous. Idempotent.
"""
import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

# Presence of this marker means the document is already patched.
MARKER = "data-spectr-latency-chip"

# The rail's measured gap: layout_bindings[74] puts the {flex:1} spacer at
# left 883, width 343, and the rail itself at top 804, height 56.
#
# The chip is pinned to the RIGHT of that gap, not the left, so it groups with
# the gear rather than with the morph slider. Two reasons, both visible in the
# capture: the Latency control lived in Settings until now, so sitting one icon
# from the gear is where someone goes looking for it; and the morph slider
# carries a SET A + B TO MORPH caption below it, which the chip crowded when it
# was left-pinned. 1062 leaves 14px of clear rail before the gear at 1226.
# 819 centres a 26px chip in a 56px rail (804 + (56 - 26) / 2).
CHIP_LEFT = 1062
CHIP_TOP = 819

# A FIXED width, for the same reason the band-count chip carries one. The two
# labels differ by two characters and the milliseconds by two digits, so a
# content-sized chip would change width on every toggle. It sits in 343px of
# slack so nothing else moves either way, but a control that resizes when you
# press it reads as a glitch. 150 holds the longest string this can render
# ("TRACKING . 1.3 ms" at 17 chars) with room for a four-digit millisecond
# figure at a pathological sample rate.
CHIP_W = 150

_TOGGLE = (
    "function spectrToggleLatencyMode() {\n"
    "  // The one place the mode changes, for both the rail chip and the T\n"
    "  // key. Two entry points, one write path: whatever moves the mode has\n"
    "  // to be the thing that tells the processor, because the processor is\n"
    "  // what raises the host's latency-changed flag.\n"
    "  const store = globalThis.__spectrLatency;\n"
    "  const state = store && store.state;\n"
    "  const options = state && Array.isArray(state.options)\n"
    "    ? state.options : [];\n"
    "  if (options.length < 2) return null;\n"
    "  let at = -1;\n"
    "  for (let i = 0; i < options.length; i += 1)\n"
    "    if (options[i].mode === state.mode) at = i;\n"
    "  const next = options[(at + 1) % options.length];\n"
    "  if (!next || next.mode === state.mode) return null;\n"
    "  // Optimistic locally so the chip does not lag a round trip, then tell\n"
    "  // the processor, which owns the value and persists it. Only `mode` is\n"
    "  // written: every figure is read back out of `options` by mode, so the\n"
    "  // millisecond reading cannot drift from the label beside it.\n"
    "  store.state = Object.assign({}, state, { mode: next.mode });\n"
    "  // Wake BOTH surfaces. The Settings chips subscribe to this same list,\n"
    "  // so toggling from the rail moves the panel too -- otherwise opening\n"
    "  // Settings after a T press would show the mode it used to be in.\n"
    "  (store.listeners || []).forEach(function (fn) {\n"
    "    try { fn(); } catch (error) {\n"
    "      console.error(\"[Spectr] latency listener failed\", error);\n"
    "    }\n"
    "  });\n"
    "  // The stable token travels, never an index: an index would make the\n"
    "  // option order part of the wire contract.\n"
    "  if (window.pulp && window.pulp.postMessage)\n"
    "    Promise.resolve(window.pulp.postMessage(\"render_mode_set\","
    " { mode: next.mode }, \"spectr-render-mode\"))\n"
    "      .then(function (response) {\n"
    "        // The PROCESSOR's answer, which is the only reading that\n"
    "        // proves the switch happened. The optimistic write above\n"
    "        // would look identical if the message never arrived, so a\n"
    "        // gate that reads the chip text alone cannot tell a real\n"
    "        // switch from a dead one. This can only be true if\n"
    "        // Spectr::set_render_mode ran and rebuilt the renderer.\n"
    "        const body = response && response.payload;\n"
    "        const confirmed = body && body.latency && body.latency.mode;\n"
    "        if (!confirmed) return;\n"
    "        store.confirmed = confirmed;\n"
    "        console.log(\"[Spectr] latency mode confirmed \" + confirmed\n"
    "          + \" latency_samples=\" + body.latency.samples);\n"
    "      })\n"
    "      .catch(function (error) {\n"
    "        console.error(\"[Spectr] latency mode write failed\", error);\n"
    "      });\n"
    "  return next;\n"
    "}\n"
    "function SpectrLatencyRail() {\n"
    "  const store = globalThis.__spectrLatency\n"
    "    || (globalThis.__spectrLatency = {});\n"
    "  const [flash, setFlash] = React.useState(false);\n"
    "  const [, setRevision] = React.useState(0);\n"
    "  // Re-render when hydration lands. The editor mounts before the\n"
    "  // processor answers, so reading the global once at mount is what made\n"
    "  // the first version of the Settings control permanently invisible.\n"
    "  React.useEffect(function () {\n"
    "    const listeners = store.listeners || (store.listeners = []);\n"
    "    const onHydrate = function () {\n"
    "      setRevision(function (n) { return n + 1; });\n"
    "    };\n"
    "    listeners.push(onHydrate);\n"
    "    return function () {\n"
    "      const at = listeners.indexOf(onHydrate);\n"
    "      if (at >= 0) listeners.splice(at, 1);\n"
    "    };\n"
    "  }, []);\n"
    "  const hydrated = store.state;\n"
    "  // Render nothing rather than an empty chip when the payload has not\n"
    "  // arrived. Every hook above runs first so the order is stable across\n"
    "  // the hydrated/unhydrated transition.\n"
    "  if (!hydrated || !Array.isArray(hydrated.options)\n"
    "      || hydrated.options.length === 0) return null;\n"
    "  const matches = hydrated.options.filter(function (option) {\n"
    "    return option.mode === hydrated.mode;\n"
    "  });\n"
    "  const current = matches.length ? matches[0] : hydrated.options[0];\n"
    "  // Same formatting the Settings hint uses, so one control cannot read\n"
    "  // 1.3 ms while the other reads 1 ms.\n"
    "  const millis = (typeof current.ms === 'number')\n"
    "    ? (current.ms < 10 ? current.ms.toFixed(1)"
    " : String(Math.round(current.ms))) + \" ms\"\n"
    "    : \"\";\n"
    "  const label = String(current.label || \"\").toUpperCase();\n"
    "  return /* @__PURE__ */ React.createElement(\"button\", {\n"
    "    \"data-spectr-latency-chip\": true,\n"
    "    \"data-spectr-latency-mode\": current.mode,\n"
    "    \"data-spectr-rail-action\": \"latency\",\n"
    "    title: \"Latency \\u00B7 \" + (current.label || \"\")"
    " + \" \\u00B7 press T to switch\",\n"
    "    onClick: function () {\n"
    "      setFlash(true);\n"
    "      setTimeout(function () { setFlash(false); }, 180);\n"
    "      spectrToggleLatencyMode();\n"
    "    },\n"
    "    style: {\n"
    "      position: \"absolute\", left: __LEFT__, top: __TOP__, zIndex: 6,\n"
    "      width: __W__, minWidth: __W__, boxSizing: \"border-box\",\n"
    "      background: flash ? \"rgba(180,220,255,0.22)\""
    " : \"rgba(255,255,255,0.03)\",\n"
    "      border: \"1px solid \" + (flash ? \"rgba(200,230,255,0.6)\""
    " : \"rgba(255,255,255,0.08)\"),\n"
    "      color: \"rgba(255,255,255,0.85)\",\n"
    "      padding: \"5px 10px\",\n"
    "      fontFamily: \"var(--mono)\", fontSize: 10, letterSpacing: 1,\n"
    "      borderRadius: 3, cursor: \"pointer\", height: 26,\n"
    "      display: \"inline-flex\", alignItems: \"center\","
    " justifyContent: \"center\",\n"
    "      lineHeight: 1, whiteSpace: \"nowrap\", pointerEvents: \"auto\",\n"
    "      transition: \"background 0.15s, border-color 0.15s\"\n"
    "    }\n"
    "  }, /* @__PURE__ */ React.createElement(\"span\","
    " { className: \"tnum\", style: { lineHeight: 1 } },\n"
    "    millis ? (label + \" \\u00B7 \" + millis) : label));\n"
    "}\n")
# Substituted rather than %-formatted: the toggle body contains a modulo, and
# a stray %-format over JS source is the kind of edit that silently produces
# valid-looking nonsense.
_TOGGLE = (_TOGGLE.replace("__LEFT__", str(CHIP_LEFT))
                  .replace("__TOP__", str(CHIP_TOP))
                  .replace("__W__", str(CHIP_W)))

_CHROME_OLD = "function Chrome({ settings, setSettings, bankRef, status,"
_CHROME_NEW = _TOGGLE + _CHROME_OLD

# Mounted as the LAST child of Chrome's fragment, immediately after
# SpectrOutputMeter -- which is itself unbound and appended last for the same
# reason. Nothing addresses a child at this index, so nothing renumbers.
_MOUNT_OLD = "React.createElement(SpectrOutputMeter, null));"
_MOUNT_NEW = ("React.createElement(SpectrOutputMeter, null),"
              " /* @__PURE__ */ React.createElement(SpectrLatencyRail, null));")

# The key. `t` is free: the App handler binds s/l/b/f/g (edit modes), 1-6,
# `m` (mute selection) and `a` (analyzer), and nothing else. T for Tracking --
# the mode you reach for urgently, because it is the one you need before you
# can play through the plugin at all.
#
# No exemption is added to overlayBlocksShortcut(). The chip is not an overlay,
# and when Settings IS open the guard blocking bare letters is correct: the
# panel's own Latency chips are right there.
_KEY_OLD = (
    "        else fireStatus(result.count + \" BAND\""
    " + (result.count === 1 ? \"\" : \"S\")\n"
    "          + (result.muted ? \" MUTED\" : \" UNMUTED\"));\n"
    "        return;\n"
    "      }\n")
_KEY_NEW = (
    "        else fireStatus(result.count + \" BAND\""
    " + (result.count === 1 ? \"\" : \"S\")\n"
    "          + (result.muted ? \" MUTED\" : \" UNMUTED\"));\n"
    "        return;\n"
    "      }\n"
    "      // Latency mode, on the key the SHORTCUTS panel advertises. The\n"
    "      // guard above already rejected every modifier, a focused text\n"
    "      // field and any open overlay, so this needs no guard of its own.\n"
    "      //\n"
    "      // It routes through the same toggle the rail chip presses rather\n"
    "      // than writing the mode here, because that toggle is what reaches\n"
    "      // the processor -- and the processor is what tells the host its\n"
    "      // delay compensation has moved.\n"
    "      if (k === \"t\") {\n"
    "        e.preventDefault();\n"
    "        closeBandMenu();\n"
    "        const nextMode = spectrToggleLatencyMode();\n"
    "        // A key that silently does nothing is indistinguishable from one\n"
    "        // that is broken, so an un-hydrated payload is reported.\n"
    "        if (!nextMode) fireStatus(\"LATENCY UNAVAILABLE\");\n"
    "        else fireStatus(\"LATENCY \\u2192 \""
    " + String(nextMode.label || \"\").toUpperCase());\n"
    "        return;\n"
    "      }\n")

# Settings KEEPS the control. The chip is for reading and switching at a
# glance; the panel is for DECIDING, and it carries what a chip cannot -- both
# options side by side with their costs, and a line of guidance per mode. They
# read and write one store, so they cannot disagree. What the panel was missing
# is any hint that the faster path now exists, which is the one way the two
# surfaces could have contradicted each other: a user who only ever found the
# modal would go on paying for it.
_SUBTITLE_OLD = ("subtitle: \"Changing this rebuilds the processor and moves"
                 " your DAW's delay compensation, so choose it before a take"
                 " rather than during one.\" }")
_SUBTITLE_NEW = ("subtitle: \"Changing this rebuilds the processor and moves"
                 " your DAW's delay compensation, so choose it before a take"
                 " rather than during one. The chip at the bottom right shows"
                 " the current mode and switches it in one click.\" }")

PATCHES = [
    ("define the rail chip and its toggle", _CHROME_OLD, _CHROME_NEW),
    ("point the Settings group at the rail chip", _SUBTITLE_OLD, _SUBTITLE_NEW),
    ("mount the rail chip last in Chrome", _MOUNT_OLD, _MOUNT_NEW),
    ("bind T to the latency toggle", _KEY_OLD, _KEY_NEW),
]


def main():
    with open(PATH, encoding="utf-8") as handle:
        document = json.load(handle)
    html = document["html"]

    if MARKER in html:
        print("patch_materialized_latency_rail: already applied")
        return 0

    # Every patch point must be unambiguous BEFORE anything is written.
    # Ambiguity is a failure, not a best-effort apply.
    failures = []
    for name, old, new in PATCHES:
        count = html.count(old)
        if count != 1:
            failures.append("%s: patch point occurs %d times, expected 1"
                            % (name, count))
        if old in new.replace(old, "", 1):
            failures.append("%s: patch point survives its own replacement, so "
                            "re-running would apply it again" % name)
    if failures:
        for line in failures:
            print("patch_materialized_latency_rail: " + line, file=sys.stderr)
        return 1

    for name, old, new in PATCHES:
        html = html.replace(old, new, 1)

    # Re-check: a patch that silently applied nothing would otherwise write an
    # unchanged document and report success.
    for name, old, new in PATCHES:
        if html.count(new) != 1:
            print("patch_materialized_latency_rail: %s did not apply" % name,
                  file=sys.stderr)
            return 1
    if MARKER not in html:
        print("patch_materialized_latency_rail: marker missing after patch",
              file=sys.stderr)
        return 1

    document["html"] = html
    with open(PATH, "w", encoding="utf-8") as handle:
        json.dump(document, handle, ensure_ascii=False, separators=(",", ":"))
    print("patch_materialized_latency_rail: applied %d patches" % len(PATCHES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
