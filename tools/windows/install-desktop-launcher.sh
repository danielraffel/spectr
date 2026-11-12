#!/usr/bin/env bash
set -euo pipefail

# Install a double-clickable macOS launcher for the retained Windows demo VM.
repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
desktop_dir="${SPECTR_WINDOWS_DESKTOP_DIR:-$HOME/Desktop}"
runtime_root="${SPECTR_WINDOWS_RUNTIME_DIR:-$HOME/Library/Application Support/Pulp/WindowsDemo}"
launcher="$desktop_dir/Spectr Windows Demo.command"
readme="$desktop_dir/Spectr Windows Demo - How to Use.md"
mkdir -p "$desktop_dir" "$runtime_root/tools/windows"
# Keep routine desktop access working after the development worktree is removed.
for helper in open-utm-rdp.sh ensure-rdp-over-ssh.sh ssh-qemu.sh prepare-reaper-demo.ps1; do
  cp "$repo_root/tools/windows/$helper" "$runtime_root/tools/windows/$helper"
done
cat >"$launcher" <<EOF
#!/usr/bin/env bash
set -euo pipefail
cd "$runtime_root"
exec tools/windows/open-utm-rdp.sh
EOF
chmod +x "$launcher"
cat >"$readme" <<EOF
# Spectr Windows demo

## Open Windows

1. Double-click **Spectr Windows Demo.command** on this Desktop.
2. Wait for a window titled **FreeRDP: 127.0.0.1:53390**. A cold Windows boot
   can take up to two minutes. Sign-in is automatic using the protected local credential.
3. Use that window as Windows. The black UTM window is only the VM console;
   leave it open and ignore it.

If the FreeRDP window is behind another app, use **Command-Tab** until the
window titled **FreeRDP: 127.0.0.1:53390** is visible.

If Jump Desktop opens a saved connection named **spectr-qemu-local**, close it
and use this launcher instead; that saved connection can point at an old port.
You do not need to approve an SSH key or enter a password for this launcher.

## Test Spectr in REAPER

1. Double-click **REAPER (ARM64)** in the Windows desktop.
2. Create or select a track and click **FX**.
3. Choose **VST3: Spectr (Pulp)** and click **Add** to open the Skia/Dawn editor.
   The CLAP entry may show REAPER's generic sliders.

Windows plugin GUI, audio, and performance validation is still in progress.
Opening Windows does not establish that the installed plugin is the latest build.

## Test Ableton

Double-click **Ableton Live 10 Trial**. Complete its normal first-run or
licensing flow. Live 12 is not supported by this ARM guest's virtual CPU.

## If it does not open

Run **Spectr Windows Demo.command** again. It starts or reuses the guest,
repairs the RDP connection, and retries transient reconnects automatically.
EOF
printf 'Installed: %s\n' "$launcher"
printf 'Connection helpers: %s\n' "$runtime_root"
printf 'Instructions: %s\n' "$readme"
