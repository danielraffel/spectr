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

    $multiBuild = Join-Path $temp 'multi-config-build'
    New-Item -ItemType Directory -Force -Path (Join-Path $multiBuild 'Release'), (Join-Path $multiBuild 'CLAP\Release') | Out-Null
    Set-Content -LiteralPath (Join-Path $multiBuild 'CMakeCache.txt') -Value 'CMAKE_GENERATOR:INTERNAL=Visual Studio 18 2026' -NoNewline
    Copy-Item -LiteralPath (Join-Path $build 'Spectr.exe') -Destination (Join-Path $multiBuild 'Release\Spectr.exe')
    Copy-Item -LiteralPath (Join-Path $build 'CLAP\Spectr.clap') -Destination (Join-Path $multiBuild 'CLAP\Release\Spectr.clap')
    Copy-Item -LiteralPath (Join-Path $build 'VST3') -Destination (Join-Path $multiBuild 'VST3') -Recurse
    Copy-Item -LiteralPath (Join-Path $build 'icudtl.dat') -Destination (Join-Path $multiBuild 'VST3\Release\icudtl.dat')
    $multiPackage = Join-Path $temp 'multi-config.zip'
    & (Join-Path $PSScriptRoot 'package-release.ps1') -BuildDir $multiBuild -Architecture arm64-win -Output $multiPackage | Out-Null
    $multiExpanded = Join-Path $temp 'multi-config-expanded'
    Expand-Archive -LiteralPath $multiPackage -DestinationPath $multiExpanded
    if ((Assert-SpectrPackageManifest $multiExpanded (Get-Content (Join-Path $multiExpanded 'manifest.json') -Raw | ConvertFrom-Json)) -ne 'arm64-win') {
        throw 'Visual Studio Release layout did not produce a verified package'
    }
    Write-Output 'PASS: Visual Studio Release layout packages and validates'

    Copy-Item -LiteralPath (Join-Path $build 'Spectr.exe') -Destination (Join-Path $multiBuild 'Spectr.exe')
    Remove-Item -LiteralPath (Join-Path $multiBuild 'Release\Spectr.exe') -Force
    Assert-Fails { & (Join-Path $PSScriptRoot 'package-release.ps1') -BuildDir $multiBuild -Architecture arm64-win -Output (Join-Path $temp 'stale-root.zip') | Out-Null } 'multi-config stale-root fallback' 'Missing build artifact for'
    Copy-Item -LiteralPath (Join-Path $build 'Spectr.exe') -Destination (Join-Path $multiBuild 'Release\Spectr.exe')
    Remove-Item -LiteralPath (Join-Path $multiBuild 'CMakeCache.txt') -Force
    Assert-Fails { & (Join-Path $PSScriptRoot 'package-release.ps1') -BuildDir $multiBuild -Architecture arm64-win -Output (Join-Path $temp 'ambiguous.zip') | Out-Null } 'cacheless ambiguous layout' 'Ambiguous staged build artifact'

    $tampered = $manifest | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    $tampered.files[0].sha256 = ('0' * 64)
    Assert-Fails { Assert-SpectrPackageManifest $expanded $tampered } 'tampered manifest hash'

    $wrongArchitecture = $manifest | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    $wrongArchitecture.artifact_architectures.vst3 = 'x86_64-win'
    Assert-Fails { Assert-SpectrPackageManifest $expanded $wrongArchitecture } 'architecture/path mismatch'

    $traversal = $manifest | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    $traversal.files[0].path = '..\outside.exe'
    Assert-Fails { Assert-SpectrPackageManifest $expanded $traversal } 'manifest traversal path'
    Assert-Fails { Get-SpectrPackagePath $expanded 'C:\outside.exe' } 'Windows absolute manifest path' 'Manifest path must be relative'
    Assert-Fails { Get-SpectrPackagePath $expanded '\\server\share\outside.exe' } 'UNC manifest path' 'Manifest path must be relative'

    $missingRuntime = $manifest | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    $missingRuntime.runtime_sidecars = @()
    Assert-Fails { Assert-SpectrPackageManifest $expanded $missingRuntime } 'missing required ICU sidecar'
    $missingMetadata = $manifest | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    $missingMetadata.metadata_sidecars = @()
    Assert-Fails { Assert-SpectrPackageManifest $expanded $missingMetadata } 'missing required build-info sidecar' 'required Spectr.pulp-build-info.json'
    $wrongSchema = $manifest | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    Set-Content -LiteralPath $buildInfoPath -Value '{"schema":"wrong","product":{"source_git_sha":"c064d1e146cc8a7ed274286e45aa6990e25cb426"},"build":{"archs":["arm64-win"]}}' -NoNewline
    foreach ($entry in @($wrongSchema.files)) {
        if ($entry.path.Replace('/', '\') -eq 'VST3\Spectr.vst3\Contents\arm64-win\Spectr.pulp-build-info.json') {
            $entry.bytes = (Get-Item $buildInfoPath).Length
            $entry.sha256 = (Get-FileHash $buildInfoPath -Algorithm SHA256).Hash
        }
    }
    Assert-Fails { Assert-SpectrPackageManifest $expanded $wrongSchema } 'build-info schema mismatch' 'unsupported schema'
    $wrongSha = $manifest | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    Set-Content -LiteralPath $buildInfoPath -Value '{"schema":"pulp.build-info.v1","product":{"source_git_sha":"bad"},"build":{"archs":["arm64-win"]}}' -NoNewline
    foreach ($entry in @($wrongSha.files)) {
        if ($entry.path.Replace('/', '\') -eq 'VST3\Spectr.vst3\Contents\arm64-win\Spectr.pulp-build-info.json') {
            $entry.bytes = (Get-Item $buildInfoPath).Length
            $entry.sha256 = (Get-FileHash $buildInfoPath -Algorithm SHA256).Hash
        }
    }
    Assert-Fails { Assert-SpectrPackageManifest $expanded $wrongSha } 'build-info source SHA mismatch' 'source_git_sha provenance'
    $wrongBuildInfo = $manifest | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    Set-Content -LiteralPath (Join-Path $expanded 'VST3\Spectr.vst3\Contents\arm64-win\Spectr.pulp-build-info.json') -Value '{"schema":"pulp.build-info.v1","product":{"source_git_sha":"c064d1e146cc8a7ed274286e45aa6990e25cb426"},"build":{"archs":["x86_64-win"]}}' -NoNewline
    foreach ($entry in @($wrongBuildInfo.files)) {
        if ($entry.path.Replace('/', '\') -eq 'VST3\Spectr.vst3\Contents\arm64-win\Spectr.pulp-build-info.json') {
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

    $installReceipt = Join-Path $temp 'install-receipt.json'
    & (Join-Path $PSScriptRoot 'install-package.ps1') -Package $package -Vst3Root (Join-Path $installRoot 'VST3') -ClapRoot (Join-Path $installRoot 'CLAP') -StandaloneRoot (Join-Path $installRoot 'Standalone') -Receipt $installReceipt -NoShortcuts | Out-Null
    foreach ($path in @((Join-Path $installRoot 'VST3\Spectr.vst3'), (Join-Path $installRoot 'CLAP\Spectr.clap'), (Join-Path $installRoot 'Standalone\Spectr.exe'))) {
        if (-not (Test-Path -LiteralPath $path)) { throw "Installer did not install $path" }
    }
    $receipt = Get-Content -LiteralPath $installReceipt -Raw | ConvertFrom-Json
    if ($receipt.schema -ne 'spectr.windows.install.v1' -or $receipt.status -ne 'installed_files_match_manifest') { throw 'Install receipt has wrong schema or status' }
    if ($receipt.package.sha256 -ne (Get-FileHash $package -Algorithm SHA256).Hash) { throw 'Install receipt package hash differs' }
    if ($receipt.package.manifest_sha256 -ne (Get-FileHash (Join-Path $expanded 'manifest.json') -Algorithm SHA256).Hash) {
        throw 'Install receipt manifest hash differs'
    }
    $installed = @($receipt.installed_files)
    if ($installed.Count -lt 4) { throw 'Install receipt omitted VST3, metadata, CLAP or standalone files' }
    foreach ($file in $installed) {
        if (-not (Test-Path -LiteralPath $file.destination -PathType Leaf)) { throw "Install receipt names a missing destination: $($file.destination)" }
        if ($file.sha256 -ne (Get-FileHash -LiteralPath $file.destination -Algorithm SHA256).Hash -or
            $file.bytes -ne (Get-Item -LiteralPath $file.destination).Length) {
            throw "Install receipt hash or size differs at destination: $($file.destination)"
        }
        $manifestEntry = @($manifest.files | Where-Object { $_.path -eq $file.package_path })
        if ($manifestEntry.Count -ne 1 -or $manifestEntry[0].sha256 -ne $file.sha256) { throw "Install receipt is not tied to manifest: $($file.package_path)" }
    }
    Write-Output 'PASS: installer emits destination hashes tied to the package manifest'

    # The default receipt path is part of the routine one-command installer
    # flow. Keep this unqualified so a PowerShell property-expression typo
    # cannot collapse the receipt to the current drive root.
    $defaultPackage = Join-Path $temp 'default-receipt-package.zip'
    Copy-Item -LiteralPath $package -Destination $defaultPackage
    $defaultInstallRoot = Join-Path $temp 'default-receipt-install'
    $defaultReceipt = "${defaultPackage}.install-receipt.json"
    & (Join-Path $PSScriptRoot 'install-package.ps1') -Package $defaultPackage `
        -Vst3Root (Join-Path $defaultInstallRoot 'VST3') -ClapRoot (Join-Path $defaultInstallRoot 'CLAP') `
        -StandaloneRoot (Join-Path $defaultInstallRoot 'Standalone') -NoShortcuts | Out-Null
    if (-not (Test-Path -LiteralPath $defaultReceipt -PathType Leaf)) {
        throw "Installer default receipt was not written beside the package: $defaultReceipt"
    }
    Write-Output 'PASS: installer default receipt is written beside the package'

    $badPackage = Join-Path $temp 'missing-manifest.zip'
    Compress-Archive -Path (Join-Path $build 'icudtl.dat') -DestinationPath $badPackage
    Assert-Fails { & (Join-Path $PSScriptRoot 'install-package.ps1') -Package $badPackage -Vst3Root (Join-Path $installRoot 'VST3') -ClapRoot (Join-Path $installRoot 'CLAP') -StandaloneRoot (Join-Path $installRoot 'Standalone') -Receipt $installReceipt -NoShortcuts | Out-Null } 'invalid package after successful install' 'Package manifest.json is missing'
    if (Test-Path -LiteralPath $installReceipt) { throw 'Failed retry left a stale success receipt' }
    Write-Output 'PASS: failed retry removes stale install receipt'

    $corruptRoot = Join-Path $temp 'corrupt-install'
    $corruptReceipt = Join-Path $temp 'corrupt-install-receipt.json'
    $global:SpectrTestCorruptDestination = Join-Path $corruptRoot 'CLAP\Spectr.clap'
    function Copy-Item {
        param([string]$LiteralPath, [string]$Destination, [switch]$Recurse, [switch]$Force)
        Microsoft.PowerShell.Management\Copy-Item @PSBoundParameters
        if ($Destination.StartsWith(($global:SpectrTestCorruptDestination + '.'), [StringComparison]::OrdinalIgnoreCase) -and
            $Destination.EndsWith('.new', [StringComparison]::OrdinalIgnoreCase)) {
            Set-Content -LiteralPath $Destination -Value 'corrupted during copy' -NoNewline
        }
    }
    try {
        Assert-Fails {
            & (Join-Path $PSScriptRoot 'install-package.ps1') -Package $package `
                -Vst3Root (Join-Path $corruptRoot 'VST3') -ClapRoot (Join-Path $corruptRoot 'CLAP') `
                -StandaloneRoot (Join-Path $corruptRoot 'Standalone') -Receipt $corruptReceipt -NoShortcuts | Out-Null
        } 'destination changed during copy' 'Staged file differs from package manifest'
    } finally {
        Remove-Item Function:\Copy-Item
        Remove-Variable SpectrTestCorruptDestination -Scope Global
    }
    if (Test-Path -LiteralPath $corruptReceipt) { throw 'Installer wrote success receipt after destination corruption' }
    Write-Output 'PASS: destination corruption fails closed before receipt publication'

    $oldFiles = @(
        (Join-Path $installRoot 'VST3\Spectr.vst3\Contents\arm64-win\Spectr.vst3'),
        (Join-Path $installRoot 'CLAP\Spectr.clap'),
        (Join-Path $installRoot 'Standalone\Spectr.exe')
    )
    $oldHashes = @()
    for ($index = 0; $index -lt $oldFiles.Count; $index++) {
        Set-Content -LiteralPath $oldFiles[$index] -Value "prior install $index" -NoNewline
        $oldHashes += (Get-FileHash -LiteralPath $oldFiles[$index] -Algorithm SHA256).Hash
    }
    $global:SpectrTestFailSwap = Join-Path $installRoot 'CLAP\Spectr.clap'
    function Move-Item {
        param([string]$LiteralPath, [string]$Destination, [switch]$Force)
        if ($Destination -eq $global:SpectrTestFailSwap -and $LiteralPath.EndsWith('.new')) {
            throw 'Injected failure after VST3 swap'
        }
        Microsoft.PowerShell.Management\Move-Item @PSBoundParameters
    }
    try {
        Assert-Fails {
            & (Join-Path $PSScriptRoot 'install-package.ps1') -Package $package `
                -Vst3Root (Join-Path $installRoot 'VST3') -ClapRoot (Join-Path $installRoot 'CLAP') `
                -StandaloneRoot (Join-Path $installRoot 'Standalone') -Receipt $installReceipt -NoShortcuts | Out-Null
        } 'mid-swap copy failure' 'Injected failure after VST3 swap'
    } finally {
        Remove-Item Function:\Move-Item
        Remove-Variable SpectrTestFailSwap -Scope Global
    }
    for ($index = 0; $index -lt $oldFiles.Count; $index++) {
        if ((Get-FileHash -LiteralPath $oldFiles[$index] -Algorithm SHA256).Hash -ne $oldHashes[$index]) {
            throw "Previous installed artifact was not restored: $($oldFiles[$index])"
        }
    }
    if (Test-Path -LiteralPath $installReceipt) { throw 'Mid-swap failure wrote a success receipt' }
    if (Test-Path -LiteralPath "$installReceipt.pending.json") { throw 'Mid-swap failure left a pending journal' }
    if (@(Get-ChildItem -LiteralPath $installRoot -Recurse -Force | Where-Object { $_.Name -match '\.[0-9a-f]{32}\.(new|backup)$' }).Count -ne 0) {
        throw 'Mid-swap failure left staged or backup artifacts'
    }
    Write-Output 'PASS: mid-swap failure restores previous VST3, CLAP and standalone bytes'

    $interruptedId = [guid]::NewGuid().ToString('N')
    $interruptedVst3 = Join-Path $installRoot 'VST3\Spectr.vst3'
    Move-Item -LiteralPath $interruptedVst3 -Destination "$interruptedVst3.$interruptedId.backup"
    Copy-Item -LiteralPath (Join-Path $expanded 'VST3\Spectr.vst3') -Destination $interruptedVst3 -Recurse
    $interruptedJournal = [ordered]@{
        schema = 'spectr.windows.install.pending.v1'
        id = $interruptedId
        destinations = @(
            [ordered]@{ path = [IO.Path]::GetFullPath($interruptedVst3); had_original = $true },
            [ordered]@{ path = [IO.Path]::GetFullPath((Join-Path $installRoot 'CLAP\Spectr.clap')); had_original = $true },
            [ordered]@{ path = [IO.Path]::GetFullPath((Join-Path $installRoot 'Standalone\Spectr.exe')); had_original = $true }
        )
    }
    $interruptedJournal | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$installReceipt.pending.json"
    Assert-Fails {
        & (Join-Path $PSScriptRoot 'install-package.ps1') -Package $badPackage `
            -Vst3Root (Join-Path $installRoot 'VST3') -ClapRoot (Join-Path $installRoot 'CLAP') `
            -StandaloneRoot (Join-Path $installRoot 'Standalone') -Receipt $installReceipt -NoShortcuts | Out-Null
    } 'recovery before invalid package validation' 'Package manifest.json is missing'
    for ($index = 0; $index -lt $oldFiles.Count; $index++) {
        if ((Get-FileHash -LiteralPath $oldFiles[$index] -Algorithm SHA256).Hash -ne $oldHashes[$index]) {
            throw "Interrupted swap did not restore prior artifact: $($oldFiles[$index])"
        }
    }
    if (Test-Path -LiteralPath "$installReceipt.pending.json" -PathType Leaf) { throw 'Interrupted swap journal was not cleared after recovery' }
    if (Test-Path -LiteralPath "$interruptedVst3.$interruptedId.backup") { throw 'Interrupted swap backup was not cleared after recovery' }
    Write-Output 'PASS: interrupted swap journal restores previous installed bytes on retry'

    # A same-ID receipt whose hashes are valid at unrelated paths must not
    # authorize deletion of the real install backup during recovery.
    $foreignId = [guid]::NewGuid().ToString('N')
    $foreignRoot = Join-Path $temp 'foreign-receipt-files'
    Move-Item -LiteralPath $interruptedVst3 -Destination "$interruptedVst3.$foreignId.backup"
    Copy-Item -LiteralPath (Join-Path $expanded 'VST3\Spectr.vst3') -Destination $interruptedVst3 -Recurse
    $foreignReceipt = $receipt | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    $foreignReceipt.transaction_id = $foreignId
    for ($index = 0; $index -lt @($foreignReceipt.installed_files).Count; $index++) {
        $file = $foreignReceipt.installed_files[$index]
        $alternate = Join-Path $foreignRoot ("file-$index")
        New-Item -ItemType Directory -Force -Path $foreignRoot | Out-Null
        Copy-Item -LiteralPath (Join-Path $expanded ([string]$file.package_path).Replace('/', [IO.Path]::DirectorySeparatorChar)) -Destination $alternate
        $file.destination = $alternate
    }
    $foreignJournal = [ordered]@{
        schema = 'spectr.windows.install.pending.v1'
        id = $foreignId
        destinations = $interruptedJournal.destinations
        package = $receipt.package
        installed_files = $receipt.installed_files
    }
    $foreignJournal | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$installReceipt.pending.json"
    $foreignReceipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $installReceipt
    Assert-Fails {
        & (Join-Path $PSScriptRoot 'install-package.ps1') -Package $badPackage `
            -Vst3Root (Join-Path $installRoot 'VST3') -ClapRoot (Join-Path $installRoot 'CLAP') `
            -StandaloneRoot (Join-Path $installRoot 'Standalone') -Receipt $installReceipt -NoShortcuts | Out-Null
    } 'foreign receipt destination during pending recovery' 'Package manifest.json is missing'
    if ((Get-FileHash -LiteralPath $oldFiles[0] -Algorithm SHA256).Hash -ne $oldHashes[0] -or
        (Test-Path -LiteralPath "$interruptedVst3.$foreignId.backup") -or
        (Test-Path -LiteralPath "$installReceipt.pending.json") -or
        (Test-Path -LiteralPath $installReceipt)) {
        throw 'Foreign receipt incorrectly committed the pending install or failed to restore its backup'
    }
    Write-Output 'PASS: foreign-path receipt cannot commit an interrupted transaction'

    $committedRoot = Join-Path $temp 'committed-recovery'
    $committedReceiptPath = Join-Path $temp 'committed-recovery-receipt.json'
    $committedArgs = @{
        Package = $package
        Vst3Root = (Join-Path $committedRoot 'VST3')
        ClapRoot = (Join-Path $committedRoot 'CLAP')
        StandaloneRoot = (Join-Path $committedRoot 'Standalone')
        Receipt = $committedReceiptPath
        NoShortcuts = $true
    }
    & (Join-Path $PSScriptRoot 'install-package.ps1') @committedArgs | Out-Null
    $committedReceipt = Get-Content -LiteralPath $committedReceiptPath -Raw | ConvertFrom-Json
    $committedVst3 = Join-Path $committedRoot 'VST3\Spectr.vst3'
    $committedBackup = "$committedVst3.$($committedReceipt.transaction_id).backup"
    Copy-Item -LiteralPath $committedVst3 -Destination $committedBackup -Recurse
    Set-Content -LiteralPath (Join-Path $committedBackup 'Contents\arm64-win\Spectr.vst3') -Value 'prior install' -NoNewline
    $committedJournal = [ordered]@{
        schema = 'spectr.windows.install.pending.v1'
        id = $committedReceipt.transaction_id
        destinations = @(
            [ordered]@{ path = [IO.Path]::GetFullPath($committedVst3); had_original = $true },
            [ordered]@{ path = [IO.Path]::GetFullPath((Join-Path $committedRoot 'CLAP\Spectr.clap')); had_original = $true },
            [ordered]@{ path = [IO.Path]::GetFullPath((Join-Path $committedRoot 'Standalone\Spectr.exe')); had_original = $true }
        )
        package = $committedReceipt.package
        installed_files = $committedReceipt.installed_files
    }
    $committedJournal | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$committedReceiptPath.pending.json"
    Assert-Fails {
        & (Join-Path $PSScriptRoot 'install-package.ps1') -Package $badPackage `
            -Vst3Root $committedArgs.Vst3Root -ClapRoot $committedArgs.ClapRoot `
            -StandaloneRoot $committedArgs.StandaloneRoot -Receipt $committedReceiptPath -NoShortcuts | Out-Null
    } 'validated committed receipt before invalid retry' 'Package manifest.json is missing'
    $committedBinary = Join-Path $committedVst3 'Contents\arm64-win\Spectr.vst3'
    $committedExpected = @($committedReceipt.installed_files | Where-Object { $_.destination -eq $committedBinary })
    if ($committedExpected.Count -ne 1 -or
        (Get-FileHash -LiteralPath $committedBinary -Algorithm SHA256).Hash -ne $committedExpected[0].sha256 -or
        (Test-Path -LiteralPath $committedBackup) -or
        (Test-Path -LiteralPath "$committedReceiptPath.pending.json")) {
        throw 'Valid committed receipt did not preserve the installed package during recovery'
    }
    Write-Output 'PASS: matched receipt commits an interrupted transaction before retry'

    $freshRoot = Join-Path $temp 'fresh-install'
    $freshVst3Root = Join-Path $freshRoot 'VST3'
    $freshClapRoot = Join-Path $freshRoot 'CLAP'
    $freshStandaloneRoot = Join-Path $freshRoot 'Standalone'
    $freshReceipt = Join-Path $temp 'fresh-install-receipt.json'
    New-Item -ItemType Directory -Force -Path $freshVst3Root | Out-Null
    $heldLock = [IO.File]::Open((Join-Path $freshVst3Root '.Spectr.install.lock'), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    try {
        Assert-Fails {
            & (Join-Path $PSScriptRoot 'install-package.ps1') -Package $package `
                -Vst3Root $freshVst3Root -ClapRoot $freshClapRoot -StandaloneRoot $freshStandaloneRoot `
                -Receipt $freshReceipt -NoShortcuts | Out-Null
        } 'concurrent installer' 'Another Spectr installer holds an install lock'
    } finally { $heldLock.Dispose() }
    if (Test-Path -LiteralPath $freshReceipt) { throw 'Lock rejection wrote a success receipt' }
    Write-Output 'PASS: destination lock rejects a concurrent installer'

    $otherRoot = Join-Path $temp 'different-roots-same-receipt'
    $heldReceiptLock = [IO.File]::Open("$freshReceipt.lock", [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    try {
        Assert-Fails {
            & (Join-Path $PSScriptRoot 'install-package.ps1') -Package $package `
                -Vst3Root (Join-Path $otherRoot 'VST3') -ClapRoot (Join-Path $otherRoot 'CLAP') `
                -StandaloneRoot (Join-Path $otherRoot 'Standalone') -Receipt $freshReceipt -NoShortcuts | Out-Null
        } 'different roots sharing a receipt' 'Another Spectr installer holds an install lock'
    } finally { $heldReceiptLock.Dispose() }
    if (Test-Path -LiteralPath $freshReceipt) { throw 'Shared-receipt lock rejection wrote a success receipt' }
    if (Test-Path -LiteralPath "$freshReceipt.pending.json") { throw 'Shared-receipt lock rejection wrote a journal' }
    Write-Output 'PASS: receipt lock rejects different roots sharing one receipt path'

    $heldPendingLock = [IO.File]::Open("$freshReceipt.pending.json.lock", [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    try {
        Assert-Fails {
            & (Join-Path $PSScriptRoot 'install-package.ps1') -Package $package `
                -Vst3Root (Join-Path $otherRoot 'VST3') -ClapRoot (Join-Path $otherRoot 'CLAP') `
                -StandaloneRoot (Join-Path $otherRoot 'Standalone') -Receipt $freshReceipt -NoShortcuts | Out-Null
        } 'pending journal identity lock' 'Another Spectr installer holds an install lock'
    } finally { $heldPendingLock.Dispose() }
    Write-Output 'PASS: pending journal identity is locked before install'

    $global:SpectrTestFailSwap = Join-Path $freshClapRoot 'Spectr.clap'
    function Move-Item {
        param([string]$LiteralPath, [string]$Destination, [switch]$Force)
        if ($Destination -eq $global:SpectrTestFailSwap -and $LiteralPath.EndsWith('.new')) {
            throw 'Injected fresh-install failure after VST3 swap'
        }
        Microsoft.PowerShell.Management\Move-Item @PSBoundParameters
    }
    try {
        Assert-Fails {
            & (Join-Path $PSScriptRoot 'install-package.ps1') -Package $package `
                -Vst3Root $freshVst3Root -ClapRoot $freshClapRoot -StandaloneRoot $freshStandaloneRoot `
                -Receipt $freshReceipt -NoShortcuts | Out-Null
        } 'fresh mid-swap failure' 'Injected fresh-install failure after VST3 swap'
    } finally {
        Remove-Item Function:\Move-Item
        Remove-Variable SpectrTestFailSwap -Scope Global
    }
    foreach ($destination in @((Join-Path $freshVst3Root 'Spectr.vst3'), (Join-Path $freshClapRoot 'Spectr.clap'), (Join-Path $freshStandaloneRoot 'Spectr.exe'))) {
        if (Test-Path -LiteralPath $destination) { throw "Fresh failed install left an artifact: $destination" }
    }
    if (Test-Path -LiteralPath $freshReceipt) { throw 'Fresh failed install wrote a success receipt' }
    if (Test-Path -LiteralPath "$freshReceipt.pending.json") { throw 'Fresh failed install left a pending journal' }
    if (@(Get-ChildItem -LiteralPath $freshRoot -Recurse -Force | Where-Object { $_.Name -match '\.[0-9a-f]{32}\.(new|backup)$' }).Count -ne 0) {
        throw 'Fresh failed install left staged or backup artifacts'
    }
    Write-Output 'PASS: fresh mid-swap failure removes new files and leaves no receipt'

    # The COM shortcut implementation is Windows-only. Mock only its file
    # creation here so rollback of both shortcut paths runs under pwsh too.
    $oldPublic = $env:PUBLIC
    $oldProgramData = $env:ProgramData
    $env:PUBLIC = Join-Path $temp 'mock-public'
    $env:ProgramData = Join-Path $temp 'mock-programdata'
    function New-Object {
        param([Parameter(Position = 0)] [string]$TypeName, [string]$ComObject, [object[]]$ArgumentList)
        if ([string]::IsNullOrEmpty($ComObject)) { return Microsoft.PowerShell.Utility\New-Object @PSBoundParameters }
        if ($ComObject -ne 'WScript.Shell') { throw "Unexpected COM mock request: $ComObject" }
        $shell = [pscustomobject]@{}
        $shell | Add-Member -MemberType ScriptMethod -Name CreateShortcut -Value {
            param($path)
            $link = [pscustomobject]@{ Path = $path; TargetPath = ''; WorkingDirectory = ''; IconLocation = '' }
            $link | Add-Member -MemberType ScriptMethod -Name Save -Value {
                [IO.File]::WriteAllText($this.Path, "$($this.TargetPath)|$($this.WorkingDirectory)|$($this.IconLocation)")
            }
            return $link
        }
        return $shell
    }
    $shortcutRoot = Join-Path $temp 'shortcut-install'
    $shortcutReceipt = Join-Path $temp 'shortcut-install-receipt.json'
    $shortcutArgs = @{
        Package = $package
        Vst3Root = (Join-Path $shortcutRoot 'VST3')
        ClapRoot = (Join-Path $shortcutRoot 'CLAP')
        StandaloneRoot = (Join-Path $shortcutRoot 'Standalone')
        Receipt = $shortcutReceipt
    }
    try {
        & (Join-Path $PSScriptRoot 'install-package.ps1') @shortcutArgs | Out-Null
        $shortcutReceiptData = Get-Content -LiteralPath $shortcutReceipt -Raw | ConvertFrom-Json
        if (@($shortcutReceiptData.shortcuts).Count -ne 2) { throw 'Shortcut install receipt lacks two hashes' }
        $shortcutDestinations = @($shortcutReceiptData.shortcuts | ForEach-Object { [string]$_.destination })
        $priorPaths = @(
            (Join-Path $shortcutRoot 'VST3\Spectr.vst3\Contents\arm64-win\Spectr.vst3'),
            (Join-Path $shortcutRoot 'CLAP\Spectr.clap'),
            (Join-Path $shortcutRoot 'Standalone\Spectr.exe')
        ) + $shortcutDestinations
        $priorHashes = @()
        for ($index = 0; $index -lt $priorPaths.Count; $index++) {
            Set-Content -LiteralPath $priorPaths[$index] -Value "previous shortcut transaction $index" -NoNewline
            $priorHashes += (Get-FileHash -LiteralPath $priorPaths[$index] -Algorithm SHA256).Hash
        }
        $global:SpectrTestFailSwap = $shortcutDestinations[1]
        function Move-Item {
            param([string]$LiteralPath, [string]$Destination, [switch]$Force)
            if ($Destination -eq $global:SpectrTestFailSwap -and $LiteralPath.EndsWith('.new.lnk')) {
                throw 'Injected failure after first shortcut swap'
            }
            Microsoft.PowerShell.Management\Move-Item @PSBoundParameters
        }
        try {
            Assert-Fails { & (Join-Path $PSScriptRoot 'install-package.ps1') @shortcutArgs | Out-Null } 'shortcut mid-swap failure' 'Injected failure after first shortcut swap'
        } finally {
            Remove-Item Function:\Move-Item
            Remove-Variable SpectrTestFailSwap -Scope Global
        }
        for ($index = 0; $index -lt $priorPaths.Count; $index++) {
            if ((Get-FileHash -LiteralPath $priorPaths[$index] -Algorithm SHA256).Hash -ne $priorHashes[$index]) {
                throw "Shortcut failure did not restore prior artifact: $($priorPaths[$index])"
            }
        }
        if (Test-Path -LiteralPath $shortcutReceipt) { throw 'Shortcut failure wrote a success receipt' }
        if (Test-Path -LiteralPath "$shortcutReceipt.pending.json") { throw 'Shortcut failure left a pending journal' }
        Write-Output 'PASS: shortcut failure restores previous plugin and shortcut bytes'
    } finally {
        Remove-Item Function:\New-Object
        $env:PUBLIC = $oldPublic
        $env:ProgramData = $oldProgramData
    }
} finally {
    if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Recurse -Force }
}
