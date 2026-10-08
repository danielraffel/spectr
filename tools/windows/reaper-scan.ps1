[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$Vst3Root,
    [ValidateSet('x86_64-win', 'arm64-win')] [string]$Architecture = 'x86_64-win',
    [string]$ReaperExe = '',
    [string]$CachePath = (Join-Path $env:APPDATA 'REAPER\reaper-vstplugins64.ini'),
    [string]$Receipt = ''
)

$ErrorActionPreference = 'Stop'
$Vst3Root = (Resolve-Path -LiteralPath $Vst3Root).Path
if ([string]::IsNullOrWhiteSpace($Receipt)) {
    $Receipt = Join-Path (Split-Path $Vst3Root -Parent) 'reaper-scan-receipt.json'
}
if ([string]::IsNullOrWhiteSpace($ReaperExe)) {
    $reaperDirectory = if ($Architecture -eq 'arm64-win') { 'REAPER (arm64)' } else { 'REAPER (x64)' }
    $ReaperExe = Join-Path (Join-Path $env:ProgramFiles $reaperDirectory) 'reaper.exe'
}
if (-not (Test-Path -LiteralPath $ReaperExe -PathType Leaf)) { throw "REAPER is missing: $ReaperExe" }
$plugin = Join-Path $Vst3Root 'Spectr.vst3'
if (-not (Test-Path -LiteralPath $plugin -PathType Leaf)) { throw "Packaged VST3 binary is missing: $plugin" }
if (-not (Test-Path -LiteralPath $CachePath -PathType Leaf)) { throw "REAPER VST cache is missing: $CachePath" }
$cacheText = Get-Content -LiteralPath $CachePath -Raw
$entryMatch = [regex]::Match($cacheText, '(?m)^(?:Spectr\.dll|Spectr\.vst3)=.*$')
$entry = if ($entryMatch.Success) { $entryMatch.Value } else { $null }
$reaperHash = (Get-FileHash -LiteralPath $ReaperExe -Algorithm SHA256).Hash
$pluginHash = (Get-FileHash -LiteralPath $plugin -Algorithm SHA256).Hash
$receiptObject = [ordered]@{
    schema = 1
    generated_at_utc = (Get-Date).ToUniversalTime().ToString('o')
    reaper = (Get-Item -LiteralPath $ReaperExe).FullName
    reaper_sha256 = $reaperHash
    architecture = $Architecture
    plugin_path = $plugin
    plugin_sha256 = $pluginHash
    cache_path = $CachePath
    scan_entry = $entry
    scan_entry_present = ($null -ne $entry)
    limitation = 'A cache entry proves discovery only; load and audio acceptance require a desktop-capable host session.'
}
$receiptObject | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Receipt -Encoding utf8
if ($null -eq $entry) { throw "REAPER did not record Spectr in $CachePath" }
Write-Output "PASS: REAPER scan entry: $entry"
Write-Output "Receipt: $Receipt"
