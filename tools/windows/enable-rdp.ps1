[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$terminalServer = 'HKLM:\SYSTEM\CurrentControlSet\Control\Terminal Server'
Set-ItemProperty -LiteralPath $terminalServer -Name fDenyTSConnections -Type DWord -Value 0
Enable-NetFirewallRule -DisplayGroup 'Remote Desktop'
Set-Service -Name TermService -StartupType Manual
Start-Service -Name TermService

$listener = Get-NetTCPConnection -LocalPort 3389 -State Listen -ErrorAction SilentlyContinue
[ordered]@{
    schema = 1
    generated_at_utc = (Get-Date).ToUniversalTime().ToString('o')
    fDenyTSConnections = (Get-ItemProperty -LiteralPath $terminalServer -Name fDenyTSConnections).fDenyTSConnections
    term_service = (Get-Service -Name TermService).Status.ToString()
    rdp_listener = ($null -ne $listener)
} | ConvertTo-Json -Compress

if ($null -eq $listener) { throw 'TermService is running but no TCP 3389 listener was observed' }
