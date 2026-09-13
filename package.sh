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
SPECTR_SHA_EXPECTED="${SPECTR_SHA_EXPECTED:-}"
APP_ID="${APP_ID:-}"
INST_ID="${INST_ID:-}"

[[ -n "$PULP_ROOT" && -x "$PULP_ROOT/tools/scripts/build_combined_installer.sh" ]] || {
  echo "PULP_ROOT must name a Pulp source checkout with build_combined_installer.sh" >&2
  exit 2
}

# The installer recipe is the one packaging input that carried no provenance
# check. Every other input below is pinned to an exact SHA, but PULP_ROOT
# defaults to whatever sibling checkout happens to sit next to this one, at
# whatever revision it happens to be parked on.
#
# That matters because the recipe is not interchangeable. A checkout from
# before prompt-free installer signing signs the product archive with
# `productbuild --sign`, which the dedicated signing keychain's ACL denies:
# the key authorizes productsign, not productbuild. Headless, the suppressed
# authorization dialog comes back as CSSMERR_CSP_USER_CANCELED (-128) and
# "Error signing data." -- naming no keychain, reading like a broken
# certificate, and arriving only AFTER every bundle has already been signed.
# The run dies one step from done, and nothing earlier hints at it.
PULP_INSTALLER_FLOOR="${PULP_INSTALLER_FLOOR:-65cba47ed65af2c20cb897f5f4880b7636f48cb6}"
git -C "$PULP_ROOT" rev-parse --git-dir >/dev/null 2>&1 || {
  echo "PULP_ROOT must be a Git checkout so its installer recipe can be identified: $PULP_ROOT" >&2
  exit 2
}
[[ -z "$(git -C "$PULP_ROOT" status --porcelain --untracked-files=no)" ]] || {
  echo "PULP_ROOT has modified tracked files, so its installer recipe is unprovenanced: $PULP_ROOT" >&2
  exit 2
}
git -C "$PULP_ROOT" merge-base --is-ancestor "$PULP_INSTALLER_FLOOR" HEAD 2>/dev/null || {
  echo "PULP_ROOT does not contain prompt-free installer signing: $PULP_ROOT" >&2
  echo "  at $(git -C "$PULP_ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)" >&2
  echo "  its build_combined_installer.sh signs with productbuild --sign, which the" >&2
  echo "  signing keychain denies after every bundle is already signed." >&2
  echo "  Point PULP_ROOT at a Pulp checkout that contains $PULP_INSTALLER_FLOOR." >&2
  exit 2
}
[[ -n "$APP_ID" ]] || { echo "APP_ID must be a Developer ID Application identity hash" >&2; exit 2; }
[[ -n "$INST_ID" ]] || { echo "INST_ID must be a Developer ID Installer identity hash" >&2; exit 2; }
[[ -n "$PULP_DIR_EXPECTED" ]] || { echo "PULP_DIR_EXPECTED must name the exact accepted SDK CMake directory" >&2; exit 2; }
[[ "$PULP_SDK_SHA_EXPECTED" =~ ^[0-9a-f]{40}$ ]] || {
  echo "PULP_SDK_SHA_EXPECTED must be the exact accepted 40-character Pulp source SHA" >&2
  exit 2
}
[[ "$SPECTR_SHA_EXPECTED" =~ ^[0-9a-f]{40}$ ]] || {
  echo "SPECTR_SHA_EXPECTED must be the exact accepted 40-character Spectr source SHA" >&2
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
SPECTR_SHA_ACTUAL="$(sed -n 's/^SPECTR_SOURCE_GIT_SHA:INTERNAL=//p' "$CACHE" | tail -1)"
[[ "$SPECTR_SHA_ACTUAL" == "$SPECTR_SHA_EXPECTED" ]] || {
  echo "build Spectr source mismatch: expected $SPECTR_SHA_EXPECTED, got ${SPECTR_SHA_ACTUAL:-missing}" >&2
  exit 2
}
grep -q '^SPECTR_SOURCE_GIT_DIRTY:INTERNAL=FALSE$' "$CACHE" || {
  echo "Spectr installer inputs must come from a clean configured source tree" >&2
  exit 2
}
SPECTR_SHA_NOW="$(git -C "$ROOT" rev-parse --verify HEAD)"
[[ "$SPECTR_SHA_NOW" == "$SPECTR_SHA_EXPECTED" ]] || {
  echo "current Spectr source mismatch: expected $SPECTR_SHA_EXPECTED, got $SPECTR_SHA_NOW" >&2
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
SPECTR_SHA_AFTER_BUILD="$(git -C "$ROOT" rev-parse --verify HEAD)"
[[ "$SPECTR_SHA_AFTER_BUILD" == "$SPECTR_SHA_EXPECTED" ]] || {
  echo "Spectr source changed during package rebuild: expected $SPECTR_SHA_EXPECTED, got $SPECTR_SHA_AFTER_BUILD" >&2
  exit 2
}
SPECTR_SHA_CACHED_AFTER_BUILD="$(sed -n 's/^SPECTR_SOURCE_GIT_SHA:INTERNAL=//p' "$CACHE" | tail -1)"
[[ "$SPECTR_SHA_CACHED_AFTER_BUILD" == "$SPECTR_SHA_EXPECTED" ]] || {
  echo "configured Spectr source changed during package rebuild" >&2
  exit 2
}
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

# Contents/MacOS holds executable code and nothing else. The SDK's control
# shipping POST_BUILD copies three JSON sidecars next to the artifact, which for
# a bundle target resolves inside Contents/MacOS, and `codesign --verify --deep
# --strict` then rejects the bundle:
#
#   Spectr: code object is not signed at all
#   In subcomponent: .../Contents/MacOS/Spectr.AUv2.control-shipping.json
#
# The copies are POST_BUILD, so a bundle whose target is already up to date
# never receives them -- which is the only reason an earlier packaging run
# succeeded. Drop every non-Mach-O file from Contents/MacOS so the outcome does
# not depend on whether the link step happened to re-run. The manifests remain
# authoritative under $BUILD/pulp-control-shipping-manifests.
for artifact in "$AU" "$VST3" "$CLAP" "$APP"; do
  while IFS= read -r -d '' sidecar; do
    file -b "$sidecar" 2>/dev/null | grep -q "Mach-O" && continue
    echo "[package] dropping non-code sidecar: ${sidecar#"$artifact/"}"
    rm -f "$sidecar"
  done < <(find "$artifact/Contents/MacOS" -type f -print0 2>/dev/null || true)
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
