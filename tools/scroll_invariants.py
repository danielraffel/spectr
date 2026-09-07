#!/usr/bin/env python3
"""Scroll invariants for a fixed-header, scrolling-body panel.

Two captures of the same panel, one scrolled, answer a question no single
screenshot can: does the body actually move, and does the header stay put while
it does? Both halves matter and they fail in opposite directions —

  A body that does not move is a body with nothing in it. That is how the
  Settings panel failed: the header painted, the close control painted, and the
  content area was empty, so scrolling it was a no-op and every whole-image
  statistic still looked healthy.

  A header that moves with the body is a header that was never fixed.

So the invariant is a CONJUNCTION, and reporting either half alone would pass
the broken panel.
"""

from __future__ import annotations

import argparse
import sys

try:
    from PIL import Image, ImageChops
except ImportError:  # pragma: no cover
    sys.exit("Pillow is required: python3 -m pip install pillow")


def region_changed(a: Image.Image, b: Image.Image, box: tuple[int, int, int, int]) -> bool:
    return ImageChops.difference(a.crop(box), b.crop(box)).getbbox() is not None


def parse_box(text: str) -> tuple[int, int, int, int]:
    parts = [int(p) for p in text.split(",")]
    if len(parts) != 4:
        raise argparse.ArgumentTypeError("expected x0,y0,x1,y1")
    return tuple(parts)  # type: ignore[return-value]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("unscrolled")
    ap.add_argument("scrolled")
    ap.add_argument("--header", type=parse_box, required=True,
                    help="x0,y0,x1,y1 of the region that must NOT move")
    ap.add_argument("--body", type=parse_box, required=True,
                    help="x0,y0,x1,y1 of the region that MUST move")
    ap.add_argument("--plant", choices=["header-moves", "body-frozen"],
                    help="force a failure to prove the check can fail")
    args = ap.parse_args()

    a = Image.open(args.unscrolled).convert("RGB")
    b = Image.open(args.scrolled).convert("RGB")
    if a.size != b.size:
        print(f"RED  captures differ in size: {a.size} vs {b.size}")
        return 1

    if args.plant == "header-moves":
        # Paint over the header of one capture only.
        b = b.copy()
        b.paste((255, 0, 0), args.header)
        print("CONTROL: planted a moving header")
    elif args.plant == "body-frozen":
        b = a.copy()
        print("CONTROL: planted a frozen body (scrolled == unscrolled)")

    header_moved = region_changed(a, b, args.header)
    body_moved = region_changed(a, b, args.body)

    print(f"header_fixed={not header_moved}  body_scrolled={body_moved}")

    failures = []
    if header_moved:
        failures.append("the header moved with the content, so it is not fixed")
    if not body_moved:
        failures.append(
            "the body is pixel-identical after scrolling — either it has no "
            "content to scroll or the scroll did not take effect. This is NOT "
            "a pass: an empty panel scrolls to nothing and looks identical."
        )

    for f in failures:
        print(f"  RED  {f}")
    if failures:
        print(f"RED    {len(failures)} violation(s)")
        return 1

    print("GREEN  header stayed fixed and the body scrolled beneath it")
    if args.plant:
        print("BROKEN: the planted negative did not fail the check", file=sys.stderr)
        return 4
    return 0


if __name__ == "__main__":
    sys.exit(main())
