[CmdletBinding()]
param(
    [ValidateSet('x86_64-win', 'arm64-win', 'arm64ec-win')]
    [string]$Architecture = 'arm64-win',
    [string]$ReaperIni = '',
    [string]$Receipt = '',
    [switch]$KeepAudioMode
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ReaperIni)) {
    $ReaperIni = Join-Path $env:APPDATA 'REAPER\REAPER.ini'
}
$ReaperIni = (Resolve-Path -LiteralPath $ReaperIni).Path
if ([string]::IsNullOrWhiteSpace($Receipt)) {
    $Receipt = Join-Path (Split-Path $ReaperIni -Parent) 'spectr-demo-preparation-receipt.json'
}

$backup = "$ReaperIni.bak-spectr-demo"
if (-not (Test-Path -LiteralPath $backup -PathType Leaf)) {
    Copy-Item -LiteralPath $ReaperIni -Destination $backup
}

$text = Get-Content -LiteralPath $ReaperIni -Raw
# Do not reopen a project that previously failed to load. The operator can
# still open it explicitly after the host is stable.
$text = [regex]::Replace($text, '(?m)^(lastproject|faultyproject|projecttab2)=.*\r?\n?', '')
if (-not $KeepAudioMode) {
    # REAPER's Dummy Audio mode is 4. It is deterministic in a VM and avoids
    # coupling plugin acceptance to a host WASAPI device.
    $text = [regex]::Replace($text, '(?s)(\[audioconfig\]\r?\n)mode=\d+', '$1mode=4', 1)
}
Set-Content -LiteralPath $ReaperIni -Value $text -Encoding Unicode

$receiptObject = [ordered]@{
    schema = 1
    generated_at_utc = (Get-Date).ToUniversalTime().ToString('o')
    architecture = $Architecture
    reaper_ini = $ReaperIni
    backup = $backup
    dummy_audio_configured = (-not $KeepAudioMode)
    startup_project_cleared = $true
    limitation = 'This prepares a deterministic REAPER host. It does not claim plugin discovery, instantiation, audio, or screenshot acceptance.'
}
$receiptObject | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Receipt -Encoding utf8
Write-Output "PASS: REAPER demo host prepared ($Architecture)"
Write-Output "Receipt: $Receipt"
