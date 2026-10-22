#!/usr/bin/env bash
set -euo pipefail

# Run the proven x64 Windows focused check through the Proxmox guest agent.
# The VM is started only when stopped and is stopped again only when this
# invocation started it, so an interactive session is never interrupted.
host="${SPECTR_PROXMOX_HOST:-macpro}"
vmid="${SPECTR_PROXMOX_VMID:-300}"
# VM 300's current golden keeps the x64 build under this path.  Preserve the
# override for rebuilt images, but make the default match the maintained guest
# instead of the retired spectr-win-proof tree.
build_dir="${SPECTR_PROXMOX_BUILD_DIR:-C:/spectr-build-current-x64}"
test_exe="${SPECTR_PROXMOX_TEST_EXE:-${build_dir}/Spectr-test.exe}"
guest_ssh_target="${SPECTR_PROXMOX_GUEST_SSH_TARGET:-pulp-rdp@192.168.86.21}"
started=0

cleanup() {
  if (( started )); then
    ssh -o ConnectTimeout=8 "$host" "qm stop $vmid --timeout 60" >/dev/null
  fi
}
trap cleanup EXIT

status="$(ssh -o ConnectTimeout=8 "$host" "qm status $vmid")"
if [[ "$status" == *"status: stopped"* ]]; then
  ssh -o ConnectTimeout=8 "$host" "qm start $vmid" >/dev/null
  started=1
fi

ready=0
for _ in $(seq 1 "${SPECTR_PROXMOX_AGENT_ATTEMPTS:-36}"); do
  probe="$(ssh -o ConnectTimeout=8 "$host" "qm guest exec $vmid -- cmd.exe /c echo SPECTR_PROXMOX_GUEST_READY" 2>/dev/null || true)"
  if [[ "$probe" == *SPECTR_PROXMOX_GUEST_READY* && "$probe" == *'"exitcode" : 0'* ]]; then
    ready=1
    break
  fi
  sleep 5
done
if (( ready )); then
  result="$(ssh -o ConnectTimeout=8 "$host" "qm guest exec $vmid -- cmd.exe /c $test_exe \"Spectr processes audio\" --reporter compact")"
  transport="qemu-guest-agent"
else
  # Windows OpenSSH is the bounded fallback when QEMU-GA is installed and
  # running in the guest but the Proxmox virtio channel is unavailable. Keep
  # this sequential and single-session so it cannot create an SSH burst.
  echo "WARN: Proxmox guest agent unavailable; using Windows SSH fallback" >&2
  ps_script="& '$test_exe' 'Spectr processes audio' --reporter compact; exit \$LASTEXITCODE"
  encoded="$(printf '%s' "$ps_script" | iconv -f UTF-8 -t UTF-16LE | base64 | tr -d '\n')"
  result="$(ssh -o ConnectTimeout=8 "$guest_ssh_target" "powershell -NoProfile -EncodedCommand $encoded")"
  transport="windows-ssh"
fi
printf '%s\n' "$result"
if [[ "$result" != *'"exitcode" : 0'* || "$result" != *'All tests passed (2 assertions in 1 test case)'* ]]; then
  if [[ "$transport" == windows-ssh && "$result" == *'All tests passed (2 assertions in 1 test case)'* ]]; then
    : # SSH returns the test's output directly rather than qm guest exec JSON.
  else
    echo "Proxmox Windows focused check failed" >&2
    exit 1
  fi
fi
if [[ "$transport" == windows-ssh ]]; then
  echo "PASS: Proxmox VM $vmid focused Windows check via Windows SSH fallback"
else
  echo "PASS: Proxmox VM $vmid focused Windows check"
fi
