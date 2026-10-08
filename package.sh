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
# A side-by-side development identity (SPECTR_DEV_IDENTITY, see
# cmake/SpectrIdentity.cmake) builds differently named targets and bundles, and
# its installer must carry its own package identifiers so it can never replace
# the shipping Spectr receipts. The shipping identity keeps every name below.
DEV_IDENT="$(sed -n 's/^SPECTR_DEV_IDENTITY:STRING=//p' "$CACHE" | tail -1)"
if [[ -n "$DEV_IDENT" ]]; then
  TARGET_PREFIX="Spectr${DEV_IDENT}Dev"
  BUNDLE_NAME="Spectr ${DEV_IDENT} Dev"
  PKG_NAME="Spectr${DEV_IDENT}Dev"
else
  TARGET_PREFIX="Spectr"
  BUNDLE_NAME="Spectr"
  PKG_NAME="Spectr"
fi
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
# Test seams and diagnostics never ship. SPECTR_ENABLE_TEST_SEAMS compiles the
# SPECTR_* environment seams (script injection, synthetic input, planted
# defects) into the products; the other options add acceptance-only code or
# exports to them. A package is built only from a configuration with all OFF.
for opt in SPECTR_ENABLE_TEST_SEAMS SPECTR_ENABLE_PERF_FIXTURES \
           SPECTR_SHARED_PRODUCT_ACCEPTANCE SPECTR_SHARED_NATIVE_HOST_PROBE; do
  if grep -qE "^$opt:BOOL=(ON|TRUE|1|YES)$" "$CACHE"; then
    echo "$opt is ON in $CACHE: a package must come from a release configuration" >&2
    exit 2
  fi
done

# Rebuild every payload named below from this exact clean head. The governor
# leases a bounded share of the shared M5 rather than claiming the machine.
"$PULP_ROOT/tools/ci/governed-build.sh" \
  cmake --build "$BUILD" \
  --target "${TARGET_PREFIX}_Standalone" "${TARGET_PREFIX}_AU" "${TARGET_PREFIX}_VST3" "${TARGET_PREFIX}_CLAP"
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

AU="$BUILD/AU/$BUNDLE_NAME.component"
VST3="$BUILD/VST3/$BUNDLE_NAME.vst3"
CLAP="$BUILD/CLAP/$BUNDLE_NAME.clap"
APP="$BUILD/$BUNDLE_NAME.app"
for artifact in "$AU" "$VST3" "$CLAP" "$APP"; do
  [[ -d "$artifact" ]] || { echo "missing installer input: $artifact" >&2; exit 2; }
done
# The rebuilt products must read no SPECTR_* test seam: their names are absent.
python3 "$ROOT/tools/check_no_test_seams.py" --expect absent "$APP" "$AU" "$VST3" "$CLAP"

# A RELEASE (Spectr.app numbered exactly VER) must read the release feed: an
# app that ships reading a practice or loopback feed, or with no updater, can
# never be offered the next release. Checked before anything is signed.
# A development identity is a different app that must never be offered a
# release over itself, so it is built without an updater and is exempt.
RELEASE_FEED="https://github.com/danielraffel/spectr/releases/latest/download/appcast.xml"
APP_BUILD="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$APP/Contents/Info.plist")"
if [[ "$APP_BUILD" == "$VER" && -z "$DEV_IDENT" ]]; then
  APP_FEED="$(/usr/libexec/PlistBuddy -c 'Print :SUFeedURL' "$APP/Contents/Info.plist" 2>/dev/null || true)"
  [[ "$APP_FEED" == "$RELEASE_FEED" ]] || {
    echo "Spectr.app $APP_BUILD is a release build but its SUFeedURL is '${APP_FEED:-<none>}'," >&2
    echo "not $RELEASE_FEED. Reconfigure with -DSPECTR_SPARKLE_CHANNEL=release and no" >&2
    echo "SPECTR_SPARKLE_FEED_URL, or number it as a practice/preview build." >&2
    exit 2
  }
fi

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
#
# The helper ships as part of this Spectr release, so it carries the Spectr
# version: a tester's report and the Installer's receipt then name the release
# it came with, not the kit's own build number. The source app is never
# touched -- a staging copy under OUT is stamped, and the recipe below signs
# that copy (it re-signs every --app, so the edited Info.plist is sealed).
if [[ -n "${DIAG_APP:-}" ]]; then
  [[ -d "$DIAG_APP" ]] || { echo "DIAG_APP does not exist: $DIAG_APP" >&2; exit 2; }
  [[ -f "${DIAG_ENT:-}" ]] || { echo "DIAG_ENT must name DiagnosticKit.entitlements" >&2; exit 2; }
  DIAG_STAGE="$OUT/diagnostics-staging"
  rm -rf "$DIAG_STAGE"
  mkdir -p "$DIAG_STAGE"
  DIAG_STAGED="$DIAG_STAGE/$(basename "$DIAG_APP")"
  ditto "$DIAG_APP" "$DIAG_STAGED"
  for key in CFBundleShortVersionString CFBundleVersion; do
    /usr/libexec/PlistBuddy -c "Set :$key $VER" "$DIAG_STAGED/Contents/Info.plist" 2>/dev/null ||
      /usr/libexec/PlistBuddy -c "Add :$key string $VER" "$DIAG_STAGED/Contents/Info.plist"
    [[ "$(/usr/libexec/PlistBuddy -c "Print :$key" "$DIAG_STAGED/Contents/Info.plist")" == "$VER" ]] || {
      echo "could not stamp $key=$VER on the staged diagnostics app" >&2; exit 2; }
  done
  args+=(--app "Diagnostics app" "$DIAG_STAGED" "$DIAG_ENT")
fi
[[ "${NOTARIZE:-1}" == 1 ]] || args+=(--no-notarize)

# Spectr.app embeds Sparkle (cmake/SpectrSparkle.cmake). Sign the source
# framework inside-out before handing the app to Pulp's combined-installer
# recipe. The recipe signs a staging copy; validating the original build tree
# after it returns otherwise sees Sparkle's ad-hoc Autoupdate and rejects a
# package even though the staged copy was signed. Re-signing here is harmless
# when the recipe also signs its staging copy and keeps both validation paths
# honest.
SPARKLE_FW="$APP/Contents/Frameworks/Sparkle.framework"
if [[ -d "$SPARKLE_FW" ]]; then
  "$PULP_ROOT/tools/scripts/ensure_signing_ready.sh" --quiet || {
    echo "signing preflight failed; run 'pulp ship doctor'" >&2; exit 2; }
  SPARKLE_V="$SPARKLE_FW/Versions/B"
  for nested in "$SPARKLE_V/Autoupdate" "$SPARKLE_V/Updater.app"; do
    [[ -e "$nested" ]] && codesign --force --options runtime --timestamp -s "$APP_ID" "$nested"
  done
  [[ -d "$SPARKLE_V/XPCServices" ]] && {
    echo "Sparkle XPC services are present; a non-sandboxed Spectr must not ship them" >&2; exit 2; }
  codesign --force --options runtime --timestamp -s "$APP_ID" "$SPARKLE_FW"
  codesign --verify --deep --strict --verbose=2 "$SPARKLE_FW"
fi

"$PULP_ROOT/tools/scripts/build_combined_installer.sh" "${args[@]}"

# Spectr.app's own CFBundleVersion (APP_BUILD, read above) may carry a practice
# build number (SPECTR_APP_BUILD_VERSION, e.g. 1.0.7.1); everything else is VER.
PKG="$OUT/$PKG_NAME-$VER.pkg"
version_args=(--expected "$VER" --pkg "$PKG" --product-name "$PKG_NAME")
[[ "$APP_BUILD" == "$VER" ]] || version_args+=(--app-build-version "$APP_BUILD")
python3 "$ROOT/tools/check_release_version.py" "${version_args[@]}"

# Each signed bundle declares the macOS floor its binaries are built for.
MIN_OS="$(sed -n 's/^CMAKE_OSX_DEPLOYMENT_TARGET:[^=]*=//p' "$CACHE" | tail -1)"
[[ -n "$MIN_OS" ]] || { echo "build cache names no CMAKE_OSX_DEPLOYMENT_TARGET" >&2; exit 2; }
python3 "$ROOT/tools/check_min_os.py" --expected "$MIN_OS" \
  --bundle "$APP" --bundle "$AU" --bundle "$VST3" --bundle "$CLAP"

# The updater lives in the app and nowhere else, and the signed app's nested
# Sparkle code carries the Developer ID signature notarization requires.
# Notarytool staples the submitted package, not the original build-tree app.
# Validate the extracted, stapled app for a real release; a no-notarize practice
# run keeps validating the source app and therefore intentionally fails spctl.
if [[ -d "$SPARKLE_FW" ]]; then
  SPARKLE_CHECK_APP="$APP"
  if [[ "${NOTARIZE:-1}" == 1 ]]; then
    VERIFY_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/spectr-notarized.XXXXXX")"
    mkdir -p "$VERIFY_ROOT/xar" "$VERIFY_ROOT/payload"
    xar -xf "$PKG" -C "$VERIFY_ROOT/xar"
    APP_PAYLOAD="$VERIFY_ROOT/xar/$PKG_NAME.app.pkg/Payload"
    [[ -f "$APP_PAYLOAD" ]] || {
      echo "notarized package is missing app payload: $APP_PAYLOAD" >&2; exit 2; }
    gzip -dc "$APP_PAYLOAD" | (cd "$VERIFY_ROOT/payload" && cpio -idm >/dev/null)
    SPARKLE_CHECK_APP="$VERIFY_ROOT/payload/Applications/$PKG_NAME.app"
    [[ -d "$SPARKLE_CHECK_APP" ]] || {
      echo "could not extract notarized app for Sparkle validation: $SPARKLE_CHECK_APP" >&2
      exit 2
    }
  fi
  feed_args=()
  [[ "$APP_BUILD" == "$VER" ]] && feed_args=(--feed "$RELEASE_FEED")
  python3 "$ROOT/tools/ship/check_sparkle.py" bundles --signed --app "$SPARKLE_CHECK_APP" \
    --plugin "$AU" --plugin "$VST3" --plugin "$CLAP" ${feed_args[@]+"${feed_args[@]}"}
fi

# A practice package is named for its build so two of them can sit side by side
# on the practice release. A preview (X.Y.(Z-1).9nnn) keeps the product name.
if [[ "$APP_BUILD" == "$VER".* ]]; then
  mv "$PKG" "$OUT/$PKG_NAME-$APP_BUILD.pkg"
  PKG="$OUT/$PKG_NAME-$APP_BUILD.pkg"
  echo "practice package: $PKG"
fi
