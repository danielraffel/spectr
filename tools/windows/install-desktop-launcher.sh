#!/usr/bin/env bash
set -euo pipefail

# Install a double-clickable macOS launcher for the retained Windows demo VM.
repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
desktop_dir="${SPECTR_WINDOWS_DESKTOP_DIR:-$HOME/Desktop}"
launcher="$desktop_dir/Spectr Windows Demo.command"
mkdir -p "$desktop_dir"
cat >"$launcher" <<EOF
#!/usr/bin/env bash
set -euo pipefail
cd "$repo_root"
exec tools/windows/open-utm-rdp.sh
EOF
chmod +x "$launcher"
printf 'Installed: %s\n' "$launcher"
printf 'Double-click it to open the visible Windows desktop. The UTM black console can remain open.\n'
