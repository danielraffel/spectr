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

# Make the interactive path deterministic before opening the desktop. A fresh
# REAPER profile has no [audioconfig] section and otherwise stops at its first-
# run device modal, which prevents the Spectr host probe from ever reaching FX
# discovery. Set SPECTR_WINDOWS_PROXMOX_PREPARE=0 only for an intentional
# operator-managed session.
if [[ "${SPECTR_WINDOWS_PROXMOX_PREPARE:-1}" != 0 ]]; then
  repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
  prepare_script="$repo_root/tools/windows/prepare-reaper-demo.ps1"
  ssh_key="${SPECTR_WINDOWS_PROXMOX_SSH_KEY:-$HOME/.ssh/id_ed25519}"
  ssh_target="${SPECTR_WINDOWS_PROXMOX_SSH_TARGET:-pulp-rdp@${SPECTR_WINDOWS_PROXMOX_HOST:-192.168.86.21}}"
  [[ -f "$prepare_script" ]] || { echo "missing REAPER preparation helper: $prepare_script" >&2; exit 2; }
  [[ -f "$ssh_key" ]] || { echo "missing Proxmox SSH key: $ssh_key (set SPECTR_WINDOWS_PROXMOX_PREPARE=0 to opt out)" >&2; exit 2; }
  scp -q -o BatchMode=yes -o IdentitiesOnly=yes -o StrictHostKeyChecking=no \
    -i "$ssh_key" "$prepare_script" "$ssh_target:C:/Users/pulp-rdp/prepare-reaper-demo.ps1"
  ssh -n -o BatchMode=yes -o IdentitiesOnly=yes -o StrictHostKeyChecking=no \
    -i "$ssh_key" "$ssh_target" \
    'powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\Users\pulp-rdp\prepare-reaper-demo.ps1 -Architecture x86_64-win'
  echo "reaper=prepared audio=dummy architecture=x86_64-win"
fi

# FreeRDP reads one option per line from fd 3. The password never enters its
# command line, environment, or a temporary file.
exec sdl-freerdp /args-from:fd:3 3< <(
  printf '%s\n' \
    "/v:${SPECTR_WINDOWS_PROXMOX_HOST:-192.168.86.21}:3389" \
    "/u:$username" "/p:$password" /cert:ignore \
    "/size:${SPECTR_WINDOWS_RDP_SIZE:-1920x1080}"
)
