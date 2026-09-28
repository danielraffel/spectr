#!/usr/bin/env python3
"""A plain pointer move over the bands costs almost nothing.

Perfetto plus a per-event probe on a standalone with LFO 1 running: every
`pointermove` on the filter surface cost ~42 ms (533 events, 8.7 s of UI
thread in a 15 s mouse sweep) while `mousemove` on the same element cost
0.2 ms -- so the React move handler, not event delivery, was the cost. In this
captured-import runtime any React update re-applies the whole document, and a
hover move made two:

  * `setCursor("crosshair")` on every move. React skips an update whose value
    is unchanged only in some cases; here each call still re-applied the
    document. The setter now does nothing when the cursor is already that
    value (the style is also written directly, for immediate feedback).
  * The handler is registered as both `onPointerMoveCapture` and
    `onPointerMove`, so it ran twice per move. The second delivery of the
    same event now returns at once.

Requires patch_materialized_hover_no_rerender.py.
Raw-text surgery on the escaped document. Idempotent.
Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"
MARKER = "const cursorValueRef = "

EDITS = [
    (
        "cursor setter skips unchanged values",
        '''  const [cursor, setCursor] = useState('crosshair');''',
        '''  const [cursor, setCursorState] = useState('crosshair');
  // Only a changed cursor is a React update: each update re-applies the whole
  // captured document, and a hover move asked for "crosshair" every time.
  // See tools/patch_materialized_pointer_move_cost.py.
  const cursorValueRef = useRef('crosshair');
  const setCursor = (value) => {
    if (cursorValueRef.current === value) return;
    cursorValueRef.current = value;
    setCursorState(value);
  };'''),
    (
        "one move event runs the handler once",
        '''      onPointerMoveCapture: onPointerMove,
      onPointerMove,''',
        '''      // Registered for both phases; the event is handled once.
      onPointerMoveCapture: (e) => {
        const key = `${e.timeStamp}|${e.clientX}|${e.clientY}`;
        if (lastMoveKeyRef.current === key) return;
        lastMoveKeyRef.current = key;
        onPointerMove(e);
      },
      onPointerMove: (e) => {
        const key = `${e.timeStamp}|${e.clientX}|${e.clientY}`;
        if (lastMoveKeyRef.current === key) return;
        lastMoveKeyRef.current = key;
        onPointerMove(e);
      },'''),
    (
        "move de-duplication key",
        '''  const cursorValueRef = useRef('crosshair');''',
        '''  const cursorValueRef = useRef('crosshair');
  const lastMoveKeyRef = useRef("");'''),
]


def encode(text):
    return json.dumps(text, ensure_ascii=False)[1:-1]


def main():
    raw = PATH.read_text(encoding="utf-8")
    if encode(MARKER) in raw:
        print("pointer move cost fix already applied")
        return 0
    for name, old, new in EDITS:
        count = raw.count(encode(old))
        if count != 1:
            sys.exit("FAIL: %s anchor occurs %d times, expected 1" % (name, count))
        raw = raw.replace(encode(old), encode(new), 1)
    json.loads(raw)
    PATH.write_text(raw, encoding="utf-8")
    print("pointer move cost fix applied")
    return 0


if __name__ == "__main__":
    sys.exit(main())
