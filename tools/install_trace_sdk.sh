#!/usr/bin/env bash
# Install an immutable, development-only Pulp SDK with Perfetto compiled in.
# This exists solely for Spectr's exact-head M5 interaction acceptance. The
# resulting SDK is marked distribution_eligible=false and must never feed the
# installer/package lane.
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 /path/to/exact-clean-pulp-checkout" >&2
  exit 2
fi

[[ -d "$1" ]] || {
  echo "not a Pulp source checkout: $1" >&2
  exit 2
}
pulp_source=$(cd "$1" && pwd)
[[ -f "$pulp_source/CMakeLists.txt" ]] || {
  echo "not a Pulp source checkout: $pulp_source" >&2
  exit 2
}
[[ -x "$pulp_source/tools/ci/governed-build.sh" ]] || {
  echo "Pulp checkout lacks the governed build wrapper" >&2
  exit 2
}
[[ -x "$pulp_source/tools/scripts/fetch_skia_for_release.py" ]] || {
  echo "Pulp checkout lacks the pinned Skia fetcher" >&2
  exit 2
}
if [[ -n $(git -C "$pulp_source" status --porcelain) ]]; then
  echo "trace SDK requires a completely clean Pulp checkout" >&2
  exit 2
fi
pulp_sha=$(git -C "$pulp_source" rev-parse HEAD)
[[ $pulp_sha =~ ^[0-9a-f]{40}$ ]] || {
  echo "could not resolve exact Pulp source SHA" >&2
  exit 2
}

pulp_home=${PULP_HOME:-${HOME}/.pulp}
platform=darwin-arm64
prefix="$pulp_home/sdk-trace/$platform/$pulp_sha"
config="$prefix/lib/cmake/Pulp/PulpConfig.cmake"
targets="$prefix/lib/cmake/Pulp/PulpTargets.cmake"
provenance="$prefix/sdk-provenance.json"

valid_existing=0
if [[ -f "$config" && -f "$targets" && -f "$provenance" ]]; then
  if rg -q 'Pulp::perfetto|pulp-perfetto' "$targets" \
      && rg -q "\"source_git_sha\"[[:space:]]*:[[:space:]]*\"$pulp_sha\"" "$provenance" \
      && rg -q '"distribution_eligible"[[:space:]]*:[[:space:]]*false' "$provenance"; then
    valid_existing=1
  fi
fi
if [[ $valid_existing -eq 1 ]]; then
  echo "$prefix"
  exit 0
fi
if [[ -e "$prefix" ]]; then
  echo "refusing to overwrite incomplete immutable trace SDK: $prefix" >&2
  exit 2
fi

for dependency in \
  "$pulp_source/external/vst3sdk/pluginterfaces" \
  "$pulp_source/external/AudioUnitSDK/include/AudioUnitSDK/AUBase.h"; do
  [[ -e "$dependency" ]] || {
    echo "missing prepared Pulp dependency: $dependency" >&2
    exit 2
  }
done

python3 "$pulp_source/tools/scripts/fetch_skia_for_release.py" darwin-arm64
if [[ -n $(git -C "$pulp_source" status --porcelain --untracked-files=no) ]]; then
  echo "pinned dependency preparation changed tracked Pulp source" >&2
  exit 2
fi

mkdir -p "$pulp_home/sdk-build-trace/$platform" "$(dirname "$prefix")"
build=$(mktemp -d "$pulp_home/sdk-build-trace/$platform/$pulp_sha.XXXXXX")
staging=$(mktemp -d "$(dirname "$prefix")/.$pulp_sha.XXXXXX")
cleanup() {
  if [[ -n ${build:-} && $build == "$pulp_home/sdk-build-trace/$platform/"* ]]; then
    rm -rf -- "$build"
  fi
  if [[ -n ${staging:-} && $staging == "$(dirname "$prefix")/."* ]]; then
    rm -rf -- "$staging"
  fi
}
trap cleanup EXIT

cmake -S "$pulp_source" -B "$build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=/usr/bin/clang \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
  -DCMAKE_INSTALL_PREFIX="$staging" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DSKIA_DIR="$pulp_source/external/skia-build" \
  -DPULP_MACOS_ARCH=arm64 \
  -DPULP_ENABLE_GPU=ON \
  -DPULP_REQUIRE_GPU_FOR_SDK=ON \
  -DPULP_ENABLE_DESIGN_IMPORT=ON \
  -DPULP_BUILD_WEBVIEW=ON \
  -DPULP_ENABLE_AUDIO_PROBES=OFF \
  -DPULP_ENABLE_INSPECTOR=OFF \
  -DPULP_BUILD_TESTS=OFF \
  -DPULP_BUILD_EXAMPLES=OFF \
  -DPULP_TRACING=ON

# Exact-head Perfetto SDK installation gate. The governed wrapper is the only
# expensive command in this script and takes a fair share of the M5 host.
"$pulp_source/tools/ci/governed-build.sh" \
  cmake --build "$build" --target install

staged_targets="$staging/lib/cmake/Pulp/PulpTargets.cmake"
if [[ ! -f "$staged_targets" ]] \
    || ! rg -q 'Pulp::perfetto|pulp-perfetto' "$staged_targets"; then
  echo "installed SDK did not export the Perfetto link closure" >&2
  exit 1
fi
cat > "$staging/sdk-provenance.json" <<EOF
{
  "schema": "pulp.sdk-provenance.v1",
  "kind": "development",
  "distribution_eligible": false,
  "source_git_sha": "$pulp_sha",
  "profile": "spectr-trace"
}
EOF

mv "$staging" "$prefix"
staging=""

echo "$prefix"
