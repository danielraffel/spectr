[CmdletBinding()]
param(
    [string]$Helper = (Join-Path $PSScriptRoot 'headless-build.ps1'),
    [string]$SourceDir = 'C:\spectr-gui',
    [string]$BuildDir = 'C:\builds\spectr-arm64-gpu',
    [string]$GovernedBuild = 'C:\tmp\spectr-headless-fixture-governed.sh',
    [string]$FixtureRoot = 'C:\tmp\spectr-headless-fixture'
)

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $FixtureRoot | Out-Null
$invocationPath = Join-Path $FixtureRoot 'governed-invocation.txt'
$fixturePosix = ($FixtureRoot -replace '\\', '/') -replace '^C:', '/c'
$governedFixture = @'
#!/usr/bin/env bash
printf '<%s>\n' "$@" > "__INVOCATION__"
exit 0
'@
$governedFixture = $governedFixture.Replace('__INVOCATION__', "$fixturePosix/governed-invocation.txt")
$governedFixture | Set-Content -LiteralPath $GovernedBuild -Encoding ascii
$bash = 'C:\Program Files\Git\bin\bash.exe'
$vsVars = 'C:\VS18BuildTools\VC\Auxiliary\Build\vcvarsall.bat'

function Invoke-Helper([string]$Target, [string]$Receipt) {
    $output = Join-Path $FixtureRoot ("$Target-helper-output.log")
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Helper `
        -SourceDir $SourceDir -BuildDir $BuildDir -VsVars $vsVars `
        -GovernedBuild $GovernedBuild -Bash $bash -TargetArchitecture $Target `
        -Receipt $Receipt *> $output
    $code = $LASTEXITCODE
    $ErrorActionPreference = $saved
    return $code
}

function Copy-FixtureBuild([string]$Destination, [string]$CacheSource) {
    New-Item -ItemType Directory -Force -Path $Destination, (Join-Path $Destination 'VST3'), (Join-Path $Destination 'CLAP') | Out-Null
    Copy-Item -LiteralPath (Join-Path $BuildDir 'Spectr.exe') -Destination $Destination
    Copy-Item -LiteralPath (Join-Path $BuildDir 'Spectr-test.exe') -Destination $Destination
    Copy-Item -LiteralPath (Join-Path $BuildDir 'VST3\Spectr.dll') -Destination (Join-Path $Destination 'VST3')
    Copy-Item -LiteralPath (Join-Path $BuildDir 'CLAP\Spectr.clap') -Destination (Join-Path $Destination 'CLAP')
    "CMAKE_HOME_DIRECTORY:INTERNAL=$CacheSource`n" | Set-Content -LiteralPath (Join-Path $Destination 'CMakeCache.txt') -Encoding ascii
}

function Set-PeMachine([string]$Path, [UInt16]$Machine, [bool]$Arm64X) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    [Array]::Copy([BitConverter]::GetBytes($Machine), 0, $bytes, $peOffset + 4, 2)
    if ($Arm64X) {
        $sectionCount = [BitConverter]::ToUInt16($bytes, $peOffset + 6)
        $optionalSize = [BitConverter]::ToUInt16($bytes, $peOffset + 20)
        $table = $peOffset + 24 + $optionalSize
        $renamed = $false
        for ($index = 0; $index -lt $sectionCount; $index++) {
            $offset = $table + ($index * 40)
            if ($offset + 8 -gt $bytes.Length) { break }
            $name = [Text.Encoding]::ASCII.GetString($bytes, $offset, 8).Trim([char]0)
            if ($name -ne '.a64xrm') {
                $nameBytes = [Text.Encoding]::ASCII.GetBytes('.a64xrm' + [char]0)
                [Array]::Copy($nameBytes, 0, $bytes, $offset, $nameBytes.Length)
                $renamed = $true
                break
            }
        }
        if (-not $renamed) { throw "Unable to add ARM64X fixture section: $Path" }
    } else {
        $sectionCount = [BitConverter]::ToUInt16($bytes, $peOffset + 6)
        $optionalSize = [BitConverter]::ToUInt16($bytes, $peOffset + 20)
        $table = $peOffset + 24 + $optionalSize
        for ($index = 0; $index -lt $sectionCount; $index++) {
            $offset = $table + ($index * 40)
            if ($offset + 8 -gt $bytes.Length) { break }
            $name = [Text.Encoding]::ASCII.GetString($bytes, $offset, 8).Trim([char]0)
            if ($name -eq '.a64xrm') {
                $nameBytes = [Text.Encoding]::ASCII.GetBytes('.fixture' + [char]0)
                [Array]::Copy($nameBytes, 0, $bytes, $offset, $nameBytes.Length)
                break
            }
        }
    }
    [IO.File]::WriteAllBytes($Path, $bytes)
}

$spaceBuild = Join-Path $FixtureRoot 'build with spaces'
Copy-FixtureBuild $spaceBuild $SourceDir
$BuildDir = $spaceBuild

$positiveReceipt = Join-Path $FixtureRoot 'positive-receipt.json'
$positiveExit = Invoke-Helper 'arm64' $positiveReceipt
if ($positiveExit -ne 0) { throw "positive fixture failed with exit $positiveExit" }
$positive = Get-Content -LiteralPath $positiveReceipt -Raw | ConvertFrom-Json
if ($positive.status -ne 'PASS') { throw 'positive fixture did not produce PASS receipt' }
if ($positive.focused_test.assertions -lt 1 -or $positive.focused_test.test_cases -lt 1) {
    throw 'positive fixture did not parse dynamic test counts'
}
if ((Get-Content -LiteralPath $invocationPath -Raw) -match '--parallel|-j') {
    throw 'governed invocation carried an unbounded parallelism flag'
}
if ((Get-Content -LiteralPath $invocationPath -Raw) -notmatch [regex]::Escape($spaceBuild)) {
    throw 'governed invocation did not preserve the space-containing build path as one argument'
}
if ($positive.toolchain.host_architecture -notin @('arm64', 'x64') -or $positive.toolchain.target_architecture -ne 'arm64') {
    throw 'positive receipt lacks explicit tool-host/target selection'
}
foreach ($artifact in $positive.artifacts.psobject.Properties.Value) {
    if ($artifact.pe_machine -ne '0xAA64') { throw "positive artifact machine mismatch: $($artifact.pe_machine)" }
}

$arm64ecBuild = Join-Path $FixtureRoot 'arm64ec build'
Copy-FixtureBuild $arm64ecBuild $SourceDir
foreach ($artifact in @('Spectr.exe', 'VST3\Spectr.dll', 'CLAP\Spectr.clap')) {
    Set-PeMachine (Join-Path $arm64ecBuild $artifact) 0x8664 $true
}
$BuildDir = $arm64ecBuild
$arm64ecReceipt = Join-Path $FixtureRoot 'arm64ec-positive-receipt.json'
$arm64ecExit = Invoke-Helper 'arm64ec' $arm64ecReceipt
if ($arm64ecExit -ne 0) { throw "ARM64EC positive fixture failed with exit $arm64ecExit" }
$arm64ec = Get-Content -LiteralPath $arm64ecReceipt -Raw | ConvertFrom-Json
if ($arm64ec.status -ne 'PASS' -or $arm64ec.target.package_policy -ne 'arm64ec-win') { throw 'ARM64EC positive fixture lacked PASS policy evidence' }
foreach ($artifact in $arm64ec.artifacts.psobject.Properties.Value) {
    if ($artifact.pe_machine -ne '0x8664' -or -not $artifact.arm64x_section) { throw 'ARM64EC fixture lacks PE8664 plus ARM64X evidence' }
}

$plainX64Build = Join-Path $FixtureRoot 'plain x64 build'
Copy-FixtureBuild $plainX64Build $SourceDir
foreach ($artifact in @('Spectr.exe', 'VST3\Spectr.dll', 'CLAP\Spectr.clap')) {
    Set-PeMachine (Join-Path $plainX64Build $artifact) 0x8664 $false
}
$BuildDir = $plainX64Build
$x64Receipt = Join-Path $FixtureRoot 'x64-positive-receipt.json'
$x64Exit = Invoke-Helper 'x64' $x64Receipt
if ($x64Exit -ne 0) { throw "x64 positive fixture failed with exit $x64Exit" }
$x64 = Get-Content -LiteralPath $x64Receipt -Raw | ConvertFrom-Json
if ($x64.toolchain.host_architecture -ne 'arm64' -or $x64.toolchain.target_architecture -ne 'x64' -or $x64.toolchain.vcvars_arguments -ne 'arm64_x64') {
    throw 'x64 fixture did not prove ARM64-host/x64-target tool selection'
}
$negativeReceipt = Join-Path $FixtureRoot 'plain-x64-negative-receipt.json'
$negativeExit = Invoke-Helper 'arm64ec' $negativeReceipt
if ($negativeExit -eq 0) { throw 'plain x64 ARM64EC fixture unexpectedly passed' }
$negative = Get-Content -LiteralPath $negativeReceipt -Raw | ConvertFrom-Json
if ($negative.status -ne 'FAIL' -or $negative.failure.phase -ne 'artifact-verification') { throw 'plain x64 negative fixture lacked artifact failure evidence' }
if ($negative.failure.message -notmatch 'ARM64EC payload lacks ARM64X') { throw 'plain x64 negative receipt lacks ARM64X mismatch evidence' }
if (-not (Test-Path -LiteralPath $negative.evidence_dir)) { throw 'negative evidence directory was deleted' }
foreach ($log in @($negative.build.log, $negative.focused_test.log)) {
    if (-not (Test-Path -LiteralPath $log -PathType Leaf)) { throw "negative log was not retained: $log" }
}

$mismatchBuild = Join-Path $FixtureRoot 'cache mismatch build'
Copy-FixtureBuild $mismatchBuild 'C:\spectr-unrelated-source'
$BuildDir = $mismatchBuild
$mismatchReceipt = Join-Path $FixtureRoot 'cache-mismatch-negative-receipt.json'
$mismatchExit = Invoke-Helper 'arm64' $mismatchReceipt
if ($mismatchExit -eq 0) { throw 'cache mismatch fixture unexpectedly passed' }
$mismatch = Get-Content -LiteralPath $mismatchReceipt -Raw | ConvertFrom-Json
if ($mismatch.status -ne 'FAIL' -or $mismatch.failure.phase -ne 'provenance' -or $mismatch.failure.message -notmatch 'does not match SourceDir') {
    throw 'cache mismatch receipt lacks source binding evidence'
}

[ordered]@{
    schema = 1
    status = 'PASS'
    positive_receipt = $positiveReceipt
    arm64ec_positive_receipt = $arm64ecReceipt
    x64_positive_receipt = $x64Receipt
    plain_x64_negative_receipt = $negativeReceipt
    cache_mismatch_negative_receipt = $mismatchReceipt
    governed_invocation = (Get-Content -LiteralPath $invocationPath -Raw).Trim()
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $FixtureRoot 'fixture-receipt.json') -Encoding utf8
Write-Output "PASS: $FixtureRoot"
