[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$Vst3Root,
    [ValidateSet('x86_64-win', 'arm64-win')] [string]$Architecture = 'x86_64-win',
    [string]$ReaperExe = '',
    [string]$CachePath = (Join-Path $env:APPDATA 'REAPER\reaper-vstplugins64.ini'),
    [string]$Receipt = '',
    [string]$FailedScanEvidence = '',
    [string]$ObservedAcceptanceReceipt = '',
    [switch]$RequireAcceptance
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

# REAPER exposes a cache entry even when the plug-in is listed under
# Preferences -> Plug-ins -> Plug-ins that failed to scan.  Keep that state
# separate from discovery and require explicit host evidence for acceptance.
$failedScanPath = $null
$failedScanText = $null
$failedScanPluginPresent = $null
if (-not [string]::IsNullOrWhiteSpace($FailedScanEvidence)) {
    $failedScanPath = (Resolve-Path -LiteralPath $FailedScanEvidence).Path
    $failedScanText = Get-Content -LiteralPath $failedScanPath -Raw
    $failedScanPluginPresent = [regex]::IsMatch(
        $failedScanText,
        '(?im)(?:^|[\\/\s])Spectr(?:\.vst3|\.dll)(?:$|[\s\\/])'
    )
}

$observedAcceptance = $null
if (-not [string]::IsNullOrWhiteSpace($ObservedAcceptanceReceipt)) {
    $observedAcceptancePath = (Resolve-Path -LiteralPath $ObservedAcceptanceReceipt).Path
    $observedAcceptance = Get-Content -LiteralPath $observedAcceptancePath -Raw | ConvertFrom-Json
    if ($observedAcceptance.plugin_instance_observed -ne $true) {
        throw "Acceptance receipt does not prove a Spectr instance: $observedAcceptancePath"
    }
    if ($observedAcceptance.failed_scan_list_empty -ne $true) {
        throw "Acceptance receipt does not prove a clean failed-scan list: $observedAcceptancePath"
    }
    if ($observedAcceptance.plugin_sha256 -ne $pluginHash) {
        throw "Acceptance receipt plugin hash does not match $plugin"
    }
    if ($observedAcceptance.reaper_sha256 -ne $reaperHash) {
        throw "Acceptance receipt REAPER hash does not match $ReaperExe"
    }
    if ($null -eq $failedScanPath) {
        throw 'Acceptance receipt requires -FailedScanEvidence proving that Spectr is absent from the failed-scan list'
    }
}

$acceptanceStatus = if ($null -ne $observedAcceptance) { 'accepted' } else { 'blocked' }
$receiptObject = [ordered]@{
    schema = 2
    generated_at_utc = (Get-Date).ToUniversalTime().ToString('o')
    reaper = (Get-Item -LiteralPath $ReaperExe).FullName
    reaper_sha256 = $reaperHash
    architecture = $Architecture
    plugin_path = $plugin
    plugin_sha256 = $pluginHash
    cache_path = $CachePath
    scan_entry = $entry
    scan_entry_present = ($null -ne $entry)
    discovery_status = if ($null -ne $entry) { 'discovered' } else { 'not-discovered' }
    acceptance_status = $acceptanceStatus
    failed_scan_evidence_path = $failedScanPath
    failed_scan_evidence_supplied = ($null -ne $failedScanPath)
    failed_scan_plugin_present = $failedScanPluginPresent
    failed_scan_list_empty = if ($null -eq $failedScanPluginPresent) { $null } else { -not $failedScanPluginPresent }
    plugin_instance_observed = ($null -ne $observedAcceptance)
    limitation = 'The cache entry proves discovery only. REAPER load acceptance requires supplied failed-scan evidence showing Spectr absent and a matching observed host-instance receipt.'
}
$receiptObject | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Receipt -Encoding utf8
if ($null -eq $entry) { throw "REAPER did not record Spectr in $CachePath" }
if ($null -ne $failedScanPluginPresent -and $failedScanPluginPresent) {
    throw "REAPER failed-scan evidence still lists Spectr: $failedScanPath"
}
if ($null -ne $observedAcceptance -and $failedScanPluginPresent -ne $false) {
    throw "Acceptance receipt requires failed-scan evidence that does not list Spectr: $failedScanPath"
}
if ($RequireAcceptance -and $null -eq $observedAcceptance) {
    throw 'REAPER acceptance is blocked: provide -ObservedAcceptanceReceipt with plugin_instance_observed=true and failed_scan_list_empty=true'
}
if ($null -ne $observedAcceptance) {
    Write-Output "PASS: REAPER host acceptance receipt matches $entry"
} else {
    Write-Output "DISCOVERY ONLY: REAPER cache entry: $entry"
    Write-Output 'FAIL-CLOSED: this helper does not claim REAPER load, instantiation, audio, or screenshot acceptance'
}
Write-Output "Receipt: $Receipt"
