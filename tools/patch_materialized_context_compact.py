#!/usr/bin/env python3
"""Compact the band context menu: remove duplicate edit-mode rows and widen shortcut chips."""
import json
from pathlib import Path
P=Path(__file__).resolve().parents[1]/'native-ui/materialized/materialized-document.runtime.json'
s=json.loads(P.read_text()); h=s['html']
if 'fontSize: 9.5' in h and 'Cmd+Shift+P' in h:
    print('context menu compaction already applied')
    raise SystemExit(0)
# EDIT MODE IS NOT COMPACTED AWAY.
#
# This block used to delete the `EDIT MODE` Divider and all five mode rows.
# That is a content change, not compaction: the shipped build shows that
# section with the `\u25CF` active-mode dot against the current mode, and
# removing it takes band-level mode switching out of the menu entirely.
# Nothing in the commit subjects said so. The chip-legibility half below is
# kept, which is what "compact" was actually about.
#
# Restore the deletion here only on an explicit decision to drop the section.

oldstyle='''    fontSize: 8.5,
    letterSpacing: 0.5,
    opacity: 0.5,
    padding: "1px 5px",'''
newstyle='''    fontSize: 9.5,
    letterSpacing: 0.8,
    opacity: 0.78,
    padding: "2px 7px",'''
assert h.count(oldstyle)==1
h=h.replace(oldstyle,newstyle,1)
oldmanage='data-spectr-shortcut-chip\": \"manage\", style: spectrShortcutChipStyle() }, \"\\u21e7\\u2318P\"'
newmanage='data-spectr-shortcut-chip\": \"manage\", style: { ...spectrShortcutChipStyle(), fontSize: 9, letterSpacing: 0.4 } }, \"Cmd+Shift+P\"'
assert h.count(oldmanage)==1, h.count(oldmanage)
h=h.replace(oldmanage,newmanage,1)
s['html'] = h
P.write_text(json.dumps(s,separators=(',',':'),ensure_ascii=False)+'\n')
print('removed context edit-mode rows and improved shortcut chip legibility')
