#!/usr/bin/env bash
# Section 5 gate: the unified status overlay must sit where the design puts it,
# track a live drag, and then get out of the way on its own.
#
# Two app runs, not one, because --screenshot fires exactly once per process
# and the overlay's two interesting moments are ~0.4s apart and ~5s apart:
#
#   run A  captures the overlay LIT at the end of a scripted band drag, plus
#          the per-move layout receipts the LIVE check reads.
#   run B  re-runs the same drag but waits out the dismissal hold, probing the
#          overlay's text at 1000/2000/3000/5000ms and capturing the frame
#          after it should be GONE.
#
# The detector is refused a verdict unless both runs prove their drivers
# actually ran: a scripted drag that never pressed, or probes that never fired,
# would leave an overlay that is legitimately absent -- indistinguishable, in
# pixels, from one that dismissed correctly. So "nothing on screen" is only
# allowed to mean "dismissed" once the liveness markers say something was there
# to dismiss.
#
# Exit: 0 pass, 1 an overlay invariant is violated, 3 the premise is unproven
# (a driver never ran, or a capture is missing), 77 SKIP (no GPU editor on this
# host).
set -uo pipefail

APP=${1:?usage: run_status_overlay.sh <standalone-binary> <python3> <outdir>}
PYTHON=${2:?}
OUT=${3:?}
DRAG=${SPECTR_OVERLAY_DRAG:-400,400,760,300,4}
PROBES=${SPECTR_OVERLAY_PROBES:-1000,2000,3000,5000}

rm -rf "$OUT" && mkdir -p "$OUT"

run_app() {
    # $1 dump prefix, $2 screenshot path, $3 frame delay, $4 log, $5 probes
    SPECTR_DRAG="$DRAG" \
    SPECTR_DRAG_DUMP_PREFIX="$1" \
    SPECTR_STATUS_PROBE_MS="$5" \
        "$APP" --screenshot="$2" --screenshot-frame-delay="$3" > "$4" 2>&1
}

run_app "$OUT/g" "$OUT/lit.png" 25 "$OUT/runA.log" ""
run_app "$OUT/s" "$OUT/gone.png" 400 "$OUT/runB.log" "$PROBES"

# A CPU-only editor never mounts the GPU host this capture path needs. That is
# unmeasured, not a regression, so it takes the CTest SKIP contract rather than
# a FAIL that would look identical to a broken overlay.
if grep -q "editor window open" "$OUT/runA.log" \
   && ! grep -q "editor window open (.*gpu=true" "$OUT/runA.log"; then
    echo "SKIP: editor opened without the GPU host; overlay pixels unmeasurable here" >&2
    exit 77
fi

unproven=0
check_marker() {
    # $1 log, $2 pattern, $3 what it proves
    if ! grep -q "$2" "$1"; then
        echo "instrument unusable: $(basename "$1") never showed $3 (/$2/)" >&2
        unproven=1
    fi
}
check_marker "$OUT/runA.log" '\[drag\] released at'   "the scripted drag reaching release"
check_marker "$OUT/runA.log" '\[fixture\] stage move1' "a per-move layout receipt"
check_marker "$OUT/runB.log" '\[drag\] released at'   "the scripted drag reaching release"
check_marker "$OUT/runB.log" '\[status-probe\] t5000'  "the last dismissal probe firing"
for f in "$OUT/lit.png" "$OUT/gone.png" "$OUT/g.release.layout.json"; do
    if [ ! -s "$f" ]; then
        echo "instrument unusable: missing or empty $f" >&2
        unproven=1
    fi
done
[ "$unproven" -eq 0 ] || exit 3

"$PYTHON" "$(dirname "$0")/status_overlay_invariants.py" \
    --stages "$OUT/g" \
    --lit "$OUT/lit.png" \
    --lit-snapshot "$OUT/g.release.layout.json" \
    --hold-stages "$OUT/s" \
    --gone "$OUT/gone.png"
rc=$?
# The detector says 2 when it could not judge a check. That is the same class
# of answer as a dead driver, so it reports as an unproven premise, never as a
# pass and never as a violation it did not actually observe.
[ "$rc" -eq 2 ] && rc=3
exit "$rc"
