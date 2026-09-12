#!/usr/bin/env python3
"""Teach the vendored materialized runtime the `hitSlop` style property.

WHY THIS EXISTS

    `native-ui/materialized/runtime.js` is a CHECKED-IN copy of Pulp's
    materialized style applier, and it is the thing that turns a React style
    object into bridge calls for the shipping editor.  It carries a case arm
    per supported property:

        case "pointerEvents":
          call("setPointerEvents", id, value);
          return true;

    It has no arm for `hitSlop`, because the copy predates the SDK's.  The
    pinned SDK (v0.847.0) HAS the capability on both sides of the seam --
    `setHitSlop` is a registered bridge function and `View::hit_bounds()` is
    `local_bounds()` grown by it -- so the only thing missing is the one arm
    that connects a style write to the call.

    Measured, on the shipping native editor, before this patch:

        el.style.hitSlop = '6 3'   readback='6 3'   hit rect UNCHANGED 35x26
        globalThis.setHitSlop(el.id, 6, 3, 6, 3)    hit rect 41x38

    Two writes, same control, same frame budget.  The second is the bridge
    function doing exactly its job; the first is the style write being silently
    dropped on the floor by the applier.  That is the whole defect -- and it is
    the silent kind: `style.hitSlop` reads back fine, so nothing anywhere says
    the value never left JavaScript.

    `tools/patch_materialized_hit_targets.py` writes `hitSlop` into three
    control styles in the materialized document.  Without this arm every one of
    those writes is a no-op, so the two scripts are one change in two
    artifacts and both must be applied.

WHY AN ARM AND NOT A RE-COPY

    Re-copying runtime.js from the SDK would be a 1.6MB diff carrying every
    unrelated change between the vendored revision and today, on a file that is
    the shipping editor's entire behaviour.  A single case arm, written in this
    file's own dialect, is reviewable.  `call()` no-ops when the named bridge
    function is absent, so the arm also degrades safely against an older SDK
    rather than throwing.

    The parsing follows React Native's `hitSlop` and matches the SDK's own
    implementation in core/view/js/web-compat-style-decl-misc.js: a number, an
    RN `{top,right,bottom,left}` object, or a CSS-shorthand string of 1-4
    numbers with `margin`'s fill rules.

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 the patch point is missing/ambiguous.
"""

import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized", "runtime.js")

OLD = '''      case "pointerEvents":
        call("setPointerEvents", id, value);
        return true;
'''

NEW = '''      case "pointerEvents":
        call("setPointerEvents", id, value);
        return true;
      // RN `hitSlop` grows ONLY the area hit_test() accepts -- never the
      // painted box, never the Yoga layout -- so a visually small control can
      // present a comfortable pointer target without the design moving.
      // Accepts a number, an RN {top,right,bottom,left} object, or a
      // CSS-shorthand string of 1-4 numbers with margin's fill rules.
      case "hitSlop": {
        const _hs = value;
        let _t = 0, _r = 0, _b = 0, _l = 0;
        if (_hs != null && typeof _hs === "object") {
          _t = parseFloat(_hs.top) || 0;
          _r = parseFloat(_hs.right) || 0;
          _b = parseFloat(_hs.bottom) || 0;
          _l = parseFloat(_hs.left) || 0;
        } else {
          const _p = String(_hs == null ? "" : _hs).trim().split(/\\s+/)
            .map(function (n) { return parseFloat(n) || 0; });
          _t = _p.length > 0 ? _p[0] : 0;
          _r = _p.length > 1 ? _p[1] : _t;
          _b = _p.length > 2 ? _p[2] : _t;
          _l = _p.length > 3 ? _p[3] : _r;
        }
        call("setHitSlop", id, _t, _r, _b, _l);
        return true;
      }
'''

MARKER = 'call("setHitSlop", id, _t, _r, _b, _l);'


def main():
    raw = open(PATH, encoding="utf-8").read()
    if raw.count(MARKER) >= 1:
        print("already applied  hitSlop style arm")
        print("no change needed")
        return 0
    count = raw.count(OLD)
    if count != 1:
        sys.exit("FAIL: patch point occurs %d times, expected 1" % count)
    raw = raw.replace(OLD, NEW, 1)
    if raw.count(MARKER) != 1:
        sys.exit("FAIL: the marker is not present exactly once after patching")
    if raw.count('case "pointerEvents":') != 1:
        sys.exit("FAIL: the anchor arm was duplicated")
    open(PATH, "w", encoding="utf-8").write(raw)
    print("applied          hitSlop style arm")
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
