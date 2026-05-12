#!/bin/bash
# screencap-spectr.sh — capture Spectr standalone with verified editor mode
#
# REASON THIS EXISTS: I twice mistook a WebView render for native, then
# claimed visual parity that didn't exist. This script REFUSES to produce
# a screenshot that I might misattribute. Mode is verified BEFORE the
# capture is named/saved, so the filename is always honest.
#
# Usage:
#   ./screencap-spectr.sh <expected-mode> <output-path>
# Where:
#   expected-mode : "native" or "webview"
#   output-path   : where to save the PNG
#
# Behaviour:
#   - Launch Spectr.app/Contents/MacOS/Spectr
#   - Wait 6s for first paint
#   - Read the log; assert "Spectr native editor: loaded editor.js" (native)
#     OR "[Spectr] WebView editor attached" (webview)
#   - If actual mode != expected mode, ABORT with clear error
#   - Otherwise screencap, kill Spectr, exit 0

set -euo pipefail

EXPECTED="${1:-}"
OUT="${2:-}"
if [ -z "$EXPECTED" ] || [ -z "$OUT" ]; then
  echo "usage: $0 <native|webview> <output.png>" >&2
  exit 2
fi
if [ "$EXPECTED" != "native" ] && [ "$EXPECTED" != "webview" ]; then
  echo "first arg must be 'native' or 'webview' (got: $EXPECTED)" >&2
  exit 2
fi

SPECTR=/Users/danielraffel/Code/spectr/build/Spectr.app/Contents/MacOS/Spectr
LOG="$(mktemp /tmp/spectr-screencap-XXXXXX.log)"

# Clean state
pkill -9 -f "Spectr.app/Contents/MacOS/Spectr" 2>/dev/null || true
sleep 1
rm -rf "$HOME/Library/Saved Application State/" 2>/dev/null || true

# Launch
"$SPECTR" > "$LOG" 2>&1 &
PID=$!
sleep 6

# Verify mode FROM THE LOG before any capture happens
NATIVE_HIT=$(grep -c "Spectr native editor: loaded editor.js" "$LOG" || true)
WEBVIEW_HIT=$(grep -c "WebView editor attached" "$LOG" || true)

ACTUAL=""
if [ "$NATIVE_HIT" -gt 0 ] && [ "$WEBVIEW_HIT" -eq 0 ]; then
  ACTUAL="native"
elif [ "$WEBVIEW_HIT" -gt 0 ] && [ "$NATIVE_HIT" -eq 0 ]; then
  ACTUAL="webview"
else
  kill "$PID" 2>/dev/null || true
  echo "ERROR: could not determine editor mode from log" >&2
  echo "  native_hits=$NATIVE_HIT webview_hits=$WEBVIEW_HIT" >&2
  echo "  log: $LOG" >&2
  exit 3
fi

if [ "$ACTUAL" != "$EXPECTED" ]; then
  kill "$PID" 2>/dev/null || true
  echo "ERROR: expected $EXPECTED, got $ACTUAL" >&2
  echo "  Hint: cmake -DSPECTR_NATIVE_EDITOR=ON for native;" >&2
  echo "        cmake -DSPECTR_NATIVE_EDITOR=OFF for webview" >&2
  echo "  log: $LOG" >&2
  exit 4
fi

# Mode confirmed → capture
screencapture -x -t png "$OUT"
sleep 1
kill "$PID" 2>/dev/null || true

echo "OK: captured $ACTUAL render to $OUT (log: $LOG)"
