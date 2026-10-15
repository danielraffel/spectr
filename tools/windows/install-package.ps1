[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$Package,
    [string]$Vst3Root = "$env:CommonProgramFiles\VST3",
    [string]$ClapRoot = "$env:CommonProgramFiles\CLAP",
    [string]$StandaloneRoot = "$env:ProgramFiles\Spectr",
    [string]$Receipt = '',
    [switch]$NoShortcuts
)

$ErrorActionPreference = 'Stop'
$Package = (Resolve-Path -LiteralPath $Package).Path
if ([string]::IsNullOrWhiteSpace($Receipt)) { $Receipt = "$Package.install-receipt.json" }
$Receipt = [IO.Path]::GetFullPath($Receipt)
if ($Receipt.Equals($Package, [StringComparison]::OrdinalIgnoreCase)) { throw 'Receipt path must differ from package path' }
# A failed retry must never leave an earlier success receipt at this path.
if (Test-Path -LiteralPath $Receipt) { Remove-Item -LiteralPath $Receipt -Force }
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
    $installedFiles = @()
    foreach ($entry in @($manifest.files)) {
        $relative = ([string]$entry.path).Replace('/', '\')
        $destination = $null
        if ($relative.StartsWith('VST3\Spectr.vst3\', [StringComparison]::OrdinalIgnoreCase)) {
            $destination = Join-Path $Vst3Root $relative.Substring(5)
        } elseif ($relative.Equals('CLAP\Spectr.clap', [StringComparison]::OrdinalIgnoreCase)) {
            $destination = $clapDest
        } elseif ($relative.Equals('Standalone\Spectr.exe', [StringComparison]::OrdinalIgnoreCase)) {
            $destination = $standaloneDest
        }
        if ($null -eq $destination) { continue }
        if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) { throw "Installed file is missing: $destination" }
        $item = Get-Item -LiteralPath $destination
        $hash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
        if ($item.Length -ne [Int64]$entry.bytes -or
            -not $hash.Equals([string]$entry.sha256, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Installed file differs from package manifest: $destination"
        }
        $installedFiles += [ordered]@{
            package_path = [string]$entry.path
            destination = $item.FullName
            bytes = $item.Length
            sha256 = $hash
        }
    }
    $expectedCount = @($manifest.files | Where-Object {
        ([string]$_.path).Replace('/', '\') -match '^(?i:VST3\\Spectr\.vst3\\|CLAP\\Spectr\.clap$|Standalone\\Spectr\.exe$)'
    }).Count
    if ($installedFiles.Count -ne $expectedCount) { throw 'Installed file receipt is incomplete' }
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
    $receiptData = [ordered]@{
        schema = 'spectr.windows.install.v1'
        status = 'installed_files_match_manifest'
        installed_at_utc = (Get-Date).ToUniversalTime().ToString('o')
        package = [ordered]@{
            path = $Package
            sha256 = (Get-FileHash -LiteralPath $Package -Algorithm SHA256).Hash
            manifest_sha256 = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
            architecture = $architecture
            artifact_architectures = $manifest.artifact_architectures
        }
        installed_files = $installedFiles
    }
    $receiptDir = Split-Path $Receipt -Parent
    New-Item -ItemType Directory -Force -Path $receiptDir | Out-Null
    $receiptTemp = Join-Path $receiptDir ('.spectr-install-' + [guid]::NewGuid().ToString('N') + '.json')
    try {
        $receiptData | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $receiptTemp -Encoding utf8
        Move-Item -LiteralPath $receiptTemp -Destination $Receipt -Force
    } finally {
        if (Test-Path -LiteralPath $receiptTemp) { Remove-Item -LiteralPath $receiptTemp -Force }
    }
    Write-Output "PASS: installed Spectr $architecture"
    Write-Output "VST3: $vst3Dest"
    Write-Output "CLAP: $clapDest"
    Write-Output "Standalone: $standaloneDest"
    if ($shortcutPaths.Count -gt 0) { Write-Output "Shortcut: $($shortcutPaths[0])" }
    Write-Output "Install receipt: $Receipt"
} finally {
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
