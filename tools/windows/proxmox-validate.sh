#!/usr/bin/env bash
set -euo pipefail

# Run the proven x64 Windows focused check through the Proxmox guest agent.
# The VM is started only when stopped and is stopped again only when this
# invocation started it, so an interactive session is never interrupted.
host="${SPECTR_PROXMOX_HOST:-macpro}"
vmid="${SPECTR_PROXMOX_VMID:-300}"
build_dir="${SPECTR_PROXMOX_BUILD_DIR:-C:/spectr-win-proof/build-win}"
test_exe="${SPECTR_PROXMOX_TEST_EXE:-${build_dir}/Spectr-test.exe}"
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
if (( ! ready )); then
  echo "Proxmox guest agent did not become ready for VM $vmid" >&2
  exit 1
fi

result="$(ssh -o ConnectTimeout=8 "$host" "qm guest exec $vmid -- cmd.exe /c $test_exe \"Spectr processes audio\" --reporter compact")"
printf '%s\n' "$result"
if [[ "$result" != *'"exitcode" : 0'* || "$result" != *'All tests passed (2 assertions in 1 test case)'* ]]; then
  echo "Proxmox Windows focused check failed" >&2
  exit 1
fi
echo "PASS: Proxmox VM $vmid focused Windows check"
