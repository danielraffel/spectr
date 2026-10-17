[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$BuildDir,
    [ValidateSet('x86_64-win', 'arm64-win', 'arm64ec-win')] [string]$Architecture = 'arm64-win',
    [ValidateSet('x86_64-win', 'arm64-win', 'arm64ec-win')] [string]$Vst3Architecture = '',
    [ValidateSet('x86_64-win', 'arm64-win', 'arm64ec-win')] [string]$RuntimeArchitecture = '',
    [string]$Output = ''
)

$ErrorActionPreference = 'Stop'
$validation = Join-Path $PSScriptRoot 'package-validation.ps1'
if (-not (Test-Path -LiteralPath $validation -PathType Leaf)) { throw "Packaging validation helper is missing: $validation" }
. $validation
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
if ([string]::IsNullOrWhiteSpace($Vst3Architecture)) { $Vst3Architecture = $Architecture }
# ARM64EC artifacts commonly use the native ARM64 wgpu runtime. Validate this
# separate PE using its own architecture rather than the plugin architecture.
if ([string]::IsNullOrWhiteSpace($RuntimeArchitecture)) {
    $RuntimeArchitecture = if ($Vst3Architecture -eq 'arm64ec-win') { 'arm64-win' } else { $Vst3Architecture }
}

function Resolve-SpectrCMakeArtifact {
    param(
        [Parameter(Mandatory = $true)] [string]$BuildDir,
        [Parameter(Mandatory = $true)] [string]$SingleConfigPath,
        [Parameter(Mandatory = $true)] [string]$ReleasePath
    )

    $single = Join-Path $BuildDir $SingleConfigPath
    $release = Join-Path $BuildDir $ReleasePath
    $cache = Join-Path $BuildDir 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cache -PathType Leaf) {
        $generator = Get-Content -LiteralPath $cache | Where-Object { $_ -match '^CMAKE_GENERATOR:INTERNAL=' } | Select-Object -First 1
        if ([string]::IsNullOrWhiteSpace($generator)) { throw "CMake generator is missing from $cache" }
        $candidate = if ($generator -match '^CMAKE_GENERATOR:INTERNAL=(Visual Studio|Ninja Multi-Config|Xcode)') { $release } else { $single }
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { throw "Missing build artifact for $generator`: $candidate" }
        return $candidate
    }

    # A staged input without a CMake cache is useful for verified release
    # handoffs. Never guess if both layouts contain potentially different files.
    $singleExists = Test-Path -LiteralPath $single -PathType Leaf
    $releaseExists = Test-Path -LiteralPath $release -PathType Leaf
    if ($singleExists -and $releaseExists) { throw "Ambiguous staged build artifact: $single and $release" }
    if ($singleExists) { return $single }
    if ($releaseExists) { return $release }
    throw "Missing build artifact: $single or $release"
}

$buildInfoSource = Find-SpectrBuildInfoSource $BuildDir $Vst3Architecture
if ($null -eq $buildInfoSource) { throw 'Required VST3 metadata sidecar was not found: Spectr.pulp-build-info.json' }
$buildInfo = Assert-SpectrBuildInfo $buildInfoSource $Vst3Architecture
$declaredRuntimeFiles = @()
if ($null -ne $buildInfo.PSObject.Properties['bundled_runtime_libraries']) {
    $declaredRuntimeFiles = @($buildInfo.bundled_runtime_libraries | ForEach-Object { [string]$_.file })
}
if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path (Split-Path $BuildDir -Parent) ("Spectr-windows-$Architecture.zip")
}
$Output = [IO.Path]::GetFullPath($Output)
$stage = Join-Path ([IO.Path]::GetTempPath()) ("spectr-package-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $stage | Out-Null
try {
    $bin = Join-Path $stage 'Standalone\Spectr.exe'
    $vst3 = Join-Path $stage "VST3\Spectr.vst3\Contents\$Vst3Architecture\Spectr.vst3"
    $clap = Join-Path $stage "CLAP\Spectr.clap"
    foreach ($path in @($bin, $vst3, $clap)) {
        New-Item -ItemType Directory -Force -Path (Split-Path $path -Parent) | Out-Null
    }

    $standaloneSource = Resolve-SpectrCMakeArtifact $BuildDir 'Spectr.exe' 'Release\Spectr.exe'
    $vst3Source = Join-Path $BuildDir "VST3\Spectr.vst3\Contents\$Vst3Architecture\Spectr.vst3"
    if (-not (Test-Path -LiteralPath $vst3Source -PathType Leaf)) {
        $vst3Source = Join-Path $BuildDir 'VST3\Spectr.dll'
    }
    $clapSource = Resolve-SpectrCMakeArtifact $BuildDir 'CLAP\Spectr.clap' 'CLAP\Release\Spectr.clap'
    foreach ($path in @($standaloneSource, $vst3Source, $clapSource)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing build artifact: $path" }
    }
    Copy-Item -LiteralPath $standaloneSource -Destination $bin
    Copy-Item -LiteralPath $vst3Source -Destination $vst3
    Copy-Item -LiteralPath $clapSource -Destination $clap

    $runtimeSidecars = @()
    foreach ($runtime in @('icudtl.dat', 'wgpu_native.dll')) {
        $source = Find-SpectrRuntimeSource $BuildDir $runtime $Vst3Architecture
        if ($null -ne $source) {
            if ($runtime -eq 'wgpu_native.dll' -and $Architecture -ne $Vst3Architecture) {
                throw 'Mixed-architecture packages cannot include one shared wgpu_native.dll; provide per-artifact runtime sources'
            }
            $runtimeSidecars += $runtime
            Copy-Item -LiteralPath $source -Destination (Join-Path $stage $runtime)
            # Each Windows target resolves its sidecars beside its own binary.
            # Keep the root copy for existing handoffs, but stage a copy for
            # every target so standalone and CLAP cannot silently omit ICU or
            # another declared runtime.
            foreach ($targetPath in @(
                "VST3\Spectr.vst3\Contents\$Vst3Architecture\$runtime",
                "CLAP\$runtime",
                "Standalone\$runtime"
            )) {
                Copy-Item -LiteralPath $source -Destination (Join-Path $stage $targetPath)
            }
        }
    }
    # Carry the verified installer beside the artifacts so a handoff ZIP is
    # directly usable on a clean Windows machine.  The installer validates the
    # manifest before it writes any plugin files.
    $installerSource = Join-Path $PSScriptRoot 'install-package.ps1'
    if (-not (Test-Path -LiteralPath $installerSource -PathType Leaf)) {
        throw "Installer script is missing: $installerSource"
    }
    Copy-Item -LiteralPath $installerSource -Destination (Join-Path $stage 'Install-Spectr.ps1')
    Copy-Item -LiteralPath $validation -Destination (Join-Path $stage 'Spectr-PackageValidation.ps1')
    $requiredRuntimes = @('icudtl.dat')
    if ($declaredRuntimeFiles -contains 'wgpu_native.dll') { $requiredRuntimes += 'wgpu_native.dll' }
    foreach ($requiredRuntime in $requiredRuntimes) {
        if ($runtimeSidecars -notcontains $requiredRuntime) {
            throw "Required runtime sidecar was not found: $requiredRuntime"
        }
    }
    Copy-Item -LiteralPath $buildInfoSource -Destination (Join-Path $stage "VST3\Spectr.vst3\Contents\$Vst3Architecture\Spectr.pulp-build-info.json")
    Assert-SpectrPeArchitecture $bin $Architecture
    Assert-SpectrPeArchitecture $vst3 $Vst3Architecture
    Assert-SpectrPeArchitecture $clap $Architecture
    $wgpu = Join-Path $stage 'wgpu_native.dll'
    if (Test-Path -LiteralPath $wgpu -PathType Leaf) {
        Assert-SpectrPeArchitecture $wgpu $RuntimeArchitecture
    }
    $files = @(Get-ChildItem -LiteralPath $stage -Recurse -File)
    $manifest = [ordered]@{
        schema = 1
        generated_at_utc = (Get-Date).ToUniversalTime().ToString('o')
        architecture = $Architecture
        artifact_architectures = [ordered]@{
            standalone = $Architecture
            vst3 = $Vst3Architecture
            clap = $Architecture
            runtime = $RuntimeArchitecture
        }
        runtime_sidecars = @($runtimeSidecars)
        runtime_architectures = [ordered]@{ wgpu_native_dll = $RuntimeArchitecture }
        metadata_sidecars = @('Spectr.pulp-build-info.json')
        files = @($files | ForEach-Object {
            [ordered]@{ path = $_.FullName.Substring($stage.Length + 1); bytes = $_.Length; sha256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash }
        })
    }
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $stage 'manifest.json') -Encoding utf8
    if (Test-Path -LiteralPath $Output) { Remove-Item -LiteralPath $Output -Force }
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $Output -CompressionLevel Optimal
    Write-Output "PASS: $Output"
    Write-Output "SHA256: $((Get-FileHash -LiteralPath $Output -Algorithm SHA256).Hash)"
} finally {
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
