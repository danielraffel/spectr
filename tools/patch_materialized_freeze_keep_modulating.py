#!/usr/bin/env python3
"""KEEP MODULATING on a modulated Freeze keeps the LFO in charge of Freeze.

THE BUG

    With an LFO on Freeze, a press of LIVE / FROZEN (button, Q, the chord, the
    context menu) asks "Freeze is being modulated by LFO 1. Turn off its Freeze
    target?". KEEP MODULATING then still applied the press, and the processor
    holds a press until the gate's next change. A gate that changes slowly --
    or, with Hold for Length, a hold of several bars -- left Freeze pinned to
    the press for that long, which reads exactly as "Keep modulating stopped
    the modulation".

THE RULE

    The question offers two answers to one conflict: the user's press against
    the LFO. KEEP MODULATING keeps the LFO: the press is dropped, the target
    stays on, and LIVE / FROZEN keeps following the LFO. TURN OFF switches the
    target off and then applies the press. A Freeze press is the one action
    that cannot coexist with its modulation -- it IS an override of the gate --
    so it is the one that opts out of being applied on Keep
    (`keepApplies: false`). LENGTH, BANDS, PRESETS and the level knobs keep
    applying on Keep: their action becomes the centre the LFO moves around, so
    the modulation carries on. With "Ask before overriding modulation" off
    nothing asks, and a press applies until the gate's next change, as before.

Runs after tools/patch_materialized_modulation_freeze_override.py and
tools/patch_materialized_modulation_level_targets.py, which wrote the text it
edits. Raw-text surgery on the escaped document. Idempotent: each edit is
skipped when its replacement is already present.
Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"

EDITS = [
    (
        "a request carries whether Keep applies its action",
        '''function spectrOverrideModulated(control, target, lfos, action) {
  if (!lfos || !lfos.length || !spectrOverrideAsks()) { action(); return; }
  spectrAskOverride({ control, target, lfos: lfos.slice(), action });
}''',
        '''// `options.keepApplies === false`: KEEP MODULATING drops the action (it
// would override the LFO itself, as a Freeze press does); TURN OFF still
// applies it. See tools/patch_materialized_freeze_keep_modulating.py.
function spectrOverrideModulated(control, target, lfos, action, options) {
  if (!lfos || !lfos.length || !spectrOverrideAsks()) { action(); return; }
  spectrAskOverride({ control, target, lfos: lfos.slice(), action,
    keepApplies: !(options && options.keepApplies === false) });
}''',
    ),
    (
        "Keep modulating leaves a Freeze press unapplied",
        '''    }
    r.action();
  }, [dontAsk]);''',
        '''    }
    if (turnOff || r.keepApplies !== false) r.action();
  }, [dontAsk]);''',
    ),
    (
        "a Freeze press is not applied on Keep",
        '''    spectrSetFrozen(next);
  });
  return next;''',
        '''    spectrSetFrozen(next);
  }, { keepApplies: false });
  return next;''',
    ),
]


def encode(text):
    return json.dumps(text, ensure_ascii=False)[1:-1]


def main():
    raw = PATH.read_text(encoding="utf-8")
    applied = 0
    for name, old, new in EDITS:
        if encode(new) in raw:
            continue
        count = raw.count(encode(old))
        if count != 1:
            sys.exit("FAIL: %s anchor occurs %d times, expected 1" % (name, count))
        raw = raw.replace(encode(old), encode(new), 1)
        applied += 1
    if applied == 0:
        print("freeze keep-modulating already applied")
        return 0
    json.loads(raw)
    PATH.write_text(raw, encoding="utf-8")
    print("freeze keep-modulating applied (%d edits)" % applied)
    return 0


if __name__ == "__main__":
    sys.exit(main())
