#!/usr/bin/env bash
set -euo pipefail

# A linked QEMU overlay does not persist the interactive desktop settings from
# a previous session. Repair them through the authenticated SSH path before a
# human opens Jump Desktop. The command is deliberately encoded so Windows'
# remote command parser cannot reinterpret registry paths or the firewall
# display-group string.
encoded="$({
  python3 - <<'PY'
import base64

script = r'''$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$terminalServer = 'HKLM:\SYSTEM\CurrentControlSet\Control\Terminal Server'
Set-ItemProperty -LiteralPath $terminalServer -Name fDenyTSConnections -Type DWord -Value 0
netsh advfirewall firewall set rule group="remote desktop" new enable=yes | Out-Null
foreach ($name in @('TermService', 'UmRdpService', 'SessionEnv')) {
  Set-Service -Name $name -StartupType Automatic
  Start-Service -Name $name -ErrorAction SilentlyContinue
}
$listener = Get-NetTCPConnection -LocalPort 3389 -State Listen -ErrorAction SilentlyContinue
if ($null -eq $listener) { throw 'RDP services are running but TCP 3389 is not listening' }
Write-Output 'SPECTR_RDP_READY'
'''
print(base64.b64encode(script.encode('utf-16le')).decode())
PY
})"

output="$(tools/windows/ssh-qemu.sh \
  "powershell.exe -NoProfile -NonInteractive -EncodedCommand ${encoded}")"
[[ "$output" == *SPECTR_RDP_READY* ]] || {
  echo "RDP repair did not return its readiness marker: $output" >&2
  exit 1
}
printf 'rdp=ready port=%s\n' "${SPECTR_WINDOWS_RDP_PORT:-53389}"
