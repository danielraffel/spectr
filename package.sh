#!/usr/bin/env bash
# Build Spectr's single macOS installer from one exact Release build.
# Signing and notarization stay in Pulp's canonical combined-installer recipe;
# this wrapper owns only Spectr's artifact paths and fail-closed provenance.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${BUILD:-$ROOT/build}"
OUT="${OUT:-$ROOT/artifacts}"
VER="${VER:-}"
PULP_ROOT="${PULP_ROOT:-$(cd "$ROOT/../pulp" 2>/dev/null && pwd || true)}"
PULP_DIR_EXPECTED="${PULP_DIR_EXPECTED:-}"
PULP_SDK_SHA_EXPECTED="${PULP_SDK_SHA_EXPECTED:-}"
SPECTR_SHA_EXPECTED="${SPECTR_SHA_EXPECTED:-}"
APP_ID="${APP_ID:-}"
INST_ID="${INST_ID:-}"
# A side-by-side development identity (cmake -DSPECTR_DEV_IDENTITY=<Suffix>):
# "Spectr <Suffix> Dev", its own bundle IDs, AU subtype and VST3 class, so it
# installs next to Spectr rather than over it. Empty packages the shipping
# identity. It must match the identity the build was configured with.
SPECTR_DEV_IDENTITY="${SPECTR_DEV_IDENTITY:-}"

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
# The installer version is the build's product version: the one the plugin
# descriptor, Settings > About and every bundle's Info.plist already carry.
# VER may restate it but never contradict it.
PROJECT_VER="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$CACHE" | tail -1)"
[[ -n "$PROJECT_VER" ]] || { echo "build cache names no CMAKE_PROJECT_VERSION" >&2; exit 2; }
VER="${VER:-$PROJECT_VER}"
[[ "$VER" == "$PROJECT_VER" ]] || {
  echo "VER=$VER disagrees with the build's product version $PROJECT_VER" >&2
  echo "  change project(Spectr VERSION ...) in CMakeLists.txt instead" >&2
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

BUILD_DEV_IDENTITY="$(sed -n 's/^SPECTR_DEV_IDENTITY:[^=]*=//p' "$CACHE" | tail -1)"
[[ "$BUILD_DEV_IDENTITY" == "$SPECTR_DEV_IDENTITY" ]] || {
  echo "build identity mismatch: SPECTR_DEV_IDENTITY=${SPECTR_DEV_IDENTITY:-<shipping>}," >&2
  echo "  but the build was configured with ${BUILD_DEV_IDENTITY:-<shipping>}" >&2
  exit 2
}
if [[ -n "$SPECTR_DEV_IDENTITY" ]]; then
  [[ "$SPECTR_DEV_IDENTITY" =~ ^[A-Za-z][A-Za-z0-9]*$ ]] || {
    echo "SPECTR_DEV_IDENTITY must be alphanumeric: $SPECTR_DEV_IDENTITY" >&2; exit 2; }
  # The names cmake/SpectrIdentity.cmake derives from the suffix.
  TARGET="Spectr${SPECTR_DEV_IDENTITY}Dev"
  PRODUCT="Spectr ${SPECTR_DEV_IDENTITY} Dev"
  # The installer's package identifiers (com.pulp.<name>.*) and file name
  # take no spaces; its title and welcome pane carry the product name.
  PKG_NAME="Spectr-${SPECTR_DEV_IDENTITY}-Dev"
else
  TARGET="Spectr"
  PRODUCT="Spectr"
  PKG_NAME="Spectr"
fi

# Rebuild every payload named below from this exact clean head. The governor
# leases a bounded share of the shared M5 rather than claiming the machine.
"$PULP_ROOT/tools/ci/governed-build.sh" \
  cmake --build "$BUILD" \
  --target "${TARGET}_Standalone" "${TARGET}_AU" "${TARGET}_VST3" "${TARGET}_CLAP"
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

AU="$BUILD/AU/$PRODUCT.component"
VST3="$BUILD/VST3/$PRODUCT.vst3"
CLAP="$BUILD/CLAP/$PRODUCT.clap"
APP="$BUILD/$PRODUCT.app"
for artifact in "$AU" "$VST3" "$CLAP" "$APP"; do
  [[ -d "$artifact" ]] || { echo "missing installer input: $artifact" >&2; exit 2; }
done

# Pulp's signing recipe relocates control-shipping sidecars from Contents/MacOS
# into sealed Resources. Preserve that evidence for the packaged artifacts.

args=(
  --name "$PKG_NAME"
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
# Optional "Spectr Diagnostics" helper for test builds: a DiagnosticKit app
# (https://github.com/danielraffel/pulp-diagnostickit) built with
# tools/ship/diagnostics.env. It installs to /Applications as its own
# Customize-pane component, and a tester who hits "it won't load" runs it to
# save a report ZIP on the Desktop. Unset DIAG_APP to build without it; a set
# DIAG_APP that does not exist is an error, never a silent omission.
if [[ -n "${DIAG_APP:-}" ]]; then
  [[ -d "$DIAG_APP" ]] || { echo "DIAG_APP does not exist: $DIAG_APP" >&2; exit 2; }
  [[ -f "${DIAG_ENT:-}" ]] || { echo "DIAG_ENT must name DiagnosticKit.entitlements" >&2; exit 2; }
  args+=(--app "Diagnostics app" "$DIAG_APP" "$DIAG_ENT")
fi
[[ "${NOTARIZE:-1}" == 1 ]] || args+=(--no-notarize)
if [[ -n "$SPECTR_DEV_IDENTITY" ]]; then
  # Say what this is before anything installs: a development build, under
  # its own name and identifiers, next to (never over) an installed Spectr.
  WELCOME_DIR="$(mktemp -d)"
  trap 'rm -rf "$WELCOME_DIR"' EXIT
  AU_SUBTYPE="$(/usr/libexec/PlistBuddy -c 'Print :AudioComponents:0:subtype' "$AU/Contents/Info.plist")"
  AU_TYPE="$(/usr/libexec/PlistBuddy -c 'Print :AudioComponents:0:type' "$AU/Contents/Info.plist")"
  cat > "$WELCOME_DIR/welcome.html" <<HTML
<!DOCTYPE html><html><head><meta charset="utf-8"></head>
<body style="font-family: -apple-system, Helvetica, sans-serif; font-size: 13px">
<h2>$PRODUCT $VER</h2>
<p>A development build of Spectr, for trying a feature before it ships
(source ${SPECTR_SHA_EXPECTED:0:10}).</p>
<p><b>It installs alongside Spectr, not over it.</b> It is a separate plug-in
named <b>$PRODUCT</b> with its own identifiers (AU $AU_TYPE $AU_SUBTYPE, its
own VST3 class and CLAP id, bundle id
$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$AU/Contents/Info.plist")), so an installed Spectr and the sessions saved with it are left
untouched, and sessions saved with $PRODUCT open only in $PRODUCT.</p>
<p>Remove it later by deleting the $PRODUCT plug-ins and app.</p>
</body></html>
HTML
  args+=(--welcome "$WELCOME_DIR/welcome.html"
         --product-title "$PRODUCT" "$PRODUCT (development build)")
fi

"$PULP_ROOT/tools/scripts/build_combined_installer.sh" "${args[@]}"
python3 "$ROOT/tools/check_release_version.py" --expected "$VER" \
  --title "$PKG_NAME" --pkg "$OUT/$PKG_NAME-$VER.pkg"
