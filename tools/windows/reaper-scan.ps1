[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$Vst3Root,
    [string]$ReaperExe = 'C:\Program Files\REAPER (x64)\reaper.exe',
    [string]$Receipt = (Join-Path (Split-Path $Vst3Root -Parent) 'reaper-scan-receipt.json'),
    [int]$WaitSeconds = 20
)

$ErrorActionPreference = 'Stop'
$Vst3Root = (Resolve-Path -LiteralPath $Vst3Root).Path
if (-not (Test-Path -LiteralPath $ReaperExe -PathType Leaf)) { throw "REAPER is missing: $ReaperExe" }
$plugin = Join-Path $Vst3Root 'Spectr.vst3'
if (-not (Test-Path -LiteralPath $plugin -PathType Leaf)) { throw "Packaged VST3 binary is missing: $plugin" }

$reaperConfig = Join-Path $env:APPDATA 'REAPER'
$cache = Join-Path $reaperConfig 'reaper-vstplugins64.ini'
$process = Start-Process -FilePath $ReaperExe -ArgumentList '-nosplash','-new' -PassThru
try {
    Start-Sleep -Seconds $WaitSeconds
    $entry = $null
    if (Test-Path -LiteralPath $cache) { $entry = Get-Content -LiteralPath $cache | Where-Object { $_ -like 'Spectr.dll=*' -or $_ -like 'Spectr.vst3=*' } | Select-Object -First 1 }
    $receiptObject = [ordered]@{
        schema = 1
        generated_at_utc = (Get-Date).ToUniversalTime().ToString('o')
        reaper = (Get-Item -LiteralPath $ReaperExe).FullName
        reaper_process_id = $process.Id
        plugin_path = $plugin
        cache_path = $cache
        scan_entry = $entry
        scan_entry_present = ($null -ne $entry)
        limitation = 'A cache entry proves discovery only; load and audio acceptance require a desktop-capable host session.'
    }
    $receiptObject | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Receipt -Encoding utf8
    if ($null -eq $entry) { throw "REAPER did not record Spectr in $cache" }
    Write-Output "PASS: REAPER scan entry: $entry"
} finally {
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue }
}
