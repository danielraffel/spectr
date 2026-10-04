#!/usr/bin/env python3
"""Pin the help guide's level-control copy (help-content.js).

The tester's questions were answered in words a user reads in the "?" guide;
losing one of these sentences silently un-answers a question. Exit 1 names
each missing pin. --plant drops one so the check is seen to fail.
"""
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PATH = os.path.join(REPO, "native-ui", "materialized", "help-content.js")
PINS = [
    "## Level: Mix, Intensity, Output and AUTO",
    "**Intensity** is how strong the effect is.",
    "**Mix** blends Spectr's sound with the original input. It is most useful with Freeze",
    "**Output** is the final volume",
    "**AUTO** keeps the level steady as you boost or cut.",
    "It listens to the sound going through Spectr",
    "While Freeze holds, it listens to the held sound.",
    "it remembers the sound when you stop, locate or reopen the project",
    "keeps that version's AUTO, so its level does not change, until you switch AUTO off and on again.",
    "A project saved before AUTO existed opens with it off",
    "AUTO is on by default in a new instance",
    "It keeps listening while it is off",
    "A project keeps the AUTO it saved.",
    "**Range** in Settings, under Structure, sets how far a full-height drag reaches",
    "It does not change the sound.",
    "**Display**, under Appearance",
]
text = open(PATH, encoding="utf-8").read()
if "--plant" in sys.argv:
    text = text.replace(PINS[1], "")
missing = [p for p in PINS if p not in text]
for p in missing:
    print("FAIL: help copy lost %r" % p)
print("level help pins: %d/%d present" % (len(PINS) - len(missing), len(PINS)))
sys.exit(1 if missing else 0)
