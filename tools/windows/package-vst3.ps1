[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$BuildDir,
    [string]$Destination = '',
    [ValidateSet('x86_64-win', 'arm64-win', 'arm64ec-win')] [string]$Architecture = 'arm64-win',
    [string]$Receipt = ''
)

$ErrorActionPreference = 'Stop'
$validation = Join-Path $PSScriptRoot 'package-validation.ps1'
if (-not (Test-Path -LiteralPath $validation -PathType Leaf)) { throw "Packaging validation helper is missing: $validation" }
. $validation
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
if ([string]::IsNullOrWhiteSpace($Destination)) {
    $Destination = Join-Path $BuildDir 'VST3\\Spectr.vst3'
}
if ([string]::IsNullOrWhiteSpace($Receipt)) {
    $Receipt = Join-Path $BuildDir 'windows-vst3-package.json'
}
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
Assert-SpectrPeArchitecture $binary.FullName $Architecture

$package = [IO.Path]::GetFullPath($Destination)
if (Test-Path -LiteralPath $package) { Remove-Item -LiteralPath $package -Recurse -Force }
$archDir = Join-Path $package "Contents\\$Architecture"
$resourcesDir = Join-Path $package 'Contents\\Resources'
New-Item -ItemType Directory -Force -Path $archDir, $resourcesDir | Out-Null

$targetName = Join-Path $archDir 'Spectr.vst3'
Copy-Item -LiteralPath $binary.FullName -Destination $targetName
foreach ($runtime in @('icudtl.dat', 'wgpu_native.dll')) {
    $runtimeSource = Find-SpectrRuntimeSource $BuildDir $runtime $Architecture
    if ($null -ne $runtimeSource) {
        if ($runtime -eq 'wgpu_native.dll') { Assert-SpectrPeArchitecture $runtimeSource $Architecture }
        Copy-Item -LiteralPath $runtimeSource -Destination (Join-Path $archDir $runtime)
    }
}
if (-not (Test-Path -LiteralPath (Join-Path $archDir 'icudtl.dat') -PathType Leaf)) {
    throw 'Required runtime sidecar was not found: icudtl.dat'
}
$buildInfoSource = Find-SpectrBuildInfoSource $BuildDir $Architecture
if ($null -eq $buildInfoSource) { throw 'Required VST3 metadata sidecar was not found: Spectr.pulp-build-info.json' }
Copy-Item -LiteralPath $buildInfoSource -Destination (Join-Path $archDir 'Spectr.pulp-build-info.json')

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
