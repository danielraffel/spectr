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
$pending = "$Receipt.pending.json"

function Remove-SpectrPath {
    param([string]$Path)
    if (Test-Path -LiteralPath $Path) { Remove-Item -LiteralPath $Path -Recurse -Force }
}

function Restore-SpectrInstall {
    param([object[]]$Plan, [string]$JournalPath)
    # A backup exists only after the old destination was moved. If no old
    # destination existed, a missing stage means the new one was moved.
    foreach ($item in $Plan) {
        if (Test-Path -LiteralPath $item.backup) {
            Remove-SpectrPath $item.destination
            Move-Item -LiteralPath $item.backup -Destination $item.destination
        } elseif ($item.had_original) {
            if (-not (Test-Path -LiteralPath $item.destination)) {
                throw "Cannot restore prior install; original and backup are missing: $($item.destination)"
            }
        } elseif (-not (Test-Path -LiteralPath $item.staged)) {
            Remove-SpectrPath $item.destination
        }
        Remove-SpectrPath $item.staged
    }
    Remove-SpectrPath $JournalPath
}

$vst3Dest = Join-Path $Vst3Root 'Spectr.vst3'
$clapDest = Join-Path $ClapRoot 'Spectr.clap'
$standaloneDest = Join-Path $StandaloneRoot 'Spectr.exe'
$destinations = @($vst3Dest, $clapDest, $standaloneDest)
$canonicalDestinations = @($destinations | ForEach-Object { [IO.Path]::GetFullPath($_) })
$vst3Prefix = $canonicalDestinations[0].TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
for ($index = 0; $index -lt 3; $index++) {
    for ($other = $index + 1; $other -lt 3; $other++) {
        if ($canonicalDestinations[$index].Equals($canonicalDestinations[$other], [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Install destinations must be distinct'
        }
    }
}
foreach ($path in @($canonicalDestinations[1], $canonicalDestinations[2], $Receipt, $pending, $Package)) {
    if ($path.StartsWith($vst3Prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Install destination or receipt lies inside the VST3 bundle: $path"
    }
}
foreach ($destination in $canonicalDestinations) {
    if ($destination.Equals($Receipt, [StringComparison]::OrdinalIgnoreCase) -or
        $destination.Equals($pending, [StringComparison]::OrdinalIgnoreCase) -or
        $destination.Equals($Package, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Package and receipt paths must differ from install destinations'
    }
}
if (Test-Path -LiteralPath $pending -PathType Leaf) {
    $journal = Get-Content -LiteralPath $pending -Raw | ConvertFrom-Json
    if ($journal.schema -ne 'spectr.windows.install.pending.v1' -or
        [string]$journal.id -notmatch '^[0-9a-f]{32}$' -or @($journal.destinations).Count -ne 3) {
        throw "Cannot safely recover invalid install journal: $pending"
    }
    $recoveryPlan = @()
    for ($index = 0; $index -lt 3; $index++) {
        $destination = [IO.Path]::GetFullPath($destinations[$index])
        if (-not $destination.Equals([string]$journal.destinations[$index].path, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Install roots differ from pending transaction; recover with the original roots: $pending"
        }
        $recoveryPlan += [pscustomobject]@{
            destination = $destination
            staged = "$destination.$($journal.id).new"
            backup = "$destination.$($journal.id).backup"
            had_original = [bool]$journal.destinations[$index].had_original
        }
    }
    # A receipt with this transaction ID means all installed hashes were
    # verified before interruption. Keep it only while those hashes still match.
    $committed = $false
    if (Test-Path -LiteralPath $Receipt -PathType Leaf) {
        $previousReceipt = Get-Content -LiteralPath $Receipt -Raw | ConvertFrom-Json
        if ($previousReceipt.transaction_id -eq $journal.id) {
            $committed = $true
            foreach ($file in @($previousReceipt.installed_files)) {
                if (-not (Test-Path -LiteralPath $file.destination -PathType Leaf) -or
                    (Get-FileHash -LiteralPath $file.destination -Algorithm SHA256).Hash -ne $file.sha256) {
                    $committed = $false
                    break
                }
            }
        }
    }
    if ($committed) {
        foreach ($item in $recoveryPlan) {
            Remove-SpectrPath $item.staged
            Remove-SpectrPath $item.backup
        }
        Remove-SpectrPath $pending
    } else {
        Remove-SpectrPath $Receipt
        Restore-SpectrInstall $recoveryPlan $pending
    }
}
# A failed retry must never leave an earlier success receipt at this path.
Remove-SpectrPath $Receipt
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

    New-Item -ItemType Directory -Force -Path $Vst3Root, $ClapRoot, $StandaloneRoot | Out-Null
    $transactionId = [guid]::NewGuid().ToString('N')
    $sources = @($vst3Source, $clapSource, $standaloneSource)
    $plan = @()
    for ($index = 0; $index -lt 3; $index++) {
        $destination = [IO.Path]::GetFullPath($destinations[$index])
        $plan += [pscustomobject]@{
            source = $sources[$index]
            destination = $destination
            staged = "$destination.$transactionId.new"
            backup = "$destination.$transactionId.backup"
            had_original = (Test-Path -LiteralPath $destination)
        }
    }
    try {
        foreach ($item in $plan) {
            Copy-Item -LiteralPath $item.source -Destination $item.staged -Recurse
        }
        # All sources are staged on the destination volume before any live
        # artifact moves. This also catches a damaged copy before swap.
        foreach ($entry in @($manifest.files)) {
            $relative = ([string]$entry.path).Replace('/', '\')
            $stagedFile = $null
            if ($relative.StartsWith('VST3\Spectr.vst3\', [StringComparison]::OrdinalIgnoreCase)) {
                $stagedFile = Join-Path $plan[0].staged $relative.Substring(17)
            } elseif ($relative.Equals('CLAP\Spectr.clap', [StringComparison]::OrdinalIgnoreCase)) {
                $stagedFile = $plan[1].staged
            } elseif ($relative.Equals('Standalone\Spectr.exe', [StringComparison]::OrdinalIgnoreCase)) {
                $stagedFile = $plan[2].staged
            }
            if ($null -eq $stagedFile) { continue }
            if (-not (Test-Path -LiteralPath $stagedFile -PathType Leaf) -or
                (Get-Item -LiteralPath $stagedFile).Length -ne [Int64]$entry.bytes -or
                (Get-FileHash -LiteralPath $stagedFile -Algorithm SHA256).Hash -ne [string]$entry.sha256) {
                throw "Staged file differs from package manifest: $stagedFile"
            }
        }
        $journalData = [ordered]@{
            schema = 'spectr.windows.install.pending.v1'
            id = $transactionId
            destinations = @($plan | ForEach-Object { [ordered]@{ path = $_.destination; had_original = $_.had_original } })
        }
        $receiptDir = Split-Path $Receipt -Parent
        New-Item -ItemType Directory -Force -Path $receiptDir | Out-Null
        $journalTemp = Join-Path $receiptDir ('.spectr-install-' + $transactionId + '.pending.tmp')
        try {
            $journalData | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $journalTemp -Encoding utf8
            Move-Item -LiteralPath $journalTemp -Destination $pending
        } finally {
            Remove-SpectrPath $journalTemp
        }
        foreach ($item in $plan) {
            if ($item.had_original) { Move-Item -LiteralPath $item.destination -Destination $item.backup }
            Move-Item -LiteralPath $item.staged -Destination $item.destination
        }
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
        transaction_id = $transactionId
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
    $receiptTemp = Join-Path $receiptDir ('.spectr-install-' + [guid]::NewGuid().ToString('N') + '.json')
    try {
        $receiptData | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $receiptTemp -Encoding utf8
        Move-Item -LiteralPath $receiptTemp -Destination $Receipt -Force
    } finally {
        if (Test-Path -LiteralPath $receiptTemp) { Remove-Item -LiteralPath $receiptTemp -Force }
    }
    } catch {
        $installError = $_
        Remove-SpectrPath $Receipt
        if (Test-Path -LiteralPath $pending) {
            Restore-SpectrInstall $plan $pending
        } else {
            foreach ($item in $plan) { Remove-SpectrPath $item.staged }
        }
        throw $installError
    }
    foreach ($item in $plan) {
        Remove-SpectrPath $item.staged
        Remove-SpectrPath $item.backup
    }
    Remove-SpectrPath $pending
    Write-Output "PASS: installed Spectr $architecture"
    Write-Output "VST3: $vst3Dest"
    Write-Output "CLAP: $clapDest"
    Write-Output "Standalone: $standaloneDest"
    if ($shortcutPaths.Count -gt 0) { Write-Output "Shortcut: $($shortcutPaths[0])" }
    Write-Output "Install receipt: $Receipt"
} finally {
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
