#!/usr/bin/env bash
set -euo pipefail

# Windows OpenSSH in the disposable guest must never inherit the caller's
# stdin. Commands that read stdin can otherwise hold the guest channel open and
# make a healthy QEMU session look wedged. Avoid the ssh-agent's rotating key
# set as well; the guest image authorizes this explicit key.
port="${SPECTR_WINDOWS_SSH_PORT:-50375}"
key="${SPECTR_WINDOWS_SSH_KEY:-$HOME/.ssh/id_ed25519}"
[[ -f "$key" ]] || { echo "missing SSH key: $key" >&2; exit 1; }
exec ssh -n -T -o BatchMode=yes -o IdentitiesOnly=yes -i "$key" \
  -o ConnectTimeout="${SPECTR_WINDOWS_SSH_TIMEOUT:-5}" \
  -o StrictHostKeyChecking=no -p "$port" admin@127.0.0.1 "$@"
