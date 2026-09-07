#!/usr/bin/env bash
# Build and run the NSCursor observation proof against an installed Pulp SDK.
#
#   tools/cursor-proof/build.sh <sdk-prefix>
#
# Exits 0 GREEN, 1 RED, 4 BROKEN (the control could not separate the cases).
set -euo pipefail
SDK="${1:?usage: build.sh <sdk-prefix>}"
OUT="${2:-/tmp/ns_cursor_proof}"
SK="$SDK/external/skia-build/build/mac-gpu/lib/Release"
[ -f "$SK/libskia.a" ] || { echo "no Skia at $SK (headers-only SDK?)" >&2; exit 2; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
# Link the ONE shipping object that defines set_ns_cursor_for_style, so the
# proof exercises the function the window host calls rather than a reimplementation.
( cd "$WORK" && ar x "$SDK/lib/libpulp-view-core.a" window_host_mac_geometry.mm.o )

clang++ -std=c++20 -fobjc-arc -I"$SDK/include" \
  "$(dirname "$0")/ns_cursor_proof.mm" "$WORK/window_host_mac_geometry.mm.o" \
  -o "$OUT" \
  -L"$SDK/lib" -lpulp-view-core -lpulp-canvas -lpulp-runtime -lpulp-platform \
  -lpulp-state -lyogacore -lpulp-events -lpulp-bundled-fonts -lpulp-render \
  -L"$SK" -lskia -lskparagraph -lskshaper -lskunicode_core -lskunicode_icu \
  -lsksg -lskresources -lskottie -lsvg -ljsonreader -ldawn_combined \
  -framework AppKit -framework Foundation -framework QuartzCore -framework Metal \
  -framework CoreText -framework CoreGraphics -framework ImageIO \
  -framework MetalKit -framework IOSurface -framework IOKit
exec "$OUT"
