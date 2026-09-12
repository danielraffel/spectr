#!/usr/bin/env python3
"""Generate Spectr's app/plugin icon assets from the product's own logo mark.

The mark is the five-bar spectrum glyph the editor chrome draws next to the
wordmark. Its geometry is transcribed here from that glyph's own SVG (a 16x16
viewBox, five rects) so the icon and the in-app logo cannot drift.

Outputs, all under resources/icon/:
  spectr-icon.svg          vector master at 1024 (the committed source of truth)
  spectr-icon-1024.png     raster master consumed by pulp_app_icon()
  png/icon_<N>.png         per-size renders, each generated from the geometry
  Spectr.icns              macOS icon built from the per-size renders

Why per-size renders rather than one downscale: the bars live on a 16-unit
grid, so at small sizes a bar is one or two device pixels wide. Downscaling a
single 1024 raster lands those edges between pixels and turns five distinct
bars into grey mush. Every size here is laid out on its own whole-pixel grid
instead, then drawn supersampled so only the squircle's curve is antialiased.

Usage:  python3 tools/icon/generate_icon.py [--out DIR] [--check]
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys
import tempfile

try:
    from PIL import Image, ImageDraw, ImageFilter
except ImportError:  # pragma: no cover - environment guard
    sys.exit("error: Pillow is required (pip install Pillow)")

# ── The mark ──────────────────────────────────────────────────────────────
# Transcribed from the editor chrome's logo SVG: viewBox "0 0 16 16", five
# rects. Keep this table identical to that glyph.
GRID = 16
BARS = [  # x, y, w, h, hsl-as-rgb
    (1, 6, 2, 4, (74, 74, 235)),     # hsl(240,80%,65%)  violet
    (4, 3, 2, 10, (40, 158, 242)),   # hsl(200,85%,60%)  cyan
    (7, 1, 2, 14, (56, 240, 143)),   # hsl(150,85%,60%)  green
    (10, 4, 2, 8, (242, 242, 40)),   # hsl(60,90%,60%)   yellow
    (13, 7, 2, 2, (242, 56, 56)),    # hsl(0,85%,60%)    red
]
# The bars' bounding box is 14x14 units, inset one unit inside the 16 grid.
BBOX = 14
BAR_UNITS_W = 2
BAR_UNITS_PITCH = 3

# ── The tile ──────────────────────────────────────────────────────────────
# macOS app-icon grid: on a 1024 canvas the tile is 824x824 centred, corner
# radius 185.4 (22.5% of the tile). The corner is a continuous-curvature
# ("squircle") corner, not a circular arc — see _squircle_path.
CANVAS = 1024
TILE_INSET_RATIO = 100 / 1024
TILE_RADIUS_RATIO = 185.4 / 1024

# Ground colours sampled from the app itself: --bg is #05070a and the editor
# chrome composites rgba(10,14,20,...) over it. The tile runs between them so
# it reads as the product's own surface rather than an invented navy.
TILE_TOP = (16, 23, 34)
TILE_BOTTOM = (6, 9, 16)

MARK_FRACTION = 0.62   # mark bbox as a fraction of the tile
GLOW_MIN_SIZE = 128    # below this, bloom and rounded bar caps only blur
ROUND_MIN_SIZE = 128

ICNS_SIZES = [16, 32, 64, 128, 256, 512, 1024]
# (filename, pixel size) pairs iconutil expects inside an .iconset.
ICONSET = [
    ("icon_16x16.png", 16), ("icon_16x16@2x.png", 32),
    ("icon_32x32.png", 32), ("icon_32x32@2x.png", 64),
    ("icon_128x128.png", 128), ("icon_128x128@2x.png", 256),
    ("icon_256x256.png", 256), ("icon_256x256@2x.png", 512),
    ("icon_512x512.png", 512), ("icon_512x512@2x.png", 1024),
]


# ── Geometry ──────────────────────────────────────────────────────────────

def layout(size: int) -> dict:
    """Whole-pixel geometry for one icon size.

    Everything the bars touch is an integer so their edges land exactly on
    device pixels. Only the squircle is allowed fractional coordinates.
    """
    inset = max(1, int(size * TILE_INSET_RATIO))
    tile_w = size - 2 * inset
    radius = size * TILE_RADIUS_RATIO

    target_w = tile_w * MARK_FRACTION
    bar_w = max(1, round(target_w * BAR_UNITS_W / BBOX))
    gap = max(1, round(bar_w / 2))
    mark_w = len(BARS) * bar_w + (len(BARS) - 1) * gap
    x0 = round((size - mark_w) / 2)

    max_h = max(1, round(tile_w * MARK_FRACTION))
    cy = size / 2

    rects = []
    for i, (_, _, _, h_units, rgb) in enumerate(BARS):
        h = max(1, round(h_units / BBOX * max_h))
        x = x0 + i * (bar_w + gap)
        y = round(cy - h / 2)
        rects.append((x, y, bar_w, h, rgb))

    return {
        "size": size, "inset": inset, "tile_w": tile_w, "radius": radius,
        "bar_w": bar_w, "gap": gap, "mark_w": mark_w, "rects": rects,
        "bar_radius": (bar_w * 0.25) if size >= ROUND_MIN_SIZE else 0,
        "glow": size >= GLOW_MIN_SIZE,
    }


def _squircle_path(x0: float, y0: float, w: float, h: float, r: float) -> str:
    """An Apple-style continuous-curvature rounded rectangle, as an SVG path.

    A plain circular-arc corner is visibly not the platform shape: Apple's
    corner is G2-continuous, so curvature ramps in along the edge instead of
    starting abruptly. Each corner consumes 1.528665*r of both adjoining edges
    and is drawn as three cubics (edge ramp, quarter turn, edge ramp).

    Traversing the outline clockwise, corners alternate direction: the
    top-right and bottom-left are entered along a horizontal edge and left
    along a vertical one, while the bottom-right and top-left are the
    transpose. Both variants are below; using one for all four corners folds
    the shape into itself.
    """
    A, B, C = 1.52866498, 1.08849323, 0.86840689
    D, E, F, G = 0.63149379, 0.07491139, 0.37282383, 0.16905956
    r = min(r, min(w, h) / 2 / A)
    x1, y1 = x0 + w, y0 + h

    def hv(cx, cy, sx, sy):
        """Horizontal edge -> vertical edge, ending at (cx, cy + sy*A*r)."""
        return [
            (cx + sx * B * r, cy, cx + sx * C * r, cy, cx + sx * D * r, cy + sy * E * r),
            (cx + sx * F * r, cy + sy * G * r, cx + sx * G * r, cy + sy * F * r,
             cx + sx * E * r, cy + sy * D * r),
            (cx, cy + sy * C * r, cx, cy + sy * B * r, cx, cy + sy * A * r),
        ]

    def vh(cx, cy, sx, sy):
        """Vertical edge -> horizontal edge, ending at (cx + sx*A*r, cy)."""
        return [
            (cx, cy + sy * B * r, cx, cy + sy * C * r, cx + sx * E * r, cy + sy * D * r),
            (cx + sx * G * r, cy + sy * F * r, cx + sx * F * r, cy + sy * G * r,
             cx + sx * D * r, cy + sy * E * r),
            (cx + sx * C * r, cy, cx + sx * B * r, cy, cx + sx * A * r, cy),
        ]

    p = [f"M {x0 + A * r:.4f} {y0:.4f}", f"L {x1 - A * r:.4f} {y0:.4f}"]
    for seg in hv(x1, y0, -1, +1):
        p.append("C " + " ".join(f"{v:.4f}" for v in seg))
    p.append(f"L {x1:.4f} {y1 - A * r:.4f}")
    for seg in vh(x1, y1, -1, -1):
        p.append("C " + " ".join(f"{v:.4f}" for v in seg))
    p.append(f"L {x0 + A * r:.4f} {y1:.4f}")
    for seg in hv(x0, y1, +1, -1):
        p.append("C " + " ".join(f"{v:.4f}" for v in seg))
    p.append(f"L {x0:.4f} {y0 + A * r:.4f}")
    for seg in vh(x0, y0, +1, +1):
        p.append("C " + " ".join(f"{v:.4f}" for v in seg))
    p.append("Z")
    return " ".join(p)


def _squircle_points(x0, y0, w, h, r, steps=48):
    """The same shape flattened to a polygon, for PIL (which has no path API)."""
    import re
    d = _squircle_path(x0, y0, w, h, r)
    toks = re.findall(r"[MLCZ]|-?\d+\.?\d*", d)
    pts, cur, i = [], (0.0, 0.0), 0
    while i < len(toks):
        op = toks[i]
        if op == "M" or op == "L":
            cur = (float(toks[i + 1]), float(toks[i + 2])); pts.append(cur); i += 3
        elif op == "C":
            c1 = (float(toks[i + 1]), float(toks[i + 2]))
            c2 = (float(toks[i + 3]), float(toks[i + 4]))
            end = (float(toks[i + 5]), float(toks[i + 6]))
            for s in range(1, steps + 1):
                t = s / steps; u = 1 - t
                pts.append((
                    u**3 * cur[0] + 3 * u * u * t * c1[0] + 3 * u * t * t * c2[0] + t**3 * end[0],
                    u**3 * cur[1] + 3 * u * u * t * c1[1] + 3 * u * t * t * c2[1] + t**3 * end[1],
                ))
            cur = end; i += 7
        else:
            i += 1
    return pts


# ── Raster ────────────────────────────────────────────────────────────────

def render(size: int) -> Image.Image:
    """Render one size. Bars are drawn on whole-pixel coordinates scaled by an
    integer supersample factor, so box-downsampling reproduces their edges
    exactly while the squircle's curve gets proper antialiasing."""
    L = layout(size)
    F = 8
    S = size * F
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))

    # Tile mask (the only antialiased geometry).
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).polygon(
        _squircle_points(L["inset"] * F, L["inset"] * F,
                         L["tile_w"] * F, L["tile_w"] * F, L["radius"] * F),
        fill=255)

    # Vertical ground gradient.
    grad = Image.new("RGBA", (1, S))
    gp = grad.load()
    for y in range(S):
        t = y / (S - 1)
        gp[0, y] = tuple(round(a + (b - a) * t) for a, b in
                         zip(TILE_TOP, TILE_BOTTOM)) + (255,)
    img.paste(grad.resize((S, S)), (0, 0), mask)

    # Bars.
    bars = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    bd = ImageDraw.Draw(bars)
    for (x, y, w, h, rgb) in L["rects"]:
        box = [x * F, y * F, (x + w) * F - 1, (y + h) * F - 1]
        br = L["bar_radius"] * F
        if br >= 1:
            bd.rounded_rectangle(box, radius=br, fill=rgb + (255,))
        else:
            bd.rectangle(box, fill=rgb + (255,))

    if L["glow"]:
        glow = bars.filter(ImageFilter.GaussianBlur(radius=size * 0.022 * F))
        glow.putalpha(glow.getchannel("A").point(lambda a: int(a * 0.55)))
        glow = Image.composite(glow, Image.new("RGBA", (S, S), (0, 0, 0, 0)), mask)
        img = Image.alpha_composite(img, glow)

    img = Image.alpha_composite(img, Image.composite(
        bars, Image.new("RGBA", (S, S), (0, 0, 0, 0)), mask))

    return img.resize((size, size), Image.BOX)


def build_svg() -> str:
    L = layout(CANVAS)
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{CANVAS}" height="{CANVAS}" '
        f'viewBox="0 0 {CANVAS} {CANVAS}">',
        "  <!-- Generated by tools/icon/generate_icon.py - do not hand-edit. -->",
        "  <defs>",
        '    <linearGradient id="ground" x1="0" y1="0" x2="0" y2="1">',
        f'      <stop offset="0" stop-color="rgb{TILE_TOP}"/>',
        f'      <stop offset="1" stop-color="rgb{TILE_BOTTOM}"/>',
        "    </linearGradient>",
        '    <filter id="bloom" x="-25%" y="-25%" width="150%" height="150%">',
        f'      <feGaussianBlur stdDeviation="{CANVAS * 0.022:.2f}"/>',
        "    </filter>",
        '    <clipPath id="tile">',
        f'      <path d="{_squircle_path(L["inset"], L["inset"], L["tile_w"], L["tile_w"], L["radius"])}"/>',
        "    </clipPath>",
        "  </defs>",
        f'  <path d="{_squircle_path(L["inset"], L["inset"], L["tile_w"], L["tile_w"], L["radius"])}" fill="url(#ground)"/>',
        '  <g clip-path="url(#tile)">',
        '    <g opacity="0.55" filter="url(#bloom)">',
    ]
    for grp, op in (("bloom", True), ("solid", False)):
        for (x, y, w, h, rgb) in L["rects"]:
            parts.append(
                f'      <rect x="{x}" y="{y}" width="{w}" height="{h}" '
                f'rx="{L["bar_radius"]:.2f}" fill="rgb{rgb}"/>')
        if op:
            parts.append("    </g>")
    parts += ["  </g>", "</svg>", ""]
    return "\n".join(parts)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=None, help="output dir (default resources/icon)")
    ap.add_argument("--check", action="store_true",
                    help="regenerate into a temp dir and diff against --out")
    args = ap.parse_args()

    root = pathlib.Path(__file__).resolve().parents[2]
    out = pathlib.Path(args.out) if args.out else root / "resources" / "icon"
    target = pathlib.Path(tempfile.mkdtemp()) if args.check else out
    (target / "png").mkdir(parents=True, exist_ok=True)

    (target / "spectr-icon.svg").write_text(build_svg())

    for s in ICNS_SIZES:
        render(s).save(target / "png" / f"icon_{s}.png")
    render(CANVAS).save(target / "spectr-icon-1024.png")

    with tempfile.TemporaryDirectory() as td:
        iconset = pathlib.Path(td) / "Spectr.iconset"
        iconset.mkdir()
        for name, s in ICONSET:
            render(s).save(iconset / name)
        subprocess.run(["iconutil", "-c", "icns", str(iconset),
                        "-o", str(target / "Spectr.icns")], check=True)

    if args.check:
        r = subprocess.run(["diff", "-r", str(out), str(target)])
        if r.returncode != 0:
            print("error: icon assets are stale; run tools/icon/generate_icon.py",
                  file=sys.stderr)
            return 1
        print("icon assets up to date")
        return 0

    for s in ICNS_SIZES:
        L = layout(s)
        print(f"  {s:>4}px  tile {L['tile_w']:>4}  bar {L['bar_w']:>3}w "
              f"gap {L['gap']:>3}  mark {L['mark_w']:>4}  "
              f"heights {[r[3] for r in L['rects']]}")
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
