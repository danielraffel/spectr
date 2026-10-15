Set-StrictMode -Version Latest

function Get-SpectrMachineForArchitecture {
    param([Parameter(Mandatory = $true)] [string]$Architecture)

    switch ($Architecture) {
        'x86_64-win' { return 0x8664 }
        'arm64-win' { return 0xAA64 }
        'arm64ec-win' { return 0xA641 }
        default { throw "Unsupported Windows package architecture: $Architecture" }
    }
}

function Get-SpectrPeMachine {
    param([Parameter(Mandatory = $true)] [string]$Path)

    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 0x40 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
        throw "Not a PE file (missing MZ header): $Path"
    }
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    if ($peOffset -lt 0 -or $peOffset -gt ($bytes.Length - 6) -or
        $bytes[$peOffset] -ne 0x50 -or $bytes[$peOffset + 1] -ne 0x45 -or
        $bytes[$peOffset + 2] -ne 0 -or $bytes[$peOffset + 3] -ne 0) {
        throw "Not a PE file (missing PE signature): $Path"
    }
    return [BitConverter]::ToUInt16($bytes, $peOffset + 4)
}

function Test-SpectrArm64XPe {
    param([Parameter(Mandatory = $true)] [string]$Path)

    $bytes = [IO.File]::ReadAllBytes($Path)
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    $sectionCount = [BitConverter]::ToUInt16($bytes, $peOffset + 6)
    $optionalSize = [BitConverter]::ToUInt16($bytes, $peOffset + 20)
    $sectionTable = $peOffset + 24 + $optionalSize
    for ($index = 0; $index -lt $sectionCount; $index++) {
        $offset = $sectionTable + ($index * 40)
        if ($offset -lt 0 -or $offset + 8 -gt $bytes.Length) { break }
        $name = [Text.Encoding]::ASCII.GetString($bytes, $offset, 8).Trim([char]0)
        if ($name -eq '.a64xrm') { return $true }
    }
    return $false
}

function Assert-SpectrPeArchitecture {
    param(
        [Parameter(Mandatory = $true)] [string]$Path,
        [Parameter(Mandatory = $true)] [string]$Architecture
    )

    $expected = Get-SpectrMachineForArchitecture $Architecture
    $actual = Get-SpectrPeMachine $Path
    if ($Architecture -eq 'arm64ec-win') {
        if (($actual -eq 0x8664 -or $actual -eq 0xAA64) -and (Test-SpectrArm64XPe $Path)) {
            return
        }
        throw "Architecture mismatch for ${Path}: ARM64EC payload lacks ARM64X .a64xrm metadata"
    }
    if ($actual -ne $expected) {
        throw "Architecture mismatch for ${Path}: expected $Architecture (machine 0x$('{0:X4}' -f $expected)), got machine 0x$('{0:X4}' -f $actual)"
    }
}

function Get-SpectrPackagePath {
    param(
        [Parameter(Mandatory = $true)] [string]$Stage,
        [Parameter(Mandatory = $true)] [string]$RelativePath
    )

    if ([string]::IsNullOrWhiteSpace($RelativePath) -or [IO.Path]::IsPathRooted($RelativePath)) {
        throw "Manifest path must be relative: $RelativePath"
    }
    $relative = $RelativePath -replace '/', '\'
    if ($relative -match '(^|\\)\.\.?($|\\)') {
        throw "Manifest path contains a traversal component: $RelativePath"
    }
    $root = ([IO.Path]::GetFullPath($Stage)).TrimEnd('\') + '\'
    $candidate = [IO.Path]::GetFullPath((Join-Path $Stage $relative))
    if (-not $candidate.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Manifest path escapes package root: $RelativePath"
    }
    return $candidate
}

function Assert-SpectrArchiveEntries {
    param(
        [Parameter(Mandatory = $true)] [string]$Package,
        [Parameter(Mandatory = $true)] [string]$Stage
    )

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($Package)
    try {
        $seen = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($entry in $archive.Entries) {
            $name = [string]$entry.FullName
            if ([string]::IsNullOrWhiteSpace($name)) { throw 'ZIP contains an empty entry name' }
            $normalized = $name -replace '/', '\'
            if (-not $seen.Add($normalized)) { throw "ZIP contains duplicate entry: $name" }
            [void](Get-SpectrPackagePath $Stage $normalized)
        }
    } finally {
        $archive.Dispose()
    }
}

function Assert-SpectrPackageManifest {
    param(
        [Parameter(Mandatory = $true)] [string]$Stage,
        [Parameter(Mandatory = $true)] $Manifest
    )

    if ($null -eq $Manifest -or $Manifest.schema -ne 1) {
        throw 'Unsupported or missing package manifest schema (expected schema 1)'
    }
    $architecture = [string]$Manifest.architecture
    [void](Get-SpectrMachineForArchitecture $architecture)
    $artifactArchitectures = $Manifest.artifact_architectures
    if ($null -eq $artifactArchitectures) { throw 'Package manifest lacks per-artifact architectures' }
    $standaloneArchitecture = [string]$artifactArchitectures.standalone
    $vst3Architecture = [string]$artifactArchitectures.vst3
    $clapArchitecture = [string]$artifactArchitectures.clap
    $runtimeArchitecture = if ($null -eq $artifactArchitectures.runtime) { $architecture } else { [string]$artifactArchitectures.runtime }
    foreach ($artifactArchitecture in @($standaloneArchitecture, $vst3Architecture, $clapArchitecture, $runtimeArchitecture)) {
        [void](Get-SpectrMachineForArchitecture $artifactArchitecture)
    }
    $entries = @($Manifest.files)
    if ($entries.Count -eq 0) { throw 'Package manifest has no files' }

    $seen = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $entries) {
        $relative = [string]$entry.path
        if (-not $seen.Add($relative)) { throw "Duplicate manifest path: $relative" }
        $source = Get-SpectrPackagePath $Stage $relative
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Manifest file is missing: $relative" }
        $hash = [string]$entry.sha256
        if ($hash -notmatch '^[0-9A-Fa-f]{64}$') { throw "Invalid SHA-256 in manifest: $relative" }
        if ($null -eq $entry.bytes) { throw "Missing byte count in manifest: $relative" }
        try { $expectedBytes = [Int64]$entry.bytes } catch { throw "Invalid byte count in manifest: $relative" }
        if ($expectedBytes -lt 0) { throw "Invalid byte count in manifest: $relative" }
        $item = Get-Item -LiteralPath $source
        if ($item.Length -ne $expectedBytes) { throw "Manifest byte count mismatch: $relative" }
        $actual = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
        if (-not $actual.Equals($hash, [StringComparison]::OrdinalIgnoreCase)) { throw "Manifest hash mismatch: $relative" }
    }

    $stageRoot = ([IO.Path]::GetFullPath($Stage)).TrimEnd('\') + '\'
    $topLevelManifest = [IO.Path]::GetFullPath((Join-Path $Stage 'manifest.json'))
    foreach ($item in @(Get-ChildItem -LiteralPath $Stage -Recurse -File)) {
        if ($item.FullName.Equals($topLevelManifest, [StringComparison]::OrdinalIgnoreCase)) { continue }
        $relative = $item.FullName.Substring($stageRoot.Length)
        if (-not $seen.Contains($relative)) { throw "Unlisted package file: $relative" }
    }

    $required = @(
        "Standalone\Spectr.exe",
        "VST3\Spectr.vst3\Contents\$vst3Architecture\Spectr.vst3",
        'CLAP\Spectr.clap',
        'Install-Spectr.ps1',
        'Spectr-PackageValidation.ps1'
    )
    foreach ($relative in $required) {
        if (-not $seen.Contains($relative)) { throw "Manifest is missing required file: $relative" }
    }
    Assert-SpectrPeArchitecture (Get-SpectrPackagePath $Stage 'Standalone\Spectr.exe') $standaloneArchitecture
    Assert-SpectrPeArchitecture (Get-SpectrPackagePath $Stage "VST3\Spectr.vst3\Contents\$vst3Architecture\Spectr.vst3") $vst3Architecture
    Assert-SpectrPeArchitecture (Get-SpectrPackagePath $Stage 'CLAP\Spectr.clap') $clapArchitecture
    foreach ($sidecar in @(Get-ChildItem -LiteralPath $Stage -Recurse -File -Filter 'wgpu_native.dll')) {
        Assert-SpectrPeArchitecture $sidecar.FullName $runtimeArchitecture
    }
    return $architecture
}
