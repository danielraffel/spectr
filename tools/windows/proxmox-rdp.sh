#!/usr/bin/env bash
set -euo pipefail

secret_file="${SPECTR_WINDOWS_PROXMOX_SECRET:-$HOME/.config/pulp/secrets/spectr-windows-proxmox-rdp}"
if [[ ! -f "$secret_file" ]]; then
  echo "missing credential file: $secret_file" >&2
  exit 2
fi
if [[ "$(stat -f %Lp "$secret_file")" != 600 ||
      "$(stat -f %Lp "$(dirname "$secret_file")")" != 700 ]]; then
  echo "credential file must be 0600 inside a 0700 directory" >&2
  exit 2
fi
unset username password
# shellcheck disable=SC1090
source "$secret_file"
: "${username:?credential file must define username}"
: "${password:?credential file must define password}"
export -n username password
command -v sdl-freerdp >/dev/null || { echo "sdl-freerdp is required" >&2; exit 2; }

# FreeRDP reads one option per line from fd 3. The password never enters its
# command line, environment, or a temporary file.
exec sdl-freerdp /args-from:fd:3 3< <(
  printf '%s\n' \
    "/v:${SPECTR_WINDOWS_PROXMOX_HOST:-192.168.86.21}:3389" \
    "/u:$username" "/p:$password" /cert:ignore \
    "/size:${SPECTR_WINDOWS_RDP_SIZE:-1920x1080}"
)
