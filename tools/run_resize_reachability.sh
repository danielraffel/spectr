#!/usr/bin/env bash
# COR-4 gate: resizing the host must not move a control out of the design
# viewport.
#
# Two stages, because the second is meaningless without the first. The harness
# proves the resize path actually runs (it breaks the root bounds and requires
# on_view_resized to repair them) and refuses to emit a sweep otherwise -- under
# a pinned viewport every layout receipt is byte-identical at every host size by
# design, so "nothing changed" is equally consistent with "correct" and with "my
# resize never arrived".
#
# Exit: 0 pass, 1 a control left the viewport, 3 the premise is unproven,
# 77 SKIP (no GPU capture on this host).
set -uo pipefail

SHOT=${1:?usage: run_resize_reachability.sh <native-shot> <python3> <outdir>}
PYTHON=${2:?}
OUT=${3:?}
SIZES=${SPECTR_REACH_SIZES:-660x430,792x516,990x645,1320x860,1650x1075,1320x500}

mkdir -p "$OUT"
SPECTR_SIZES="$SIZES" "$SHOT" --out="$OUT" --backend=gpu --prefix=reach-
rc=$?
if [ "$rc" -ne 0 ]; then
    # 77 (no GPU capture) and 3 (control unproven) both propagate as-is: a
    # skip must never read as a pass, and an unproven premise must never read
    # as a clean sweep.
    exit "$rc"
fi

fail=0
IFS=',' read -ra list <<< "$SIZES"
for size in "${list[@]}"; do
    stem="$OUT/reach-resize-$size"
    if [ ! -f "$stem.layout.json" ]; then
        echo "instrument unusable: no snapshot for $size" >&2
        exit 3
    fi
    printf '%-11s ' "$size"
    "$PYTHON" "$(dirname "$0")/reachability_census.py" "$stem" \
        --allow-under __behavior_pr_4t --allow-under __behavior_pr_4u || fail=1
done
exit "$fail"
