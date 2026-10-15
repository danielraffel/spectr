[CmdletBinding()]
param(
    [string]$Arm64EcBinary = '',
    [string]$X64Binary = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'package-validation.ps1')

function Write-FakePe {
    param([Parameter(Mandatory = $true)] [string]$Path, [Parameter(Mandatory = $true)] [int]$Machine, [switch]$Arm64X)
    $bytes = New-Object byte[] 512
    $bytes[0] = 0x4D; $bytes[1] = 0x5A
    [BitConverter]::GetBytes([int]0x80).CopyTo($bytes, 0x3C)
    $bytes[0x80] = 0x50; $bytes[0x81] = 0x45
    [BitConverter]::GetBytes([UInt16]$Machine).CopyTo($bytes, 0x84)
    if ($Arm64X) {
        [BitConverter]::GetBytes([UInt16]1).CopyTo($bytes, 0x86)
        [Text.Encoding]::ASCII.GetBytes('.a64xrm').CopyTo($bytes, 0x98)
    }
    New-Item -ItemType Directory -Force -Path (Split-Path $Path -Parent) | Out-Null
    [IO.File]::WriteAllBytes($Path, $bytes)
}

function Assert-Fails {
    param([Parameter(Mandatory = $true)] [scriptblock]$Action, [Parameter(Mandatory = $true)] [string]$Name, [string]$ExpectedText = '')
    try { & $Action } catch {
        if (-not [string]::IsNullOrWhiteSpace($ExpectedText) -and $_.Exception.Message -notlike "*$ExpectedText*") { throw "Wrong failure for $Name`: $($_.Exception.Message)" }
        Write-Output "PASS: rejected $Name"; return
    }
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
    Set-Content -LiteralPath (Join-Path $build 'icudtl.dat') -Value 'synthetic ICU sidecar' -NoNewline
    New-Item -ItemType Directory -Force -Path (Join-Path $build 'VST3\Release') | Out-Null
    Set-Content -LiteralPath (Join-Path $build 'VST3\Release\Spectr.pulp-build-info.json') -Value '{"schema":"pulp.build-info.v1","product":{"source_git_sha":"c064d1e146cc8a7ed274286e45aa6990e25cb426"},"build":{"archs":["arm64-win"]}}' -NoNewline
    if (-not [string]::IsNullOrWhiteSpace($Arm64EcBinary)) {
        Assert-SpectrPeArchitecture ((Resolve-Path -LiteralPath $Arm64EcBinary).Path) 'arm64ec-win'
        Write-Output 'PASS: real ARM64X/ARM64EC PE positive accepted'
    }
    if (-not [string]::IsNullOrWhiteSpace($X64Binary)) {
        Assert-SpectrPeArchitecture ((Resolve-Path -LiteralPath $X64Binary).Path) 'x86_64-win'
        Assert-Fails { Assert-SpectrPeArchitecture ((Resolve-Path -LiteralPath $X64Binary).Path) 'arm64ec-win' } 'ordinary x64 as ARM64EC'
    }
    $arm64xArm64 = Join-Path $temp 'arm64x-arm64.vst3'
    Write-FakePe $arm64xArm64 0xAA64 -Arm64X
    Assert-SpectrPeArchitecture $arm64xArm64 'arm64ec-win'
    Write-Output 'PASS: synthetic AA64 ARM64X PE positive accepted'

    Set-Content -LiteralPath (Join-Path $build 'wgpu_native.dll') -Value 'synthetic runtime' -NoNewline
    Assert-Fails { & (Join-Path $PSScriptRoot 'package-release.ps1') -BuildDir $build -Architecture arm64-win -Vst3Architecture arm64ec-win -Output (Join-Path $temp 'mixed-wgpu.zip') | Out-Null } 'mixed architecture shared wgpu runtime'
    Remove-Item -LiteralPath (Join-Path $build 'wgpu_native.dll') -Force

    $sourceBuildInfo = Join-Path $build 'VST3\Release\Spectr.pulp-build-info.json'
    $sourceBuildInfoOriginal = Get-Content $sourceBuildInfo -Raw
    Set-Content -LiteralPath $sourceBuildInfo -Value '{"schema":"wrong"}' -NoNewline
    $invalidReleaseOutput = Join-Path $temp 'invalid-build-info.zip'
    Assert-Fails { & (Join-Path $PSScriptRoot 'package-release.ps1') -BuildDir $build -Architecture arm64-win -Output $invalidReleaseOutput | Out-Null } 'package-release invalid build-info' 'unsupported schema'
    if (Test-Path -LiteralPath $invalidReleaseOutput) { throw 'package-release wrote output before validating build-info' }
    $invalidVst3Output = Join-Path $temp 'invalid-build-info.vst3'
    $invalidVst3Receipt = Join-Path $temp 'invalid-build-info-receipt.json'
    Assert-Fails { & (Join-Path $PSScriptRoot 'package-vst3.ps1') -BuildDir $build -Destination $invalidVst3Output -Receipt $invalidVst3Receipt | Out-Null } 'package-vst3 invalid build-info' 'unsupported schema'
    if (Test-Path -LiteralPath $invalidVst3Output) { throw 'package-vst3 wrote output before validating build-info' }
    if (Test-Path -LiteralPath $invalidVst3Receipt) { throw 'package-vst3 wrote receipt before validating build-info' }
    Set-Content -LiteralPath $sourceBuildInfo -Value $sourceBuildInfoOriginal -NoNewline

    & (Join-Path $PSScriptRoot 'package-release.ps1') -BuildDir $build -Architecture arm64-win -Output $package | Out-Null
    $expanded = Join-Path $temp 'expanded'
    Expand-Archive -LiteralPath $package -DestinationPath $expanded
    $manifest = Get-Content (Join-Path $expanded 'manifest.json') -Raw | ConvertFrom-Json
    $buildInfoPath = Join-Path $expanded 'VST3\Spectr.vst3\Contents\arm64-win\Spectr.pulp-build-info.json'
    $buildInfoOriginal = Get-Content $buildInfoPath -Raw
    if ((Assert-SpectrPackageManifest $expanded $manifest) -ne 'arm64-win') { throw 'Package architecture was not preserved' }
    Write-Output 'PASS: generated package has a verified ARM64 manifest and PE payloads'

    $tampered = $manifest | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    $tampered.files[0].sha256 = ('0' * 64)
    Assert-Fails { Assert-SpectrPackageManifest $expanded $tampered } 'tampered manifest hash'

    $wrongArchitecture = $manifest | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    $wrongArchitecture.artifact_architectures.vst3 = 'x86_64-win'
    Assert-Fails { Assert-SpectrPackageManifest $expanded $wrongArchitecture } 'architecture/path mismatch'

    $traversal = $manifest | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    $traversal.files[0].path = '..\outside.exe'
    Assert-Fails { Assert-SpectrPackageManifest $expanded $traversal } 'manifest traversal path'

    $missingRuntime = $manifest | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    $missingRuntime.runtime_sidecars = @()
    Assert-Fails { Assert-SpectrPackageManifest $expanded $missingRuntime } 'missing required ICU sidecar'
    $missingMetadata = $manifest | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    $missingMetadata.metadata_sidecars = @()
    Assert-Fails { Assert-SpectrPackageManifest $expanded $missingMetadata } 'missing required build-info sidecar' 'required Spectr.pulp-build-info.json'
    $wrongSchema = $manifest | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    Set-Content -LiteralPath $buildInfoPath -Value '{"schema":"wrong","product":{"source_git_sha":"c064d1e146cc8a7ed274286e45aa6990e25cb426"},"build":{"archs":["arm64-win"]}}' -NoNewline
    foreach ($entry in @($wrongSchema.files)) {
        if ($entry.path -eq 'VST3\Spectr.vst3\Contents\arm64-win\Spectr.pulp-build-info.json') {
            $entry.bytes = (Get-Item $buildInfoPath).Length
            $entry.sha256 = (Get-FileHash $buildInfoPath -Algorithm SHA256).Hash
        }
    }
    Assert-Fails { Assert-SpectrPackageManifest $expanded $wrongSchema } 'build-info schema mismatch' 'unsupported schema'
    $wrongSha = $manifest | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    Set-Content -LiteralPath $buildInfoPath -Value '{"schema":"pulp.build-info.v1","product":{"source_git_sha":"bad"},"build":{"archs":["arm64-win"]}}' -NoNewline
    foreach ($entry in @($wrongSha.files)) {
        if ($entry.path -eq 'VST3\Spectr.vst3\Contents\arm64-win\Spectr.pulp-build-info.json') {
            $entry.bytes = (Get-Item $buildInfoPath).Length
            $entry.sha256 = (Get-FileHash $buildInfoPath -Algorithm SHA256).Hash
        }
    }
    Assert-Fails { Assert-SpectrPackageManifest $expanded $wrongSha } 'build-info source SHA mismatch' 'source_git_sha provenance'
    $wrongBuildInfo = $manifest | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    Set-Content -LiteralPath (Join-Path $expanded 'VST3\Spectr.vst3\Contents\arm64-win\Spectr.pulp-build-info.json') -Value '{"schema":"pulp.build-info.v1","product":{"source_git_sha":"c064d1e146cc8a7ed274286e45aa6990e25cb426"},"build":{"archs":["x86_64-win"]}}' -NoNewline
    foreach ($entry in @($wrongBuildInfo.files)) {
        if ($entry.path -eq 'VST3\Spectr.vst3\Contents\arm64-win\Spectr.pulp-build-info.json') {
            $entry.bytes = (Get-Item (Join-Path $expanded $entry.path)).Length
            $entry.sha256 = (Get-FileHash (Join-Path $expanded $entry.path) -Algorithm SHA256).Hash
        }
    }
    Assert-Fails { Assert-SpectrPackageManifest $expanded $wrongBuildInfo } 'build-info architecture mismatch' 'build.archs does not include arm64-win'
    Set-Content -LiteralPath $buildInfoPath -Value $buildInfoOriginal -NoNewline

    $wrongArtifacts = $manifest | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    $wrongArtifacts.artifact_architectures.clap = 'x86_64-win'
    Assert-Fails { Assert-SpectrPackageManifest $expanded $wrongArtifacts } 'standalone/CLAP architecture mismatch' 'standalone and CLAP architectures'

    Set-Content -LiteralPath (Join-Path $expanded 'unlisted.dll') -Value 'unlisted' -NoNewline
    Assert-Fails { Assert-SpectrPackageManifest $expanded $manifest } 'unlisted extracted payload' 'Unlisted package file'

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
