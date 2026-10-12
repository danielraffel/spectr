[CmdletBinding()]
param(
    [ValidateSet('x86_64-win', 'arm64-win')]
    [string]$Architecture = 'arm64-win',
    [string]$Vst3Root = '',
    [string]$ReaperExe = '',
    [string]$Receipt = '',
    [switch]$Launch,
    [int]$WaitSeconds = 5
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($Vst3Root)) {
    $Vst3Root = Join-Path $env:CommonProgramFiles ("VST3\Spectr.vst3\Contents\" + $Architecture)
}
$Vst3Root = (Resolve-Path -LiteralPath $Vst3Root).Path
$plugin = Join-Path $Vst3Root 'Spectr.vst3'
if (-not (Test-Path -LiteralPath $plugin -PathType Leaf)) {
    $plugin = Join-Path $Vst3Root 'Spectr.dll'
}
if (-not (Test-Path -LiteralPath $plugin -PathType Leaf)) {
    throw "Spectr VST3 binary is missing under $Vst3Root"
}

if ([string]::IsNullOrWhiteSpace($ReaperExe)) {
    $directory = if ($Architecture -eq 'arm64-win') { 'REAPER (arm64)' } else { 'REAPER (x64)' }
    $ReaperExe = Join-Path (Join-Path $env:ProgramFiles $directory) 'reaper.exe'
}
if (-not (Test-Path -LiteralPath $ReaperExe -PathType Leaf)) {
    throw "REAPER is missing: $ReaperExe"
}

$desktopShell = @(Get-Process -Name explorer -IncludeUserName -ErrorAction SilentlyContinue |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_.UserName) })
if ($desktopShell.Count -eq 0) {
    throw 'No active Windows desktop session. Log in through the QEMU Cocoa window or RDP, then rerun this helper.'
}

if ([string]::IsNullOrWhiteSpace($Receipt)) {
    $Receipt = Join-Path (Split-Path $Vst3Root -Parent) 'reaper-demo-receipt.json'
}
$receiptObject = [ordered]@{
    schema = 1
    generated_at_utc = (Get-Date).ToUniversalTime().ToString('o')
    architecture = $Architecture
    reaper = (Get-Item -LiteralPath $ReaperExe).FullName
    reaper_sha256 = (Get-FileHash -LiteralPath $ReaperExe -Algorithm SHA256).Hash
    plugin = (Get-Item -LiteralPath $plugin).FullName
    plugin_sha256 = (Get-FileHash -LiteralPath $plugin -Algorithm SHA256).Hash
    desktop_session_detected = $true
    launch_requested = [bool]$Launch
    limitation = 'This helper launches REAPER and records exact binaries. Plugin instantiation, audio, and screenshots require a separate observed desktop receipt.'
}

if ($Launch) {
    $process = Start-Process -FilePath $ReaperExe -ArgumentList '-newinstance' -PassThru
    Start-Sleep -Seconds ([Math]::Max(0, $WaitSeconds))
    $receiptObject.reaper_pid = $process.Id
    $receiptObject.reaper_running = -not $process.HasExited
}

$receiptObject | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Receipt -Encoding utf8
Write-Output "PASS: desktop session detected"
if ($Launch) { Write-Output "REAPER PID: $($receiptObject.reaper_pid)" }
Write-Output "Receipt: $Receipt"
