#!/usr/bin/env bash
set -euo pipefail

# Bounded liveness check for a disposable Windows QEMU guest. Keep this check
# on the same SSH wrapper used by build automation so a regression in stdin or
# key selection is caught before a build is started.
count="${SPECTR_WINDOWS_SSH_PROBES:-5}"
port="${SPECTR_WINDOWS_SSH_PORT:-50375}"
timeout="${SPECTR_WINDOWS_SSH_TIMEOUT:-8}"
ready_timeout="${SPECTR_WINDOWS_SSH_READY_TIMEOUT:-120}"
[[ "$count" =~ ^[1-9][0-9]*$ ]] || { echo "SPECTR_WINDOWS_SSH_PROBES must be a positive integer" >&2; exit 2; }
[[ "$ready_timeout" =~ ^[1-9][0-9]*$ ]] || { echo "SPECTR_WINDOWS_SSH_READY_TIMEOUT must be a positive integer" >&2; exit 2; }

# A newly booted Windows guest can accept TCP before OpenSSH is ready. Treat
# resets during that bounded window as boot progress, then fail closed.
deadline=$(( $(date +%s) + ready_timeout ))
ready=0
while (( $(date +%s) <= deadline )); do
  if output="$(SPECTR_WINDOWS_SSH_PORT="$port" SPECTR_WINDOWS_SSH_TIMEOUT="$timeout" \
      tools/windows/ssh-qemu.sh 'echo SPECTR_QEMU_SSH_OK' 2>/dev/null)" &&
      [[ "$output" == *SPECTR_QEMU_SSH_OK* ]]; then
    ready=1
    break
  fi
  sleep 2
done
if (( ! ready )); then
  echo "QEMU SSH did not become ready within ${ready_timeout}s on port ${port}" >&2
  exit 1
fi
printf 'ready=ok port=%s waited_seconds=%s\n' "$port" "$(( ready_timeout - (deadline - $(date +%s)) ))"

for probe in $(seq 1 "$count"); do
  started="$(date +%s%N)"
  output="$(SPECTR_WINDOWS_SSH_PORT="$port" SPECTR_WINDOWS_SSH_TIMEOUT="$timeout" \
    tools/windows/ssh-qemu.sh 'echo SPECTR_QEMU_SSH_OK')"
  finished="$(date +%s%N)"
  [[ "$output" == *SPECTR_QEMU_SSH_OK* ]] || {
    echo "QEMU SSH probe $probe/$count returned an unexpected response: $output" >&2
    exit 1
  }
  elapsed_ms=$(( (finished - started) / 1000000 ))
  printf 'probe=%s/%s port=%s elapsed_ms=%s status=ok\n' "$probe" "$count" "$port" "$elapsed_ms"
done
