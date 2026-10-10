#!/usr/bin/env bash
set -euo pipefail

# Verify the two interactive DAWs in the retained ARM64 UTM guest. This is an
# install/shortcut check; it does not claim that Spectr loads in either host.
port="${SPECTR_WINDOWS_SSH_PORT:-50376}"
command_text="$(python3 - <<'PY'
import base64
script = r'''$apps = @(
  @{ Name = 'REAPER (ARM64)'; Path = 'C:\\Program Files\\REAPER (arm64)\\reaper.exe'; Shortcut = 'C:\\Users\\admin\\Desktop\\REAPER (ARM64).lnk' },
  @{ Name = 'Ableton Live 10 Trial'; Path = 'C:\\ProgramData\\Ableton\\Live 10 Trial\\Program\\Ableton Live 10 Trial.exe'; Shortcut = 'C:\\Users\\admin\\Desktop\\Ableton Live 10 Trial.lnk' }
)
foreach ($app in $apps) {
  if (-not (Test-Path -LiteralPath $app.Path)) { throw "$($app.Name) is missing: $($app.Path)" }
  if (-not (Test-Path -LiteralPath $app.Shortcut)) { throw "$($app.Name) shortcut is missing: $($app.Shortcut)" }
  Write-Output ("DAW_READY=" + $app.Name)
  Write-Output ("DAW_PATH=" + $app.Path)
  Write-Output ("DAW_SHORTCUT=" + $app.Shortcut)
}'''
print('powershell.exe -NoProfile -NonInteractive -EncodedCommand ' + base64.b64encode(script.encode('utf-16le')).decode())
PY
)"
output="$(SPECTR_WINDOWS_SSH_PORT="$port" tools/windows/ssh-qemu.sh "$command_text" 2>&1)"

# Windows OpenSSH may serialize PowerShell progress records as CLIXML on the
# same channel. Keep the operator-facing result readable while requiring both
# readiness markers, so startup chatter can never turn a partial check into a
# false pass.
markers="$(printf '%s\n' "$output" | awk '/^(DAW_READY|DAW_PATH|DAW_SHORTCUT)=/')"
ready_count="$(printf '%s\n' "$markers" | awk '/^DAW_READY=/{count++} END{print count+0}')"
if [[ "$ready_count" -ne 2 ]]; then
  printf '%s\n' "$output" >&2
  echo "DAW readiness check failed: expected 2 applications, found $ready_count" >&2
  exit 1
fi
printf '%s\n' "$markers"
