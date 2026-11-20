#!/bin/zsh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
SDK="${PULP_SDK:-/Volumes/Atelier/Code/agent-worktrees/_lead/pulp-sdk-gpuhost}"
VELLUM="${VELLUM_SDK:-/Volumes/Atelier/Code/agent-worktrees/_lead/vellum-sdk-gpuhost}"
BUILD="$ROOT/build-vellum-step0"
OUT="$ROOT/artifacts/vellum-step0"
mkdir -p "$OUT"
SHA=$(jq -r .source_git_sha "$SDK/sdk-provenance.json")
~/bin/leased-build -- cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DPulp_DIR="$SDK/lib/cmake/Pulp" -DVellum_DIR="$VELLUM/lib/cmake/Vellum" -DSPECTR_EXPECTED_PULP_SDK_SHA="$SHA" -DSPECTR_VELLUM_EDITOR_PACKAGE=ON -DSPECTR_ENABLE_TEST_SEAMS=ON
~/bin/leased-build -- cmake --build "$BUILD" --target Spectr_CLAP Spectr-test
/usr/bin/clang++ -std=c++20 -fobjc-arc -mmacosx-version-min=13.4 -I"$SDK/include" -I"$SDK/include/clap" -framework AppKit -framework ImageIO -framework UniformTypeIdentifiers tools/vellum-step0/clap_editor_probe.mm -o tools/vellum-step0/clap_editor_probe
PULP_DISABLE_PLUGIN_GPU=1 tools/vellum-step0/clap_editor_probe "$BUILD/CLAP/Spectr.clap" "$OUT" 1 0.75 > "$OUT/editor-probe.log" 2>&1 && PROBE_RC=0 || PROBE_RC=$?
echo "probe_exit_code=$PROBE_RC" >> "$OUT/editor-probe.log"
P="$SDK/bin/pulp"; B="$BUILD/CLAP/Spectr.clap"
"$P" audio render --plugin "$B" --out "$OUT/dry.wav" --duration-ms 250 --input-signal sine:440,-12 --initial-param 1=0 --json > "$OUT/dry.json"
"$P" audio render --plugin "$B" --out "$OUT/wet.wav" --duration-ms 250 --input-signal sine:440,-12 --initial-param 1=100 --json > "$OUT/wet.json"
"$P" audio render --plugin "$B" --out "$OUT/automation.wav" --duration-ms 250 --input-signal sine:440,-12 --initial-param 1=0 --param 1=100@4000 --json > "$OUT/automation.json"
"$P" audio render --plugin "$B" --out "$OUT/restore.wav" --duration-ms 250 --input-signal sine:440,-12 --initial-param 1=0 --param 1=100@4000 --param 1=0@8000 --json > "$OUT/restore.json"
"$P" audio render --plugin "$B" --out "$OUT/unplanted.wav" --duration-ms 250 --input-signal sine:440,-12 --initial-param 2=0 --json > "$OUT/unplanted.json"
"$P" audio render --plugin "$B" --out "$OUT/planted.wav" --duration-ms 250 --input-signal sine:440,-12 --initial-param 2=0.1 --json > "$OUT/planted.json"
ctest --test-dir "$BUILD" -R 'Vellum editor model|Vellum editor keys|Level parameters are registered' --output-on-failure
shasum -a 256 "$OUT"/*.wav "$OUT"/*.png > "$OUT/sha256sums.txt"
[ "$PROBE_RC" -eq 0 ] || { echo "editor probe failed (exit $PROBE_RC); see $OUT/editor-probe.log" >&2; exit "$PROBE_RC"; }
