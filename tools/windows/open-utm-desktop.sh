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

SPECTR_WINDOWS_SSH_PORT="$ssh_port" \
  SPECTR_WINDOWS_SSH_READY_TIMEOUT="${SPECTR_WINDOWS_SSH_READY_TIMEOUT:-180}" \
  SPECTR_WINDOWS_SSH_PROBES=3 \
  "$repo_root/tools/windows/ssh-qemu-health.sh"

SPECTR_WINDOWS_SSH_PORT="$ssh_port" \
  SPECTR_WINDOWS_RDP_PORT="$rdp_port" \
  "$repo_root/tools/windows/ensure-rdp-over-ssh.sh"

SPECTR_WINDOWS_SSH_PORT="$ssh_port" "$repo_root/tools/windows/utm-daw-status.sh"

open -a "Jump Desktop"
cat <<EOF
UTM desktop ready.
Jump Desktop: open the saved pulp-win-ci connection (127.0.0.1:${rdp_port}).
SSH: 127.0.0.1:${ssh_port}
RDP: 127.0.0.1:${rdp_port}
Windows guest: PULP-WIN-CI / ci
REAPER and Ableton Live 10 Trial: use the desktop shortcuts.
The helper verified both applications and shortcuts before opening the desktop.
DAW launchability does not claim Spectr host acceptance.
EOF
