#!/usr/bin/env python3
"""Mirror the resources/editor.html source patches into the shipping document.

INTERIM. native-ui/materialized/materialized-document.runtime.json is a
generated artifact and no recipe in this repo reproduces the checked-in pair
(danielraffel/spectr#48), so editor-behaviour fixes have to be applied to it by
hand. Every edit below is a mechanical, one-for-one image of a
replaceSpectrSource entry in resources/editor.html -- the source stays the
durable record, and a future re-materialization should make this script a no-op.

DELIBERATE DIVERGENCE, and the only one: resources/editor.html carries a
"REDRAW UNMUTES" toggle in the overflow menu that is NOT mirrored here. The
native materialized runtime paints any child ADDED to a container at that
container's FIRST child position, on top of what is already there. Reproduced
by screenshot in three unrelated containers -- the settings STRUCTURE group,
the settings MOTION group, and the overflow menu -- so this is a property of
the runtime, not of one placement. Mirroring the toggle would ship an
unreadable overlap. The setting itself IS mirrored and works; in the shipping
native editor it is reachable only at its default (on). Re-materializing the
document is what surfaces the control, and at that point this note goes away.

Idempotent: re-running it after a successful pass reports "already applied".
"""
import json
import os
import shutil
import sys

PATH = 'native-ui/materialized/materialized-document.runtime.json'

EDITS = [
    ('animation loop paints the latest canvas renderer',
     '      renderAll();\n'
     '      rafRef.current = requestAnimationFrame(draw);\n'
     '    };',
     '      (renderAllRef.current || renderAll)();\n'
     '      rafRef.current = requestAnimationFrame(draw);\n'
     '    };'),

    ('hover readout clears the status banner slot',
     '    const ty = clamp(y - 30, g.inner.y + 2, g.inner.y + g.inner.h);',
     '    const ty = clamp(y - 30, g.inner.y + 20, g.inner.y + g.inner.h);'),

    ('status banner replaces one message at a time',
     '  const [text, setText] = useStateChrome("");\n'
     '  useEffectChrome(() => {\n'
     '    if (!message) {\n'
     '      setVisible(false);\n'
     '      setText("");\n'
     '      return;\n'
     '    }\n'
     '    const display = message.split("|")[0].trim();\n'
     '    if (!display) {\n'
     '      setVisible(false);\n'
     '      setText("");\n'
     '      return;\n'
     '    }\n'
     '    setText(display);\n'
     '    setVisible(true);\n'
     '    const t = setTimeout(() => {\n'
     '      setVisible(false);\n'
     '      setText("");\n'
     '    }, 1400);\n'
     '    return () => clearTimeout(t);\n'
     '  }, [message]);\n',

     '  const [text, setText] = useStateChrome("");\n'
     '  const shownRef = useRefChrome("");\n'
     '  useEffectChrome(() => {\n'
     '    const display = message ? message.split("|")[0].trim() : "";\n'
     '    if (!display) {\n'
     '      setVisible(false);\n'
     '      setText("");\n'
     '      shownRef.current = "";\n'
     '      return;\n'
     '    }\n'
     '    const replacing = !!shownRef.current && shownRef.current !== display;\n'
     '    const timers = [];\n'
     '    if (replacing) {\n'
     '      setVisible(false);\n'
     '      timers.push(setTimeout(() => {\n'
     '        setText(display);\n'
     '        setVisible(true);\n'
     '      }, 150));\n'
     '    } else {\n'
     '      setText(display);\n'
     '      setVisible(true);\n'
     '    }\n'
     '    shownRef.current = display;\n'
     '    timers.push(setTimeout(() => {\n'
     '      setVisible(false);\n'
     '      setText("");\n'
     '      shownRef.current = "";\n'
     '    }, replacing ? 1550 : 1400));\n'
     '    return () => timers.forEach(clearTimeout);\n'
     '  }, [message]);\n'),

    ('status banner chrome survives the fade-out',
     '        background: visible && text ? "rgba(12,16,22,0.92)" : "transparent",\n'
     '        border: "1px solid " + (visible && text ? "rgba(180,210,255,0.3)" : "transparent"),\n'
     '        borderRadius: 3,\n'
     '        fontFamily: "var(--mono)",\n'
     '        fontSize: 10,\n'
     '        letterSpacing: 1.5,\n'
     '        color: visible && text ? "rgba(200,220,255,0.95)" : "transparent",\n'
     '        zIndex: 6,\n'
     '        pointerEvents: "none",\n'
     '        opacity: visible && text ? 1 : 0,\n'
     '        transition: "opacity 0.2s",\n'
     '        backdropFilter: visible && text ? "blur(8px)" : "none"\n'
     '      }\n'
     '    },\n'
     '    visible && text ? text : ""\n'
     '  );',

     '        background: text ? "rgba(12,16,22,0.92)" : "transparent",\n'
     '        border: "1px solid " + (text ? "rgba(180,210,255,0.3)" : "transparent"),\n'
     '        borderRadius: 3,\n'
     '        fontFamily: "var(--mono)",\n'
     '        fontSize: 10,\n'
     '        letterSpacing: 1.5,\n'
     '        color: text ? "rgba(200,220,255,0.95)" : "transparent",\n'
     '        zIndex: 6,\n'
     '        pointerEvents: "none",\n'
     '        opacity: visible && text ? 1 : 0,\n'
     '        transition: "opacity 0.15s ease",\n'
     '        backdropFilter: text ? "blur(8px)" : "none"\n'
     '      }\n'
     '    },\n'
     '    text\n'
     '  );'),

    # ----------------------------- task 4: mute consistency across edit modes
    ('redraw-unmutes setting is read by the bank',
     '  const { bandCount, metaphor, bloom, spectrumIntensity, muteStyle, '
     'motionMode, showMinimap, showRulers, theme } = settings;',
     '  const { bandCount, metaphor, bloom, spectrumIntensity, muteStyle, '
     'motionMode, showMinimap, showRulers, theme, unmuteOnDraw } = settings;'),

    ('redraw-unmutes pointer-stable ref',
     '  const N = bandCount;\n'
     '  const editModeRef = useRef(editMode || "sculpt");\n',
     '  const N = bandCount;\n'
     '  const unmuteOnDrawRef = useRef(unmuteOnDraw !== false);\n'
     '  useEffect(() => {\n'
     '    unmuteOnDrawRef.current = unmuteOnDraw !== false;\n'
     '  }, [unmuteOnDraw]);\n'
     '  const editModeRef = useRef(editMode || "sculpt");\n'),

    ('drawn gain edits share one mute decision',
     '    queueNativeProcessingStatePublication();\n'
     '  };\n'
     '  const minimapHit = (x, y, g) => {',
     '    queueNativeProcessingStatePublication();\n'
     '  };\n'
     '  const commitDrawnGains = (map) => {\n'
     '    if (unmuteOnDrawRef.current) {\n'
     '      commitMany(map);\n'
     '      return;\n'
     '    }\n'
     '    const held = /* @__PURE__ */ new Map();\n'
     '    for (const [index, value] of map)\n'
     '      held.set(index, isMuted(targetGainsRef.current[index]) ? -Infinity : value);\n'
     '    commitMany(held);\n'
     '  };\n'
     '  const editBaseGain = (value, index) => {\n'
     '    if (!isMuted(value)) return value;\n'
     '    const db = mutedGainDbRef.current[index];\n'
     '    return Number.isFinite(db) ? clamp(db / 24, -1, 1) : 0;\n'
     '  };\n'
     '  const minimapHit = (x, y, g) => {'),

    ('group drag defers the mute decision',
     '        for (const [i, v0] of p.groupStart.entries()) {\n'
     '          if (isMuted(v0)) {\n'
     '            map.set(i, -Infinity);\n'
     '            continue;\n'
     '          }\n'
     '          map.set(i, clamp(v0 + delta, -1, 1));\n'
     '        }\n'
     '        commitMany(map);',
     '        for (const [i, v0] of p.groupStart.entries())\n'
     '          map.set(i, clamp(editBaseGain(v0, i) + delta, -1, 1));\n'
     '        commitDrawnGains(map);'),

    ('sculpt defers the mute decision',
     '        map.set(curBand, newG);\n'
     '        commitMany(map);',
     '        map.set(curBand, newG);\n'
     '        commitDrawnGains(map);'),

    ('level defers the mute decision',
     '        for (const b of p.paintedBands) map.set(b, newG);\n'
     '        commitMany(map);',
     '        for (const b of p.paintedBands) map.set(b, newG);\n'
     '        commitDrawnGains(map);'),

    ('boost defers the mute decision',
     '          const v0 = p.startSnap[b];\n'
     '          if (isMuted(v0)) {\n'
     '            map.set(b, -Infinity);\n'
     '            continue;\n'
     '          }\n'
     '          map.set(b, clamp(v0 * k, -1, 1));\n'
     '        }\n'
     '        commitMany(map);',
     '          const v0 = editBaseGain(p.startSnap[b], b);\n'
     '          map.set(b, clamp(v0 * k, -1, 1));\n'
     '        }\n'
     '        commitDrawnGains(map);'),

    ('flare defers the mute decision',
     '          const v0 = p.startSnap[b];\n'
     '          if (isMuted(v0)) {\n'
     '            map.set(b, -Infinity);\n'
     '            continue;\n'
     '          }\n'
     '          const sign = v0 >= 0 ? 1 : -1;',
     '          const v0 = editBaseGain(p.startSnap[b], b);\n'
     '          const sign = v0 >= 0 ? 1 : -1;'),

    ('flare commits through the shared path',
     '          map.set(b, clamp(out, -1, 1));\n'
     '        }\n'
     '        commitMany(map);',
     '          map.set(b, clamp(out, -1, 1));\n'
     '        }\n'
     '        commitDrawnGains(map);'),

    ('glide defers the mute decision',
     '          const v0 = p.startSnap[i];\n'
     '          if (isMuted(v0)) {\n'
     '            map.set(i, -Infinity);\n'
     '            continue;\n'
     '          }\n'
     '          map.set(i, clamp(v0 + (newG - v0) * w, -1, 1));\n'
     '        }\n'
     '        commitMany(map);',
     '          const v0 = editBaseGain(p.startSnap[i], i);\n'
     '          map.set(i, clamp(v0 + (newG - v0) * w, -1, 1));\n'
     '        }\n'
     '        commitDrawnGains(map);'),

]


def escaped(value):
    return json.dumps(value)[1:-1]


def check_emitted_scripts(html):
    """Parse every emitted script block.

    A replacement that is one parenthesis out still substitutes cleanly and
    still passes a substring post-check, but QuickJS then rejects the whole
    document and the native editor quietly falls back to the generic auto-UI
    panel. Node is the same parser the browser oracle uses.
    """
    import re
    import subprocess
    import tempfile

    node = shutil.which('node')
    if not node:
        print('warn: node not found; skipped the emitted-script parse check')
        return
    blocks = re.findall(
        r'<script(?: type="text/javascript")?>(.*?)</script>', html, re.S)
    if not blocks:
        sys.exit('FAIL: no script blocks found in the emitted document')
    for index, block in enumerate(blocks):
        with tempfile.NamedTemporaryFile('w', suffix='.js', delete=False) as handle:
            handle.write(block)
            path = handle.name
        result = subprocess.run([node, '--check', path],
                                capture_output=True, text=True)
        os.unlink(path)
        if result.returncode != 0:
            sys.exit(f'FAIL: emitted script block {index} does not parse\n'
                     + result.stderr)
    print(f'parsed {len(blocks)} emitted script blocks')


def main():
    # An edit whose replacement still contains its own patch point would apply
    # again on every run, silently stacking duplicates. Make every edit
    # self-consuming, and refuse to run if one is not.
    for label, old, new in EDITS:
        if old in new:
            sys.exit(f'FAIL {label}: patch point survives its own replacement')

    raw = open(PATH, encoding='utf-8').read()
    changed = False
    for label, old, new in EDITS:
        old_e, new_e = escaped(old), escaped(new)
        if raw.count(new_e) == 1 and raw.count(old_e) == 0:
            print('already applied ', label)
            continue
        count = raw.count(old_e)
        if count != 1:
            sys.exit(f'FAIL {label}: patch point occurs {count} times')
        raw = raw.replace(old_e, new_e)
        changed = True
        print('applied         ', label)

    document = json.loads(raw)
    html = document['html']
    for label, _old, new in EDITS:
        if new not in html:
            sys.exit(f'FAIL {label}: post-check did not find the replacement')
    check_emitted_scripts(html)
    if changed:
        open(PATH, 'w', encoding='utf-8').write(raw)
        print('written', PATH)
    else:
        print('no change needed')


if __name__ == '__main__':
    main()
