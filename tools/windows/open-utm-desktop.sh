#!/usr/bin/env bash
set -euo pipefail

# Start the retained ARM64 Windows VM and prepare its interactive desktop.
# This is deliberately a single host-side entry point: it absorbs the normal
# UTM paused-start, SSH readiness, and RDP-service repair steps before opening
# Jump Desktop for the saved pulp-win-ci connection.

repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
utmctl="/Applications/UTM.app/Contents/MacOS/utmctl"
utm_uuid="${SPECTR_WINDOWS_UTM_UUID:-36132D8D-99F7-4916-A6D8-935A6F4AF55F}"
ssh_port="${SPECTR_WINDOWS_SSH_PORT:-50376}"
rdp_port="${SPECTR_WINDOWS_RDP_PORT:-53390}"

[[ -x "$utmctl" ]] || { echo "UTM is not installed at $utmctl" >&2; exit 1; }

status="$($utmctl list | awk -v uuid="$utm_uuid" '$1 == uuid { print $2; exit }')"
if [[ "$status" != "started" ]]; then
  "$utmctl" start "$utm_uuid" >/dev/null
fi

# The retained UTM definition uses 50376/53390. A direct disposable QEMU
# session uses the adjacent 50375/53389 pair and can still be the live desktop
# after a restart. Attach to the live pair instead of waiting on a stale UTM
# forwarding rule.
if ! nc -z 127.0.0.1 "$ssh_port" >/dev/null 2>&1 \
    && nc -z 127.0.0.1 50375 >/dev/null 2>&1; then
  ssh_port=50375
  rdp_port=53389
fi

SPECTR_WINDOWS_SSH_PORT="$ssh_port" \
  SPECTR_WINDOWS_SSH_READY_TIMEOUT="${SPECTR_WINDOWS_SSH_READY_TIMEOUT:-180}" \
  SPECTR_WINDOWS_SSH_PROBES=3 \
  "$repo_root/tools/windows/ssh-qemu-health.sh"

SPECTR_WINDOWS_SSH_PORT="$ssh_port" \
  SPECTR_WINDOWS_RDP_PORT="$rdp_port" \
  "$repo_root/tools/windows/ensure-rdp-over-ssh.sh"

ssh_key="${SPECTR_WINDOWS_SSH_KEY:-$HOME/.ssh/id_ed25519_macstudio}"
[[ -f "$ssh_key" ]] || ssh_key="$HOME/.ssh/id_ed25519"
if [[ -f "$repo_root/tools/windows/prepare-reaper-demo.ps1" && -f "$ssh_key" ]]; then
  scp -q -P "$ssh_port" -o BatchMode=yes -o IdentitiesOnly=yes -o IdentityAgent=none \
    -o StrictHostKeyChecking=no -i "$ssh_key" \
    "$repo_root/tools/windows/prepare-reaper-demo.ps1" \
    "admin@127.0.0.1:/Users/admin/prepare-reaper-demo.ps1"
  SPECTR_WINDOWS_SSH_PORT="$ssh_port" \
    SPECTR_WINDOWS_SSH_KEY="$ssh_key" \
    "$repo_root/tools/windows/ssh-qemu.sh" \
    'powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\\Users\\admin\\prepare-reaper-demo.ps1 -Architecture arm64ec-win' >/dev/null
fi

if ! SPECTR_WINDOWS_SSH_PORT="$ssh_port" "$repo_root/tools/windows/utm-daw-status.sh"; then
  echo "WARNING: the authenticated Windows desktop is reachable, but the optional DAW inventory is incomplete; opening Jump Desktop for the REAPER path." >&2
fi

open -a "Jump Desktop"
cat <<EOF
UTM desktop ready.
Jump Desktop: open the saved pulp-win-ci connection (127.0.0.1:${rdp_port}).
SSH: 127.0.0.1:${ssh_port}
RDP: 127.0.0.1:${rdp_port}
Windows guest: PULP-WIN-CI / pulp-win\\admin
Credential file: /Users/danielraffel/.config/pulp/secrets/spectr-windows-utm-admin (local mode 0600)
REAPER (ARM64): use the desktop shortcut or launch it from the Start menu.
Ableton is optional and is not present in the current retained image.
The helper verified the required REAPER installation before opening the desktop.
DAW launchability does not claim Spectr host acceptance.
EOF
