#!/bin/bash
# Launch ONLY the native @pulp/react bridge build, not the WebView build.
#
# Native build:    CFBundleIdentifier=com.pulp.spectr        (this branch)
# WebView build:   CFBundleIdentifier=com.pulp.spectr-webview (feature/webview-editor-parked)
#
# Use this script (not a bare `open Spectr.app`) so Launch Services can
# never pick the wrong build. We launch by absolute binary path AND
# assert the bundle ID is the expected native one — catches accidental
# regression if the WebView build's bundle-ID disambiguation is lost
# (e.g. when rebuilding from feature/webview-editor-parked).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NATIVE_APP="$REPO_ROOT/build/Spectr.app"
EXPECTED_BUNDLE_ID="com.pulp.spectr"

if [ ! -x "$NATIVE_APP/Contents/MacOS/Spectr" ]; then
  cat >&2 <<EOF
Native build not found at $NATIVE_APP. Build first:
  cmake -S . -B build -DPulp_DIR=\$HOME/.pulp/sdk/<version>/lib/cmake/Pulp -DSPECTR_NATIVE_EDITOR=ON
  cmake --build build --target Spectr_Standalone -j8
EOF
  exit 1
fi

# Sanity-check bundle ID — refuse to launch if this is somehow the
# WebView build (saves debugging "why am I seeing the WebView" later).
ACTUAL_BUNDLE_ID="$(plutil -extract CFBundleIdentifier raw "$NATIVE_APP/Contents/Info.plist" 2>/dev/null || echo MISSING)"
if [ "$ACTUAL_BUNDLE_ID" != "$EXPECTED_BUNDLE_ID" ]; then
  cat >&2 <<EOF
Refusing to launch: $NATIVE_APP has bundle ID '$ACTUAL_BUNDLE_ID'
                    expected '$EXPECTED_BUNDLE_ID'.
This usually means the build directory got clobbered with a WebView
build, or the WebView branch's CMake disambiguation was lost.
Re-run from the native branch:
  cmake -S . -B build -DPulp_DIR=\$HOME/.pulp/sdk/<version>/lib/cmake/Pulp -DSPECTR_NATIVE_EDITOR=ON
  cmake --build build --target Spectr_Standalone -j8
EOF
  exit 1
fi

# Direct launch by absolute binary path bypasses Launch Services
# entirely, so even if a stale WebView bundle is registered with the
# same ID, it cannot be picked. The pkill ensures we don't end up
# attached to a previous instance.
pkill -x Spectr 2>/dev/null || true
sleep 1
"$NATIVE_APP/Contents/MacOS/Spectr" &
SPECTR_PID=$!
sleep 1
osascript -e 'tell application "Spectr" to activate' >/dev/null 2>&1 || true
echo "Spectr (native) launched"
echo "  bundle id:  $ACTUAL_BUNDLE_ID"
echo "  binary:     $NATIVE_APP/Contents/MacOS/Spectr"
echo "  pid:        $SPECTR_PID"
