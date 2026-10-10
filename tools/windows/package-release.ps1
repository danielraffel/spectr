[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$BuildDir,
    [ValidateSet('x86_64-win', 'arm64-win')] [string]$Architecture = 'arm64-win',
    [string]$Output = ''
)

$ErrorActionPreference = 'Stop'
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path (Split-Path $BuildDir -Parent) ("Spectr-windows-$Architecture.zip")
}
$Output = [IO.Path]::GetFullPath($Output)
$stage = Join-Path ([IO.Path]::GetTempPath()) ("spectr-package-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $stage | Out-Null
try {
    $bin = Join-Path $stage 'Standalone\Spectr.exe'
    $vst3 = Join-Path $stage "VST3\Spectr.vst3\Contents\$Architecture\Spectr.vst3"
    $clap = Join-Path $stage "CLAP\Spectr.clap"
    foreach ($path in @($bin, $vst3, $clap)) {
        New-Item -ItemType Directory -Force -Path (Split-Path $path -Parent) | Out-Null
    }

    $standaloneSource = Join-Path $BuildDir 'Spectr.exe'
    $vst3Source = Join-Path $BuildDir "VST3\Spectr.vst3\Contents\$Architecture\Spectr.vst3"
    if (-not (Test-Path -LiteralPath $vst3Source -PathType Leaf)) {
        $vst3Source = Join-Path $BuildDir 'VST3\Spectr.dll'
    }
    $clapSource = Join-Path $BuildDir 'CLAP\Spectr.clap'
    foreach ($path in @($standaloneSource, $vst3Source, $clapSource)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing build artifact: $path" }
    }
    Copy-Item -LiteralPath $standaloneSource -Destination $bin
    Copy-Item -LiteralPath $vst3Source -Destination $vst3
    Copy-Item -LiteralPath $clapSource -Destination $clap

    foreach ($runtime in @('icudtl.dat', 'wgpu_native.dll')) {
        $source = Join-Path $BuildDir $runtime
        if (Test-Path -LiteralPath $source -PathType Leaf) {
            Copy-Item -LiteralPath $source -Destination (Join-Path $stage $runtime)
            # The VST3 loader resolves its sidecar runtime beside the
            # architecture-specific binary. Keep the root copy for existing
            # handoffs, but also package the runtime inside the bundle so the
            # installer cannot silently omit it.
            Copy-Item -LiteralPath $source -Destination (Join-Path $stage "VST3\Spectr.vst3\Contents\$Architecture\$runtime")
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
    $files = @(Get-ChildItem -LiteralPath $stage -Recurse -File)
    $manifest = [ordered]@{
        schema = 1
        generated_at_utc = (Get-Date).ToUniversalTime().ToString('o')
        architecture = $Architecture
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
