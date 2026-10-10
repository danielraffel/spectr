#!/usr/bin/env bash
set -euo pipefail

secret_file="${SPECTR_WINDOWS_PROXMOX_SECRET:-$HOME/.config/pulp/secrets/spectr-windows-proxmox-rdp}"
if [[ ! -f "$secret_file" ]]; then
  echo "missing credential file: $secret_file" >&2
  exit 2
fi
# shellcheck disable=SC1090
source "$secret_file"
: "${username:?credential file must define username}"
: "${password:?credential file must define password}"
command -v sdl-freerdp >/dev/null || { echo "sdl-freerdp is required" >&2; exit 2; }
exec sdl-freerdp \
  /v:"${SPECTR_WINDOWS_PROXMOX_HOST:-192.168.86.21}:3389" \
  /u:"$username" /p:"$password" /cert:ignore \
  /size:"${SPECTR_WINDOWS_RDP_SIZE:-1920x1080}"
