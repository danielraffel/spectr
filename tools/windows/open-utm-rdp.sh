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

echo "Windows RDP: 127.0.0.1:$rdp_port"
echo "Windows user: $username"
exec sdl-freerdp \
  /v:"127.0.0.1:$rdp_port" \
  /u:"$username" /p:"$password" /cert:ignore \
  /size:"${SPECTR_WINDOWS_RDP_SIZE:-1920x1080}"
