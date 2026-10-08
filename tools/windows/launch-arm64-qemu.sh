#!/usr/bin/env bash
set -euo pipefail

# Keep the Windows ARM64 base stopped while linked overlays run.
BASE_IMAGE="${SPECTR_WINDOWS_BASE:-/Volumes/Atelier/VMs/bench/pulp-windows-build-24h2-arm64-utm-fresh-20261007.qcow2}"
FIRMWARE="${SPECTR_WINDOWS_FIRMWARE:-/Volumes/Atelier/VMs/bench/qemu-windows-gui-code-20261007.fd}"
VARS_TEMPLATE="${SPECTR_WINDOWS_VARS_TEMPLATE:-/Volumes/Atelier/VMs/bench/qemu-windows-gui-vars-20261007.fd}"
STATE_DIR="${SPECTR_WINDOWS_STATE_DIR:-/Volumes/Atelier/VMs/bench/spectr-windows-sessions}"
MODE="${1:---display=cocoa}"
[[ "$MODE" == --display=* ]] || { echo 'usage: launch-arm64-qemu.sh [--display=cocoa|none]' >&2; exit 2; }
command -v qemu-img >/dev/null || { echo 'qemu-img is required' >&2; exit 1; }
command -v qemu-system-aarch64 >/dev/null || { echo 'qemu-system-aarch64 is required' >&2; exit 1; }
for path in "$BASE_IMAGE" "$FIRMWARE" "$VARS_TEMPLATE"; do [[ -f "$path" ]] || { echo "missing: $path" >&2; exit 1; }; done
port="${SPECTR_WINDOWS_SSH_PORT:-50375}"
rdp_port="${SPECTR_WINDOWS_RDP_PORT:-53389}"
if nc -z 127.0.0.1 "$port" >/dev/null 2>&1; then
  echo "SSH forward port is already in use: $port" >&2
  exit 1
fi
if nc -z 127.0.0.1 "$rdp_port" >/dev/null 2>&1; then
  echo "RDP forward port is already in use: $rdp_port" >&2
  exit 1
fi
mkdir -p "$STATE_DIR"
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
session="$STATE_DIR/spectr-$stamp-$$"
mkdir "$session"
overlay="$session/overlay.qcow2"
vars="$session/vars.fd"
cleanup() { rm -f "$overlay" "$vars"; rmdir "$session" 2>/dev/null || true; }
trap cleanup EXIT
qemu-img create -f qcow2 -F qcow2 -b "$BASE_IMAGE" "$overlay" >/dev/null
cp "$VARS_TEMPLATE" "$vars"
echo "Session: $session"
echo "SSH: admin@127.0.0.1:$port"
echo "RDP: 127.0.0.1:$rdp_port"
qemu-system-aarch64 \
  -name spectr-windows-arm64 \
  -accel hvf -machine virt,highmem=on,gic-version=3 -cpu host \
  -smp "${SPECTR_WINDOWS_CPUS:-8}" -m "${SPECTR_WINDOWS_MEMORY_MB:-8192}" \
  -drive if=pflash,format=raw,readonly=on,file="$FIRMWARE" \
  -drive if=pflash,format=raw,file="$vars" \
  -device ramfb -device qemu-xhci,id=usb -device usb-kbd -device usb-tablet \
  -netdev "user,id=net0,hostfwd=tcp:127.0.0.1:${port}-:22,hostfwd=tcp:127.0.0.1:${rdp_port}-:3389" \
  -device virtio-net-pci,netdev=net0 \
  -drive file="$overlay",if=none,id=nvm,format=qcow2 -device nvme,drive=nvm,serial=spectrwin \
  -display "${MODE#--display=}"
