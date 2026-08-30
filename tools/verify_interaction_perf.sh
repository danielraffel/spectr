#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "usage: $0 BUILD_DIR SPECTR_SHA PULP_SDK_SHA OUTPUT_DIR" >&2
  exit 2
}

[[ $# -eq 4 ]] || usage

build_dir=$(cd "$1" && pwd)
spectr_sha=$2
sdk_sha=$3
output_dir=$4
repo_dir=$(cd "$(dirname "$0")/.." && pwd)
app="$build_dir/Spectr.app"
cache="$build_dir/CMakeCache.txt"

[[ $spectr_sha =~ ^[0-9a-f]{40}$ ]] || usage
[[ $sdk_sha =~ ^[0-9a-f]{40}$ ]] || usage
[[ -d "$app" ]] || { echo "missing app: $app" >&2; exit 2; }
[[ -f "$cache" ]] || { echo "missing CMake cache: $cache" >&2; exit 2; }

actual_spectr_sha=$(git -C "$repo_dir" rev-parse HEAD)
if [[ $actual_spectr_sha != "$spectr_sha" ]]; then
  echo "Spectr checkout is $actual_spectr_sha, expected $spectr_sha" >&2
  exit 2
fi

mkdir -p "$output_dir"
output_dir=$(cd "$output_dir" && pwd)

# Product-acceptance gate: prove the exact-head band edit and all three
# minimap gestures (left trim, right trim, rigid-window pan) on the live
# AppKit -> QuickJS -> Skia/Graphite path. Each workload gets its own trace so
# one inexpensive interaction cannot hide the other's tail latency.
for workload in bands minimap; do
  trace="$output_dir/$workload.pftrace"
  screenshot="$output_dir/$workload.png"
  receipt="$output_dir/$workload.json"
  /usr/bin/env swift "$repo_dir/tools/bands_perf_capture.swift" \
    "$app" "$trace" "$screenshot" "$workload"
  "$repo_dir/tools/analyze_interaction_trace.py" \
    --trace "$trace" \
    --app "$app" \
    --cmake-cache "$cache" \
    --workload "$workload" \
    --spectr-sha "$spectr_sha" \
    --sdk-sha "$sdk_sha" \
    --output "$receipt"
done

echo "Spectr interaction acceptance passed: $output_dir"
