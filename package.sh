#!/usr/bin/env bash
# Build Spectr's single macOS installer from one exact Release build.
# Signing and notarization stay in Pulp's canonical combined-installer recipe;
# this wrapper owns only Spectr's artifact paths and fail-closed provenance.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${BUILD:-$ROOT/build}"
OUT="${OUT:-$ROOT/artifacts}"
VER="${VER:-1.0.0}"
PULP_ROOT="${PULP_ROOT:-$(cd "$ROOT/../pulp" 2>/dev/null && pwd || true)}"
PULP_DIR_EXPECTED="${PULP_DIR_EXPECTED:-}"
PULP_SDK_SHA_EXPECTED="${PULP_SDK_SHA_EXPECTED:-}"
APP_ID="${APP_ID:-}"
INST_ID="${INST_ID:-}"

[[ -n "$PULP_ROOT" && -x "$PULP_ROOT/tools/scripts/build_combined_installer.sh" ]] || {
  echo "PULP_ROOT must name a Pulp source checkout with build_combined_installer.sh" >&2
  exit 2
}
[[ -n "$APP_ID" ]] || { echo "APP_ID must be a Developer ID Application identity hash" >&2; exit 2; }
[[ -n "$INST_ID" ]] || { echo "INST_ID must be a Developer ID Installer identity hash" >&2; exit 2; }
[[ -n "$PULP_DIR_EXPECTED" ]] || { echo "PULP_DIR_EXPECTED must name the exact accepted SDK CMake directory" >&2; exit 2; }
[[ "$PULP_SDK_SHA_EXPECTED" =~ ^[0-9a-f]{40}$ ]] || {
  echo "PULP_SDK_SHA_EXPECTED must be the exact accepted 40-character Pulp source SHA" >&2
  exit 2
}

CACHE="$BUILD/CMakeCache.txt"
[[ -f "$CACHE" ]] || { echo "missing Spectr build cache: $CACHE" >&2; exit 2; }
grep -q '^CMAKE_BUILD_TYPE:STRING=Release$' "$CACHE" || {
  echo "Spectr installer inputs must come from a Release build" >&2
  exit 2
}
SOURCE_ROOT="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$CACHE" | tail -1)"
SOURCE_ROOT="$(cd "$SOURCE_ROOT" 2>/dev/null && pwd || true)"
[[ "$SOURCE_ROOT" == "$ROOT" ]] || {
  echo "build was not configured from this Spectr worktree: ${SOURCE_ROOT:-unknown}" >&2
  exit 2
}
PULP_DIR_ACTUAL="$(sed -n 's/^Pulp_DIR:[^=]*=//p' "$CACHE" | tail -1)"
PULP_DIR_ACTUAL="$(cd "$PULP_DIR_ACTUAL" 2>/dev/null && pwd || true)"
PULP_DIR_EXPECTED="$(cd "$PULP_DIR_EXPECTED" 2>/dev/null && pwd || true)"
[[ -n "$PULP_DIR_ACTUAL" && "$PULP_DIR_ACTUAL" == "$PULP_DIR_EXPECTED" ]] || {
  echo "build SDK mismatch: expected ${PULP_DIR_EXPECTED:-missing}, got ${PULP_DIR_ACTUAL:-missing}" >&2
  exit 2
}
PULP_SDK_SHA_ACTUAL="$(sed -n 's/^PULP_SDK_SOURCE_GIT_SHA:INTERNAL=//p' "$CACHE" | tail -1)"
[[ "$PULP_SDK_SHA_ACTUAL" == "$PULP_SDK_SHA_EXPECTED" ]] || {
  echo "build SDK source mismatch: expected $PULP_SDK_SHA_EXPECTED, got ${PULP_SDK_SHA_ACTUAL:-missing}" >&2
  exit 2
}
grep -q '^PULP_SDK_PROVENANCE_KIND:INTERNAL=release$' "$CACHE" || {
  echo "Spectr installer inputs must use a provenance-marked release Pulp SDK" >&2
  exit 2
}
grep -q '^PULP_SDK_DISTRIBUTION_ELIGIBLE:INTERNAL=TRUE$' "$CACHE" || {
  echo "Spectr installer inputs must use a distribution-eligible Pulp SDK" >&2
  exit 2
}
[[ -z "$(git -C "$ROOT" status --porcelain --untracked-files=no)" ]] || {
  echo "Spectr tracked source must be clean before the package rebuild" >&2
  exit 2
}

# Rebuild every payload named below from this exact clean head. The governor
# leases a bounded share of the shared M5 rather than claiming the machine.
"$PULP_ROOT/tools/ci/governed-build.sh" \
  cmake --build "$BUILD" \
  --target Spectr_Standalone Spectr_AU Spectr_VST3 Spectr_CLAP
[[ -z "$(git -C "$ROOT" status --porcelain --untracked-files=no)" ]] || {
  echo "the package rebuild changed tracked Spectr source" >&2
  exit 2
}

AU="$BUILD/AU/Spectr.component"
VST3="$BUILD/VST3/Spectr.vst3"
CLAP="$BUILD/CLAP/Spectr.clap"
APP="$BUILD/Spectr.app"
for artifact in "$AU" "$VST3" "$CLAP" "$APP"; do
  [[ -d "$artifact" ]] || { echo "missing installer input: $artifact" >&2; exit 2; }
done

args=(
  --name Spectr
  --version "$VER"
  --sign-identity "$APP_ID"
  --installer-identity "$INST_ID"
  --out "$OUT"
  --architectures arm64
  --plugin au "$AU"
  --plugin vst3 "$VST3"
  --plugin clap "$CLAP"
  --app "Standalone app" "$APP"
)
[[ "${NOTARIZE:-1}" == 1 ]] || args+=(--no-notarize)

exec "$PULP_ROOT/tools/scripts/build_combined_installer.sh" "${args[@]}"
