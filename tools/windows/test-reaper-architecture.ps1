[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'reaper-architecture.ps1')

function Assert-Equal([string]$Actual, [string]$Expected, [string]$Label) {
    if ($Actual -ne $Expected) { throw "$Label expected '$Expected' but got '$Actual'" }
}

Assert-Equal (Get-ReaperHostArchitecture 'x86_64-win') 'x86_64-win' 'x64 host mapping'
Assert-Equal (Get-ReaperHostArchitecture 'arm64-win') 'arm64-win' 'ARM64 host mapping'
Assert-Equal (Get-ReaperHostArchitecture 'arm64ec-win') 'arm64-win' 'ARM64EC host mapping'

$failed = $false
try { Get-ReaperHostArchitecture 'invalid-win' | Out-Null } catch { $failed = $true }
if (-not $failed) { throw 'Unsupported architecture should fail closed' }

$fixture = Join-Path ([IO.Path]::GetTempPath()) "spectr-reaper-architecture-$([guid]::NewGuid().ToString('N'))"
$oldProgramFiles = $env:ProgramFiles
$oldAppData = $env:APPDATA
try {
    $programFiles = Join-Path $fixture 'Program Files'
    $appData = Join-Path $fixture 'AppData\Roaming'
    $reaperDirectory = Join-Path $programFiles 'REAPER (arm64)'
    $cacheDirectory = Join-Path $appData 'REAPER'
    $vst3Root = Join-Path $fixture 'VST3\Spectr.vst3\Contents\arm64ec-win'
    $receipt = Join-Path $fixture 'scan-receipt.json'
    New-Item -ItemType Directory -Force -Path $reaperDirectory, $cacheDirectory, $vst3Root | Out-Null
    Set-Content -LiteralPath (Join-Path $reaperDirectory 'reaper.exe') -Value 'fake-reaper' -NoNewline
    Set-Content -LiteralPath (Join-Path $cacheDirectory 'reaper-vstplugins64arwmin.ini') -Value 'Spectr.vst3=C:\Spectr\Spectr.vst3' -NoNewline
    Set-Content -LiteralPath (Join-Path $vst3Root 'Spectr.vst3') -Value 'fake-plugin' -NoNewline
    $env:ProgramFiles = $programFiles
    $env:APPDATA = $appData

    & (Join-Path $PSScriptRoot 'reaper-scan.ps1') -Vst3Root $vst3Root -Architecture arm64ec-win -Receipt $receipt | Out-Null
    $scan = Get-Content -LiteralPath $receipt -Raw | ConvertFrom-Json
    Assert-Equal ([string]$scan.architecture) 'arm64ec-win' 'receipt plug-in architecture'
    Assert-Equal ([string]$scan.host_architecture) 'arm64-win' 'receipt host architecture'
    Assert-Equal ([string]$scan.reaper) (Join-Path $reaperDirectory 'reaper.exe') 'native ARM64 REAPER path'
    Assert-Equal ([string]$scan.cache_path) (Join-Path $cacheDirectory 'reaper-vstplugins64arwmin.ini') 'native ARM64 REAPER cache path'

    Remove-Item -LiteralPath (Join-Path $cacheDirectory 'reaper-vstplugins64arwmin.ini')
    Set-Content -LiteralPath (Join-Path $cacheDirectory 'reaper-vstplugins64.ini') -Value 'Spectr.vst3=C:\Spectr\Spectr.vst3' -NoNewline
    $failed = $false
    try {
        & (Join-Path $PSScriptRoot 'reaper-scan.ps1') -Vst3Root $vst3Root -Architecture arm64ec-win -Receipt (Join-Path $fixture 'negative.json') | Out-Null
    } catch { $failed = $true }
    if (-not $failed) { throw 'ARM64EC scan should fail when only the x64 cache exists' }
} finally {
    $env:ProgramFiles = $oldProgramFiles
    $env:APPDATA = $oldAppData
    if (Test-Path -LiteralPath $fixture) { Remove-Item -LiteralPath $fixture -Recurse -Force }
}

Write-Output 'PASS: REAPER architecture mappings and scan defaults'
