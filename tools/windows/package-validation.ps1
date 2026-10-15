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

function Assert-SpectrPeArchitecture {
    param(
        [Parameter(Mandatory = $true)] [string]$Path,
        [Parameter(Mandatory = $true)] [string]$Architecture
    )

    $expected = Get-SpectrMachineForArchitecture $Architecture
    $actual = Get-SpectrPeMachine $Path
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
    foreach ($item in @(Get-ChildItem -LiteralPath $Stage -Recurse -File)) {
        if ($item.Name -eq 'manifest.json') { continue }
        $relative = $item.FullName.Substring($stageRoot.Length)
        if (-not $seen.Contains($relative)) { throw "Unlisted package file: $relative" }
    }

    $required = @(
        "Standalone\Spectr.exe",
        "VST3\Spectr.vst3\Contents\$architecture\Spectr.vst3",
        'CLAP\Spectr.clap',
        'Install-Spectr.ps1',
        'Spectr-PackageValidation.ps1'
    )
    foreach ($relative in $required) {
        if (-not $seen.Contains($relative)) { throw "Manifest is missing required file: $relative" }
    }
    foreach ($relative in @("Standalone\Spectr.exe", "VST3\Spectr.vst3\Contents\$architecture\Spectr.vst3", 'CLAP\Spectr.clap')) {
        Assert-SpectrPeArchitecture (Get-SpectrPackagePath $Stage $relative) $architecture
    }
    foreach ($sidecar in @(Get-ChildItem -LiteralPath $Stage -Recurse -File -Filter 'wgpu_native.dll')) {
        Assert-SpectrPeArchitecture $sidecar.FullName $architecture
    }
    return $architecture
}
