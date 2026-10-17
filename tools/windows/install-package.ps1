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
    # A backup exists only after the old destination was moved. A fresh
    # destination never contained prior user data, so remove it regardless of
    # which step of the swap was interrupted.
    foreach ($item in @($Plan | Where-Object { $_.active })) {
        if (Test-Path -LiteralPath $item.backup) {
            Remove-SpectrPath $item.destination
            Move-Item -LiteralPath $item.backup -Destination $item.destination
        } elseif ($item.had_original) {
            if (-not (Test-Path -LiteralPath $item.destination)) {
                throw "Cannot restore prior install; original and backup are missing: $($item.destination)"
            }
        } else {
            Remove-SpectrPath $item.destination
        }
        Remove-SpectrPath $item.staged
    }
    Remove-SpectrPath $JournalPath
}

function Get-SpectrInstallFileDestination {
    param([string]$PackagePath)
    $relative = $PackagePath.Replace('/', '\')
    if ($relative.StartsWith('VST3\Spectr.vst3\', [StringComparison]::OrdinalIgnoreCase)) {
        return [IO.Path]::GetFullPath((Join-Path $Vst3Root $relative.Substring(5)))
    }
    if ($relative.StartsWith('CLAP\', [StringComparison]::OrdinalIgnoreCase)) {
        return [IO.Path]::GetFullPath((Join-Path $ClapRoot $relative.Substring(5)))
    }
    if ($relative.StartsWith('Standalone\', [StringComparison]::OrdinalIgnoreCase)) {
        return [IO.Path]::GetFullPath((Join-Path $StandaloneRoot $relative.Substring(11)))
    }
    return $null
}

$vst3Dest = Join-Path $Vst3Root 'Spectr.vst3'
$clapDest = Join-Path $ClapRoot 'Spectr.clap'
$standaloneDest = Join-Path $StandaloneRoot 'Spectr.exe'
$shortcutPaths = @()
if (-not $NoShortcuts) {
    $shortcutPaths = @(
        (Join-Path $env:PUBLIC 'Desktop\Spectr.lnk'),
        (Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs\Spectr.lnk')
    )
}
# Keep the shared CLAP root intact.  The plugin and each target-local runtime
# are separate transaction items so installing Spectr cannot move or remove
# other CLAP plugins that happen to live beside it.
$destinations = @(
    $vst3Dest,
    $clapDest,
    (Join-Path $ClapRoot 'icudtl.dat'),
    (Join-Path $ClapRoot 'wgpu_native.dll'),
    $standaloneDest,
    (Join-Path $StandaloneRoot 'icudtl.dat'),
    (Join-Path $StandaloneRoot 'wgpu_native.dll')
) + $shortcutPaths
$artifactCount = 7
$canonicalDestinations = @($destinations | ForEach-Object { [IO.Path]::GetFullPath($_) })
$vst3Prefix = $canonicalDestinations[0].TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
for ($index = 0; $index -lt $destinations.Count; $index++) {
    for ($other = $index + 1; $other -lt $destinations.Count; $other++) {
        if ($canonicalDestinations[$index].Equals($canonicalDestinations[$other], [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Install destinations must be distinct'
        }
    }
}
foreach ($path in @($canonicalDestinations | Select-Object -Skip 1) + @($Receipt, $pending, $Package)) {
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
$lockRoots = @($Vst3Root, $ClapRoot, $StandaloneRoot) + @($shortcutPaths | ForEach-Object { Split-Path $_ -Parent })
$lockPaths = @($lockRoots | ForEach-Object {
    $root = [IO.Path]::GetFullPath($_)
    New-Item -ItemType Directory -Force -Path $root | Out-Null
    Join-Path $root '.Spectr.install.lock'
}) + @("$Receipt.lock", "$pending.lock")
$receiptDir = Split-Path $Receipt -Parent
New-Item -ItemType Directory -Force -Path $receiptDir | Out-Null
$lockPaths = @($lockPaths | Sort-Object -Unique)
foreach ($lockPath in $lockPaths) {
    foreach ($path in @($Receipt, $pending, $Package)) {
        if ([IO.Path]::GetFullPath($path).Equals([IO.Path]::GetFullPath($lockPath), [StringComparison]::OrdinalIgnoreCase)) {
            throw "Package, receipt, and journal paths must differ from install locks: $path"
        }
    }
}
$lockStreams = @()
try {
foreach ($lockPath in $lockPaths) {
    try {
        $lockStreams += [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    } catch {
        throw "Another Spectr installer holds an install lock: $lockPath"
    }
}
if (Test-Path -LiteralPath $pending -PathType Leaf) {
    $journal = Get-Content -LiteralPath $pending -Raw | ConvertFrom-Json
    if ($journal.schema -ne 'spectr.windows.install.pending.v1' -or
        [string]$journal.id -notmatch '^[0-9a-f]{32}$' -or @($journal.destinations).Count -ne $destinations.Count) {
        throw "Cannot safely recover invalid install journal: $pending"
    }
    $recoveryPlan = @()
    for ($index = 0; $index -lt $destinations.Count; $index++) {
        $destination = [IO.Path]::GetFullPath($destinations[$index])
        if (-not $destination.Equals([string]$journal.destinations[$index].path, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Install roots differ from pending transaction; recover with the original roots: $pending"
        }
        if ($journal.destinations[$index].had_original -isnot [bool] -or
            ($journal.destinations[$index].active -isnot [bool])) {
            throw "Cannot safely recover journal with invalid prior-install state: $pending"
        }
        $recoveryPlan += [pscustomobject]@{
            destination = $destination
            staged = if ($index -lt $artifactCount) { "$destination.$($journal.id).new" } else { "$destination.$($journal.id).new.lnk" }
            backup = if ($index -lt $artifactCount) { "$destination.$($journal.id).backup" } else { "$destination.$($journal.id).backup.lnk" }
            had_original = [bool]$journal.destinations[$index].had_original
            active = [bool]$journal.destinations[$index].active
        }
    }
    # A receipt with this transaction ID means all installed hashes were
    # verified before interruption. Keep it only while those hashes still match.
    $committed = $false
    if (Test-Path -LiteralPath $Receipt -PathType Leaf) {
        $previousReceipt = Get-Content -LiteralPath $Receipt -Raw | ConvertFrom-Json
        if ($previousReceipt.schema -eq 'spectr.windows.install.v1' -and
            $previousReceipt.status -eq 'installed_files_match_manifest' -and
            $previousReceipt.transaction_id -eq $journal.id -and
            [string]$previousReceipt.package.path -eq [string]$journal.package.path -and
            [string]$previousReceipt.package.sha256 -eq [string]$journal.package.sha256 -and
            [string]$previousReceipt.package.manifest_sha256 -eq [string]$journal.package.manifest_sha256 -and
            @($previousReceipt.installed_files).Count -gt 0 -and
            @($previousReceipt.installed_files).Count -eq @($journal.installed_files).Count -and
            @($previousReceipt.shortcuts).Count -eq $shortcutPaths.Count) {
            $committed = $true
            for ($index = 0; $index -lt @($journal.installed_files).Count; $index++) {
                $file = $previousReceipt.installed_files[$index]
                $expected = $journal.installed_files[$index]
                if ([string]$file.package_path -ne [string]$expected.package_path -or
                    -not ([string]$file.destination).Equals([string]$expected.destination, [StringComparison]::OrdinalIgnoreCase) -or
                    [string]$file.sha256 -ne [string]$expected.sha256 -or
                    [Int64]$file.bytes -ne [Int64]$expected.bytes -or
                    -not (Test-Path -LiteralPath $file.destination -PathType Leaf) -or
                    (Get-FileHash -LiteralPath $file.destination -Algorithm SHA256).Hash -ne $file.sha256) {
                    $committed = $false
                    break
                }
            }
            if ($committed) {
                for ($index = 0; $index -lt $shortcutPaths.Count; $index++) {
                    $file = $previousReceipt.shortcuts[$index]
                    if (-not ([string]$file.destination).Equals([string]$recoveryPlan[$index + $artifactCount].destination, [StringComparison]::OrdinalIgnoreCase) -or
                        -not (Test-Path -LiteralPath $file.destination -PathType Leaf) -or
                        (Get-FileHash -LiteralPath $file.destination -Algorithm SHA256).Hash -ne $file.sha256) {
                        $committed = $false
                        break
                    }
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
    # Stage each artifact and its target-local runtime as separate transaction
    # items.  This preserves unrelated files in the shared CLAP directory.
    $sources = @(
        $vst3Source,
        $clapSource,
        (Join-Path $stage 'CLAP\icudtl.dat'),
        (Join-Path $stage 'CLAP\wgpu_native.dll'),
        $standaloneSource,
        (Join-Path $stage 'Standalone\icudtl.dat'),
        (Join-Path $stage 'Standalone\wgpu_native.dll')
    )
    $plan = @()
    for ($index = 0; $index -lt $destinations.Count; $index++) {
        $source = if ($index -lt $artifactCount -and (Test-Path -LiteralPath $sources[$index])) { $sources[$index] } else { $null }
        $destination = [IO.Path]::GetFullPath($destinations[$index])
        $plan += [pscustomobject]@{
            source = $source
            destination = $destination
            staged = if ($index -lt $artifactCount) { "$destination.$transactionId.new" } else { "$destination.$transactionId.new.lnk" }
            backup = if ($index -lt $artifactCount) { "$destination.$transactionId.backup" } else { "$destination.$transactionId.backup.lnk" }
            had_original = (Test-Path -LiteralPath $destination)
            active = ($index -ge $artifactCount -or $null -ne $source)
            expected_hash = ''
        }
    }
    try {
        $journalData = [ordered]@{
            schema = 'spectr.windows.install.pending.v1'
            id = $transactionId
            destinations = @($plan | ForEach-Object { [ordered]@{ path = $_.destination; had_original = $_.had_original; active = $_.active } })
            package = [ordered]@{
                path = $Package
                sha256 = (Get-FileHash -LiteralPath $Package -Algorithm SHA256).Hash
                manifest_sha256 = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
            }
            installed_files = @($manifest.files | ForEach-Object {
                $destination = Get-SpectrInstallFileDestination ([string]$_.path)
                if ($null -ne $destination) {
                    [ordered]@{ package_path = [string]$_.path; destination = $destination; bytes = [Int64]$_.bytes; sha256 = [string]$_.sha256 }
                }
            })
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
        foreach ($item in @($plan | Select-Object -First $artifactCount | Where-Object { $_.active })) {
            Copy-Item -LiteralPath $item.source -Destination $item.staged -Recurse
        }
        if ($shortcutPaths.Count -gt 0) {
            $shell = New-Object -ComObject WScript.Shell
            foreach ($item in @($plan | Select-Object -Skip $artifactCount)) {
                $shortcut = $shell.CreateShortcut($item.staged)
                $shortcut.TargetPath = $standaloneDest
                $shortcut.WorkingDirectory = $StandaloneRoot
                $shortcut.IconLocation = "$standaloneDest,0"
                $shortcut.Save()
                if (-not (Test-Path -LiteralPath $item.staged -PathType Leaf)) { throw "Staged shortcut is missing: $($item.staged)" }
                $item.expected_hash = (Get-FileHash -LiteralPath $item.staged -Algorithm SHA256).Hash
            }
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
            } elseif ($relative.Equals('CLAP\icudtl.dat', [StringComparison]::OrdinalIgnoreCase)) {
                $stagedFile = $plan[2].staged
            } elseif ($relative.Equals('CLAP\wgpu_native.dll', [StringComparison]::OrdinalIgnoreCase)) {
                $stagedFile = $plan[3].staged
            } elseif ($relative.Equals('Standalone\Spectr.exe', [StringComparison]::OrdinalIgnoreCase)) {
                $stagedFile = $plan[3].staged
            } elseif ($relative.Equals('Standalone\icudtl.dat', [StringComparison]::OrdinalIgnoreCase)) {
                $stagedFile = $plan[5].staged
            } elseif ($relative.Equals('Standalone\wgpu_native.dll', [StringComparison]::OrdinalIgnoreCase)) {
                $stagedFile = $plan[6].staged
            }
            if ($null -eq $stagedFile) { continue }
            if (-not (Test-Path -LiteralPath $stagedFile -PathType Leaf) -or
                (Get-Item -LiteralPath $stagedFile).Length -ne [Int64]$entry.bytes -or
                (Get-FileHash -LiteralPath $stagedFile -Algorithm SHA256).Hash -ne [string]$entry.sha256) {
                throw "Staged file differs from package manifest: $stagedFile"
            }
        }
        foreach ($item in @($plan | Where-Object { $_.active })) {
            if ($item.had_original) { Move-Item -LiteralPath $item.destination -Destination $item.backup }
            Move-Item -LiteralPath $item.staged -Destination $item.destination
        }
    $installedFiles = @()
    foreach ($entry in @($manifest.files)) {
        $relative = ([string]$entry.path).Replace('/', '\')
        $destination = $null
        if ($relative.StartsWith('VST3\Spectr.vst3\', [StringComparison]::OrdinalIgnoreCase)) {
            $destination = Join-Path $Vst3Root $relative.Substring(5)
        } elseif ($relative.StartsWith('CLAP\', [StringComparison]::OrdinalIgnoreCase)) {
            $destination = Join-Path $ClapRoot $relative.Substring(5)
        } elseif ($relative.StartsWith('Standalone\', [StringComparison]::OrdinalIgnoreCase)) {
            $destination = Join-Path $StandaloneRoot $relative.Substring(11)
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
        ([string]$_.path).Replace('/', '\') -match '^(?i:VST3\\Spectr\.vst3\\|CLAP\\|Standalone\\)'
    }).Count
    if ($installedFiles.Count -ne $expectedCount) { throw 'Installed file receipt is incomplete' }
    $installedShortcuts = @()
    foreach ($item in @($plan | Select-Object -Skip $artifactCount)) {
        if (-not (Test-Path -LiteralPath $item.destination -PathType Leaf)) { throw "Installed shortcut is missing: $($item.destination)" }
        $hash = (Get-FileHash -LiteralPath $item.destination -Algorithm SHA256).Hash
        if ($hash -ne $item.expected_hash) { throw "Installed shortcut differs from staged shortcut: $($item.destination)" }
        $installedShortcuts += [ordered]@{ destination = $item.destination; sha256 = $hash }
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
        shortcuts = $installedShortcuts
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
    foreach ($item in @($plan | Where-Object { $_.active })) {
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
} finally {
    foreach ($stream in $lockStreams) { $stream.Dispose() }
}
