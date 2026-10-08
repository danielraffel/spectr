[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$BuildDir,
    [string]$Destination = (Join-Path $BuildDir 'VST3\\Spectr.vst3'),
    [ValidateSet('x86_64-win', 'arm64-win')] [string]$Architecture = 'arm64-win',
    [string]$Receipt = (Join-Path $BuildDir 'windows-vst3-package.json')
)

$ErrorActionPreference = 'Stop'
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
$source = Join-Path $BuildDir 'VST3'
if (-not (Test-Path -LiteralPath $source -PathType Container)) { throw "VST3 output directory is missing: $source" }

$binary = $null
foreach ($candidate in @(
    (Join-Path $source ("Spectr.vst3\\Contents\\$Architecture\\Spectr.vst3")),
    (Join-Path $source 'Spectr.dll')
)) {
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { $binary = Get-Item -LiteralPath $candidate; break }
}
if ($null -eq $binary) { throw "No Spectr VST3 binary found under $source" }

$package = [IO.Path]::GetFullPath($Destination)
if (Test-Path -LiteralPath $package) { Remove-Item -LiteralPath $package -Recurse -Force }
$archDir = Join-Path $package "Contents\\$Architecture"
$resourcesDir = Join-Path $package 'Contents\\Resources'
New-Item -ItemType Directory -Force -Path $archDir, $resourcesDir | Out-Null

$targetName = Join-Path $archDir 'Spectr.vst3'
Copy-Item -LiteralPath $binary.FullName -Destination $targetName
foreach ($runtime in @('icudtl.dat', 'wgpu_native.dll')) {
    $runtimeSource = Join-Path $source $runtime
    if (Test-Path -LiteralPath $runtimeSource -PathType Leaf) {
        Copy-Item -LiteralPath $runtimeSource -Destination (Join-Path $archDir $runtime)
    }
}

$files = @(Get-ChildItem -LiteralPath $package -Recurse -File)
$receiptObject = [ordered]@{
    schema = 1
    generated_at_utc = (Get-Date).ToUniversalTime().ToString('o')
    architecture = $Architecture
    package = $package
    files = @($files | ForEach-Object { [ordered]@{ path = $_.FullName; bytes = $_.Length; sha256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash } })
}
$receiptObject | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Receipt -Encoding utf8
Write-Output "PASS: $package"
Write-Output "Receipt: $Receipt"
