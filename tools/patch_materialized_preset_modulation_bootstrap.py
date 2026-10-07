#!/usr/bin/env python3
import json
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"
doc = json.loads(PATH.read_text())
html = doc["html"]
if "__spectrPresetModulationBootstrap" in html:
    print("already applied")
    raise SystemExit(0)

old = '  const [selectedPatternId, setSelectedPatternId] = useAppS(null);\n'
new = ('  // Seed the neighbourhood before the menu is opened, so a Preset LFO\n'
       '  // has both its name and its shape source from the first frame.\n'
       '  const [selectedPatternId, setSelectedPatternId] = useAppS("factory:flat");\n'
       '  const __spectrPresetModulationBootstrap = true;\n')
if html.count(old) != 1:
    raise SystemExit("selected preset anchor missing or ambiguous")
html = html.replace(old, new, 1)

old = ('  useAppE(() => {\n'
       '    if (selectedPatternId) spectrSendPresetNeighbourhood(selectedPatternId,\n'
       '      settings.bandCount, userPatterns);\n'
       '  }, [selectedPatternId, settings.bandCount, userPatterns]);\n')
new = old + ('  useAppE(() => {\n'
       '    // Hydration can replace factory:flat with a persisted user default.\n'
       '    if (selectedPatternId === "factory:flat" && defaultId\n'
       '        && defaultId !== "factory:flat") setSelectedPatternId(defaultId);\n'
       '  }, [defaultId, selectedPatternId]);\n')
if html.count(old) != 1:
    raise SystemExit("neighbourhood effect anchor missing or ambiguous")
html = html.replace(old, new, 1)
doc["html"] = html
PATH.write_text(json.dumps(doc, ensure_ascii=False, separators=(",", ":")))
print("applied")
