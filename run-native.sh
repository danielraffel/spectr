#!/bin/bash
# Launch ONLY the native @pulp/react bridge build, not the WebView build.
# WebView build now has CFBundleIdentifier=com.pulp.spectr-webview after
# the 2026-05-03 disambiguation; native is com.pulp.spectr.
#
# Use this script (not a bare `open Spectr.app`) so Launch Services can
# never pick the wrong build.
set -euo pipefail
NATIVE_APP="/Users/danielraffel/Code/spectr/build/Spectr.app"
if [ ! -x "$NATIVE_APP/Contents/MacOS/Spectr" ]; then
  echo "Native build not found at $NATIVE_APP. Build first:" >&2
  echo "  cmake -S . -B build -DPulp_DIR=\$HOME/.pulp/sdk/<version>/lib/cmake/Pulp -DSPECTR_NATIVE_EDITOR=ON" >&2
  echo "  cmake --build build --target Spectr_Standalone -j8" >&2
  exit 1
fi
pkill -x Spectr 2>/dev/null || true
sleep 1
open -W "$NATIVE_APP" &
sleep 4
osascript -e 'tell application "Spectr" to activate' >/dev/null
echo "Spectr (native) launched at PID $(pgrep -x Spectr)"
echo "Path: $(ps -p $(pgrep -x Spectr) -o command= 2>/dev/null)"
