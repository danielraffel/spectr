#!/usr/bin/env bash
set -euo pipefail

# Open the retained ARM64 Windows guest in a visible native RDP window. UTM's
# emulated framebuffer is not a usable Windows display for this image, so RDP
# is the supported interactive transport. This does not reserve a TartCI
# runner and does not create another disk image.
repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
utmctl="/Applications/UTM.app/Contents/MacOS/utmctl"
utm_uuid="${SPECTR_WINDOWS_UTM_UUID:-36132D8D-99F7-4916-A6D8-935A6F4AF55F}"
ssh_port="${SPECTR_WINDOWS_SSH_PORT:-50376}"
rdp_port="${SPECTR_WINDOWS_RDP_PORT:-53390}"
rdp_domain="${SPECTR_WINDOWS_RDP_DOMAIN:-PULP-WIN-CI}"
secret_file="${SPECTR_WINDOWS_UTM_SECRET:-$HOME/.config/pulp/secrets/spectr-windows-utm-admin}"

[[ -x "$utmctl" ]] || { echo "UTM is not installed at $utmctl" >&2; exit 1; }
[[ -f "$secret_file" ]] || { echo "missing credential file: $secret_file" >&2; exit 2; }
command -v sdl-freerdp >/dev/null || { echo "sdl-freerdp is required" >&2; exit 2; }
# shellcheck disable=SC1090
source "$secret_file"
: "${username:?credential file must define username}"
: "${password:?credential file must define password}"

status="$($utmctl list | awk -v uuid="$utm_uuid" '$1 == uuid { print $2; exit }')"
if [[ "$status" != "started" ]]; then
  "$utmctl" start "$utm_uuid" >/dev/null
fi

# A direct disposable QEMU session may be the live guest after a restart.
if ! nc -z 127.0.0.1 "$ssh_port" >/dev/null 2>&1 \
    && nc -z 127.0.0.1 50375 >/dev/null 2>&1; then
  ssh_port=50375
  rdp_port=53389
fi

SPECTR_WINDOWS_SSH_PORT="$ssh_port" \
  SPECTR_WINDOWS_RDP_PORT="$rdp_port" \
  "$repo_root/tools/windows/ensure-rdp-over-ssh.sh" >/dev/null

# Keep the interactive guest deterministic across clones. The Windows image
# does not need a checkout: upload the small host-preparation helper over the
# already-authenticated SSH channel before opening RDP.
ssh_key="${SPECTR_WINDOWS_SSH_KEY:-$HOME/.ssh/id_ed25519_macstudio}"
[[ -f "$ssh_key" ]] || ssh_key="$HOME/.ssh/id_ed25519"
if [[ -f "$repo_root/tools/windows/prepare-reaper-demo.ps1" && -f "$ssh_key" ]]; then
  scp -q -P "$ssh_port" -o BatchMode=yes -o IdentitiesOnly=yes \
    -o StrictHostKeyChecking=no -i "$ssh_key" \
    "$repo_root/tools/windows/prepare-reaper-demo.ps1" \
    "admin@127.0.0.1:/Users/admin/prepare-reaper-demo.ps1"
  SPECTR_WINDOWS_SSH_PORT="$ssh_port" \
    SPECTR_WINDOWS_SSH_KEY="$ssh_key" \
    "$repo_root/tools/windows/ssh-qemu.sh" \
    'powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\\Users\\admin\\prepare-reaper-demo.ps1 -Architecture arm64-win' >/dev/null
fi

echo "Windows RDP: 127.0.0.1:$rdp_port"
echo "Windows user: $username"
sdl-freerdp \
  /v:"127.0.0.1:$rdp_port" \
  /u:"$username" /d:"$rdp_domain" /p:"$password" /cert:ignore \
  /admin \
  /size:"${SPECTR_WINDOWS_RDP_SIZE:-1920x1080}" \
  +auto-reconnect /auto-reconnect-max-retries:10 +dynamic-resolution &
rdp_pid=$!
# UTM may retain focus after starting the guest. Bring the visible RDP window
# forward when macOS accessibility automation is available; failure is
# harmless because Command-Tab still reaches the window.
sleep 1
osascript -e 'tell application "System Events" to set frontmost of process "sdl-freerdp" to true' >/dev/null 2>&1 || true
wait "$rdp_pid"
