#!/usr/bin/env bash
set -euo pipefail

# Install a double-clickable macOS launcher for the retained Windows demo VM.
repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
desktop_dir="${SPECTR_WINDOWS_DESKTOP_DIR:-$HOME/Desktop}"
launcher="$desktop_dir/Spectr Windows Demo.command"
readme="$desktop_dir/Spectr Windows Demo - How to Use.md"
mkdir -p "$desktop_dir"
cat >"$launcher" <<EOF
#!/usr/bin/env bash
set -euo pipefail
cd "$repo_root"
exec tools/windows/open-utm-rdp.sh
EOF
chmod +x "$launcher"
cat >"$readme" <<EOF
# Spectr Windows demo

## Open Windows

1. Double-click **Spectr Windows Demo.command** on this Desktop.
2. Wait up to 30 seconds for a window titled **FreeRDP: 127.0.0.1:53390**.
3. Use that window as Windows. The black UTM window is only the VM console;
   leave it open and ignore it.

If the FreeRDP window is behind another app, use **Command-Tab** until the
window titled **FreeRDP: 127.0.0.1:53390** is visible.

## Test Spectr in REAPER

1. Double-click **REAPER (ARM64)** in the Windows desktop.
2. Create or select a track and click **FX**.
3. Choose **VST3: Spectr (Pulp)** and click **Add** to open the Skia/Dawn editor.
   The CLAP entry may show REAPER's generic sliders.

## Test Ableton

Double-click **Ableton Live 10 Trial**. Complete its normal first-run or
licensing flow. Live 12 is not supported by this ARM guest's virtual CPU.

## If it does not open

Run **Spectr Windows Demo.command** again. It starts or reuses the guest,
repairs the RDP connection, and retries transient reconnects automatically.
EOF
printf 'Installed: %s\n' "$launcher"
printf 'Instructions: %s\n' "$readme"
