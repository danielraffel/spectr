[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$Package,
    [string]$Vst3Root = "$env:CommonProgramFiles\VST3",
    [string]$ClapRoot = "$env:CommonProgramFiles\CLAP",
    [string]$StandaloneRoot = "$env:ProgramFiles\Spectr",
    [switch]$NoShortcuts
)

$ErrorActionPreference = 'Stop'
$Package = (Resolve-Path -LiteralPath $Package).Path
$validation = Join-Path $PSScriptRoot 'Spectr-PackageValidation.ps1'
if (-not (Test-Path -LiteralPath $validation -PathType Leaf)) {
    $validation = Join-Path $PSScriptRoot 'package-validation.ps1'
}
if (-not (Test-Path -LiteralPath $validation -PathType Leaf)) {
    throw "Packaging validation helper is missing beside the installer: $PSScriptRoot"
}
. $validation
$stage = Join-Path ([IO.Path]::GetTempPath()) ("spectr-install-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $stage | Out-Null
try {
    Assert-SpectrArchiveEntries $Package $stage
    Expand-Archive -LiteralPath $Package -DestinationPath $stage -Force
    $manifestPath = Join-Path $stage 'manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw 'Package manifest.json is missing' }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $architecture = Assert-SpectrPackageManifest $stage $manifest

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
    $shortcutPaths = @()
    if (-not $NoShortcuts) {
        $shell = New-Object -ComObject WScript.Shell
        $shortcutPaths = @(
            (Join-Path $env:PUBLIC 'Desktop\Spectr.lnk'),
            (Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs\Spectr.lnk')
        )
        foreach ($shortcutPath in $shortcutPaths) {
            New-Item -ItemType Directory -Force -Path (Split-Path $shortcutPath -Parent) | Out-Null
            $shortcut = $shell.CreateShortcut($shortcutPath)
            $shortcut.TargetPath = $standaloneDest
            $shortcut.WorkingDirectory = $StandaloneRoot
            $shortcut.IconLocation = "$standaloneDest,0"
            $shortcut.Save()
        }
    }
    Write-Output "PASS: installed Spectr $architecture"
    Write-Output "VST3: $vst3Dest"
    Write-Output "CLAP: $clapDest"
    Write-Output "Standalone: $standaloneDest"
    if ($shortcutPaths.Count -gt 0) { Write-Output "Shortcut: $($shortcutPaths[0])" }
} finally {
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
