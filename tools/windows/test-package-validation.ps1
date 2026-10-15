[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'package-validation.ps1')

function Write-FakePe {
    param([Parameter(Mandatory = $true)] [string]$Path, [Parameter(Mandatory = $true)] [int]$Machine)
    $bytes = New-Object byte[] 512
    $bytes[0] = 0x4D; $bytes[1] = 0x5A
    [BitConverter]::GetBytes([int]0x80).CopyTo($bytes, 0x3C)
    $bytes[0x80] = 0x50; $bytes[0x81] = 0x45
    [BitConverter]::GetBytes([UInt16]$Machine).CopyTo($bytes, 0x84)
    New-Item -ItemType Directory -Force -Path (Split-Path $Path -Parent) | Out-Null
    [IO.File]::WriteAllBytes($Path, $bytes)
}

function Assert-Fails {
    param([Parameter(Mandatory = $true)] [scriptblock]$Action, [Parameter(Mandatory = $true)] [string]$Name)
    try { & $Action } catch { Write-Output "PASS: rejected $Name"; return }
    throw "Expected validation failure was accepted: $Name"
}

$temp = Join-Path ([IO.Path]::GetTempPath()) ("spectr-package-test-" + [guid]::NewGuid().ToString('N'))
$build = Join-Path $temp 'build'
$package = Join-Path $temp 'Spectr-test.zip'
$installRoot = Join-Path $temp 'install'
New-Item -ItemType Directory -Force -Path $build | Out-Null
try {
    $machine = Get-SpectrMachineForArchitecture 'arm64-win'
    Write-FakePe (Join-Path $build 'Spectr.exe') $machine
    Write-FakePe (Join-Path $build 'VST3\Spectr.vst3\Contents\arm64-win\Spectr.vst3') $machine
    Write-FakePe (Join-Path $build 'CLAP\Spectr.clap') $machine

    & (Join-Path $PSScriptRoot 'package-release.ps1') -BuildDir $build -Architecture arm64-win -Output $package | Out-Null
    $expanded = Join-Path $temp 'expanded'
    Expand-Archive -LiteralPath $package -DestinationPath $expanded
    $manifest = Get-Content (Join-Path $expanded 'manifest.json') -Raw | ConvertFrom-Json
    if ((Assert-SpectrPackageManifest $expanded $manifest) -ne 'arm64-win') { throw 'Package architecture was not preserved' }
    Write-Output 'PASS: generated package has a verified ARM64 manifest and PE payloads'

    $tampered = $manifest | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    $tampered.files[0].sha256 = ('0' * 64)
    Assert-Fails { Assert-SpectrPackageManifest $expanded $tampered } 'tampered manifest hash'

    $wrongArchitecture = $manifest | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    $wrongArchitecture.architecture = 'x86_64-win'
    Assert-Fails { Assert-SpectrPackageManifest $expanded $wrongArchitecture } 'architecture/path mismatch'

    $traversal = $manifest | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    $traversal.files[0].path = '..\outside.exe'
    Assert-Fails { Assert-SpectrPackageManifest $expanded $traversal } 'manifest traversal path'

    Set-Content -LiteralPath (Join-Path $expanded 'unlisted.dll') -Value 'unlisted' -NoNewline
    Assert-Fails { Assert-SpectrPackageManifest $expanded $manifest } 'unlisted extracted payload'

    New-Item -ItemType Directory -Force -Path (Join-Path $expanded 'nested') | Out-Null
    Set-Content -LiteralPath (Join-Path $expanded 'nested\manifest.json') -Value '{}'
    Assert-Fails { Assert-SpectrPackageManifest $expanded $manifest } 'unlisted nested manifest'

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $maliciousPackage = Join-Path $temp 'malicious-traversal.zip'
    $outsideMarker = Join-Path ([IO.Path]::GetTempPath()) 'spectr-package-validation-outside-marker.txt'
    if (Test-Path -LiteralPath $outsideMarker) { Remove-Item -LiteralPath $outsideMarker -Force }
    $maliciousArchive = [IO.Compression.ZipFile]::Open($maliciousPackage, [IO.Compression.ZipArchiveMode]::Create)
    try {
        $maliciousEntry = $maliciousArchive.CreateEntry('..\spectr-package-validation-outside-marker.txt')
        $writer = New-Object IO.StreamWriter($maliciousEntry.Open())
        try { $writer.Write('must not be extracted') } finally { $writer.Dispose() }
    } finally { $maliciousArchive.Dispose() }
    Assert-Fails { & (Join-Path $PSScriptRoot 'install-package.ps1') -Package $maliciousPackage -Vst3Root (Join-Path $installRoot 'VST3') -ClapRoot (Join-Path $installRoot 'CLAP') -StandaloneRoot (Join-Path $installRoot 'Standalone') -NoShortcuts } 'ZIP traversal entry'
    if (Test-Path -LiteralPath $outsideMarker) { throw 'ZIP traversal wrote outside the extraction stage' }

    & (Join-Path $PSScriptRoot 'install-package.ps1') -Package $package -Vst3Root (Join-Path $installRoot 'VST3') -ClapRoot (Join-Path $installRoot 'CLAP') -StandaloneRoot (Join-Path $installRoot 'Standalone') -NoShortcuts | Out-Null
    foreach ($path in @((Join-Path $installRoot 'VST3\Spectr.vst3'), (Join-Path $installRoot 'CLAP\Spectr.clap'), (Join-Path $installRoot 'Standalone\Spectr.exe'))) {
        if (-not (Test-Path -LiteralPath $path)) { throw "Installer did not install $path" }
    }
    Write-Output 'PASS: installer verifies and installs the generated package without shortcuts'
} finally {
    if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Recurse -Force }
}
