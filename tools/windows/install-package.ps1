[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$Package,
    [string]$Vst3Root = "$env:CommonProgramFiles\VST3",
    [string]$ClapRoot = "$env:CommonProgramFiles\CLAP",
    [string]$StandaloneRoot = "$env:ProgramFiles\Spectr"
)

$ErrorActionPreference = 'Stop'
$Package = (Resolve-Path -LiteralPath $Package).Path
$stage = Join-Path ([IO.Path]::GetTempPath()) ("spectr-install-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $stage | Out-Null
try {
    Expand-Archive -LiteralPath $Package -DestinationPath $stage -Force
    $manifestPath = Join-Path $stage 'manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw 'Package manifest.json is missing' }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    foreach ($entry in @($manifest.files)) {
        $source = Join-Path $stage ([string]$entry.path)
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Manifest file is missing: $($entry.path)" }
        $actual = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
        if ($actual -ne [string]$entry.sha256) { throw "Manifest hash mismatch: $($entry.path)" }
    }

    $vst3Source = Join-Path $stage 'VST3\Spectr.vst3'
    $clapSource = Join-Path $stage 'CLAP\Spectr.clap'
    $standaloneSource = Join-Path $stage 'Standalone\Spectr.exe'
    foreach ($source in @($vst3Source, $clapSource, $standaloneSource)) {
        if (-not (Test-Path -LiteralPath $source)) { throw "Package artifact is missing: $source" }
    }

    $vst3Dest = Join-Path $Vst3Root 'Spectr.vst3'
    $clapDest = Join-Path $ClapRoot 'Spectr.clap'
    $standaloneDest = Join-Path $StandaloneRoot 'Spectr.exe'
    New-Item -ItemType Directory -Force -Path $Vst3Root, $ClapRoot, $StandaloneRoot | Out-Null
    if (Test-Path -LiteralPath $vst3Dest) { Remove-Item -LiteralPath $vst3Dest -Recurse -Force }
    Copy-Item -LiteralPath $vst3Source -Destination $vst3Dest -Recurse
    Copy-Item -LiteralPath $clapSource -Destination $clapDest -Force
    Copy-Item -LiteralPath $standaloneSource -Destination $standaloneDest -Force
    Write-Output "PASS: installed Spectr $($manifest.architecture)"
    Write-Output "VST3: $vst3Dest"
    Write-Output "CLAP: $clapDest"
    Write-Output "Standalone: $standaloneDest"
} finally {
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
