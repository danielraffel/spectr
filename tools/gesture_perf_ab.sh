#!/usr/bin/env bash
# Interleaved A/B of two Spectr builds. The arms alternate within ONE pass so
# both binaries see the same machine load: on a shared Mac the runnable-thread
# count moves several-fold within an hour, so a before-pass and an after-pass
# taken apart are measuring the hour, not the change.
#
# usage: gesture_perf_ab.sh BEFORE.app AFTER.app OUTDIR [REPS]
set -uo pipefail
cd "$(dirname "$0")/.."
BEFORE=${1:?before .app}; AFTER=${2:?after .app}; OUT=${3:-/tmp/spectr-ab}; REPS=${4:-3}
R=tools/gesture_perf_capture.sh
SWEEP=0.12,0.45,0.88,0.45,180
mkdir -p "$OUT"; rm -f "$OUT/arms.csv"
for r in $(seq 1 "$REPS"); do
  "$R" "$BEFORE" "$OUT" "before-marquee-r$r" "cmd,$SWEEP"  32 noaudio off
  "$R" "$AFTER"  "$OUT" "after-marquee-r$r"  "cmd,$SWEEP"  32 noaudio off
  "$R" "$BEFORE" "$OUT" "before-drag-r$r"    "none,$SWEEP" 32 noaudio off
  "$R" "$AFTER"  "$OUT" "after-drag-r$r"     "none,$SWEEP" 32 noaudio off
done
echo "ab complete: $OUT"
