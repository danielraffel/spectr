#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 /path/to/pulp-checkout" >&2
  exit 2
fi

pulp_source=$1
react_package="$pulp_source/packages/pulp-react"
esbuild="$react_package/node_modules/.bin/esbuild"

if [[ ! -x "$esbuild" || ! -f "$react_package/src/index.ts" ]]; then
  echo "Pulp checkout must contain a built packages/pulp-react dependency tree" >&2
  exit 1
fi

repo_root=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$repo_root/native-ui/dist"

NODE_PATH="$react_package/node_modules" "$esbuild" \
  "$repo_root/native-ui/src/editor.tsx" \
  --bundle \
  --format=iife \
  --platform=neutral \
  --define:process.env.NODE_ENV='"production"' \
  --alias:"@pulp/react"="$react_package/src/index.ts" \
  --outfile="$repo_root/native-ui/dist/editor.js"
