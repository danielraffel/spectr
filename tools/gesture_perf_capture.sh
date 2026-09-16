#!/usr/bin/env bash
# One Spectr capture arm, with the machine load it was taken under recorded
# alongside it. A distribution without its load is not interpretable.
#
# usage: run_arm.sh APP OUTDIR LABEL GESTURE_SPEC BANDS {audio|noaudio} {on|off} [EXTRA_ENV...]
#
# Load is sampled as the runnable-thread count, NOT `uptime`: the 1-minute
# load average decays for minutes after a burst, so it describes the past.
set -uo pipefail
app=$1; out=$2; label=$3; gesture=$4; bands=$5; audio=$6; trace=$7; shift 7
mkdir -p "$out"
bin="$app/Contents/MacOS/Spectr"
[ -x "$bin" ] || { echo "no binary: $bin" >&2; exit 2; }
t="$out/$label.pftrace"; shot="$out/$label.png"; state="$out/$label.state.json"
rm -f "$t" "$shot" "$state"

( for i in $(seq 1 320); do ps -Ao state | grep -c '^R'; sleep 0.25; done ) > "$out/$label.load" 2>/dev/null &
loadpid=$!

env_args=(PULP_SCREENSHOT="$shot" PULP_FRAMES=460 SPECTR_STATE_OUT="$state")
[ "$trace" = on ] && env_args+=(PULP_TRACE_PATH="$t")
[ "$bands" = 64 ] && env_args+=(SPECTR_BANDS_PERF_FIXTURE=1)
[ -n "$gesture" ] && env_args+=(SPECTR_GESTURE_PERF="$gesture")
if [ "$audio" = audio ]; then
  env_args+=(PULP_SCREENSHOT_KEEP_AUDIO=1 PULP_TEST_SIGNAL=sine PULP_TEST_SIGNAL_AMPLITUDE=0)
fi
for e in "$@"; do env_args+=("$e"); done

start=$(python3 -c 'import time;print(time.time())')
env "${env_args[@]}" "$bin" > "$out/$label.stdout" 2>&1 &
apppid=$!
# Hard wall-clock cap: an audio device must never be left open by a hang.
( sleep 90; kill -9 $apppid 2>/dev/null ) & capper=$!
wait $apppid; rc=$?
kill $capper 2>/dev/null
kill $loadpid 2>/dev/null
wall=$(python3 -c "import time;print(round(time.time()-$start,2))")

sz=$(stat -f%z "$t" 2>/dev/null || echo 0)
med=$(sort -n "$out/$label.load" | awk '{a[NR]=$1} END{if(NR)print a[int(NR/2)+1]; else print "?"}')
mx=$(sort -n "$out/$label.load" | tail -1)
sel=$(grep -o 'selection=[0-9]*' "$out/$label.stdout" | head -1)
printf '%-22s rc=%s wall=%ss trace_bytes=%s load_med_R=%s load_max_R=%s %s audio=%s trace=%s\n' \
  "$label" "$rc" "$wall" "$sz" "$med" "$mx" "${sel:-selection=n/a}" "$audio" "$trace"
echo "$label,$rc,$wall,$sz,$med,$mx,${sel:-selection=n/a},$audio,$trace" >> "$out/arms.csv"
if [ "$trace" = on ] && [ "$sz" -le 4096 ]; then echo "  TRACE NOT FLUSHED" >&2; exit 1; fi
exit 0
