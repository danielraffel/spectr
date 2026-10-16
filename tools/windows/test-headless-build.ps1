[CmdletBinding()]
param(
    [string]$Helper = (Join-Path $PSScriptRoot 'headless-build.ps1'),
    [string]$SourceDir = 'C:\spectr-gui',
    [string]$BuildDir = 'C:\builds\spectr-arm64-gpu',
    [string]$GovernedBuild = 'C:\tmp\spectr-headless-fixture-governed.sh',
    [string]$FixtureRoot = 'C:\tmp\spectr-headless-fixture'
)

$ErrorActionPreference = 'Stop'

function Assert-PowerShellParses([string]$Path) {
    $resolved = Resolve-Path -LiteralPath $Path
    $tokens = $null
    $parseErrors = $null
    [System.Management.Automation.Language.Parser]::ParseFile(
        $resolved,
        [ref]$tokens,
        [ref]$parseErrors) | Out-Null
    if ($parseErrors -and $parseErrors.Count -gt 0) {
        $details = ($parseErrors | ForEach-Object { "$($_.Extent): $($_.Message)" }) -join '; '
        throw "PowerShell parse failed for $resolved`: $details"
    }
}

# Keep the Windows fixture fail-closed before it creates any source/build
# fixtures. This catches parser regressions in both the helper and this test
# harness instead of discarding Parser::ParseFile's error output.
Assert-PowerShellParses $Helper
Assert-PowerShellParses $PSCommandPath

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

function Invoke-Helper(
    [string]$Target,
    [string]$Receipt,
    [string]$InvokeSource = $SourceDir,
    [string]$InvokeBuild = $BuildDir,
    [string]$InvokeGovernedBuild = $GovernedBuild,
    [string]$InvokeTestCase = 'Spectr processes audio',
    [string]$InvokeTestExecutable = ''
) {
    $output = Join-Path $FixtureRoot ("$Target-helper-output.log")
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Helper `
        -SourceDir $InvokeSource -BuildDir $InvokeBuild -VsVars $vsVars `
        -GovernedBuild $InvokeGovernedBuild -Bash $bash -TargetArchitecture $Target `
        -TestCase $InvokeTestCase -TestExecutable $InvokeTestExecutable `
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

function New-GitSourceFixture([string]$Path) {
    New-Item -ItemType Directory -Force -Path $Path | Out-Null
    'fixture source' | Set-Content -LiteralPath (Join-Path $Path 'source-marker.txt') -Encoding ascii
    & git -C $Path init --quiet
    if ($LASTEXITCODE -ne 0) { throw "unable to initialize source fixture: $Path" }
    & git -C $Path config user.name 'headless fixture'
    & git -C $Path config user.email 'headless-fixture@example.invalid'
    & git -C $Path add source-marker.txt
    if ($LASTEXITCODE -ne 0) { throw "unable to stage source fixture: $Path" }
    & git -C $Path commit --quiet -m 'fixture source'
    if ($LASTEXITCODE -ne 0) { throw "unable to commit source fixture: $Path" }
}

$spaceBuild = Join-Path $FixtureRoot 'build with spaces'
Copy-FixtureBuild $spaceBuild $SourceDir
$BuildDir = $spaceBuild

$positiveReceipt = Join-Path $FixtureRoot 'positive-receipt.json'
$positiveTestExecutable = Join-Path $spaceBuild 'Spectr-test.exe'
$positiveExit = Invoke-Helper 'arm64' $positiveReceipt $SourceDir $spaceBuild $GovernedBuild 'Spectr processes audio' $positiveTestExecutable
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
if ($positive.focused_test.command_line -notmatch [regex]::Escape('"' + $positiveTestExecutable + '"') -or
    $positive.focused_test.command_line -notmatch [regex]::Escape('"Spectr processes audio"')) {
    throw 'focused test command did not preserve quoted executable and test-case arguments'
}
if ($positive.toolchain.host_architecture -notin @('arm64', 'x64') -or $positive.toolchain.target_architecture -ne 'arm64') {
    throw 'positive receipt lacks explicit tool-host/target selection'
}
foreach ($artifact in $positive.artifacts.psobject.Properties.Value) {
    if ($artifact.pe_machine -ne '0xAA64') { throw "positive artifact machine mismatch: $($artifact.pe_machine)" }
}

$unsafeCaseReceipt = Join-Path $FixtureRoot 'unsafe-test-case-negative-receipt.json'
$unsafeCaseExit = Invoke-Helper 'arm64' $unsafeCaseReceipt $SourceDir $spaceBuild $GovernedBuild 'Spectr processes audio & echo injected'
if ($unsafeCaseExit -eq 0) { throw 'unsafe TestCase fixture unexpectedly passed' }
$unsafeCase = Get-Content -LiteralPath $unsafeCaseReceipt -Raw | ConvertFrom-Json
if ($unsafeCase.status -ne 'FAIL' -or $unsafeCase.failure.phase -ne 'provenance' -or $unsafeCase.failure.message -notmatch 'TestCase contains unsupported shell metacharacters') {
    throw 'unsafe TestCase fixture lacks command-argument validation evidence'
}

$unsafeExecutableReceipt = Join-Path $FixtureRoot 'unsafe-test-executable-negative-receipt.json'
$unsafeExecutable = "$positiveTestExecutable`&invalid"
$unsafeExecutableExit = Invoke-Helper 'arm64' $unsafeExecutableReceipt $SourceDir $spaceBuild $GovernedBuild 'Spectr processes audio' $unsafeExecutable
if ($unsafeExecutableExit -eq 0) { throw 'unsafe TestExecutable fixture unexpectedly passed' }
$unsafeExecutableResult = Get-Content -LiteralPath $unsafeExecutableReceipt -Raw | ConvertFrom-Json
if ($unsafeExecutableResult.status -ne 'FAIL' -or $unsafeExecutableResult.failure.phase -ne 'provenance' -or $unsafeExecutableResult.failure.message -notmatch 'TestExecutable contains unsupported shell metacharacters') {
    throw 'unsafe TestExecutable fixture lacks command-argument validation evidence'
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

$mutationId = [guid]::NewGuid().ToString('N')
$mutationSource = Join-Path $FixtureRoot "mutation-source-$mutationId"
New-GitSourceFixture $mutationSource

$mutationBuild = Join-Path $FixtureRoot "mutation-build-$mutationId"
Copy-FixtureBuild $mutationBuild $mutationSource
$mutationMarker = Join-Path $mutationSource 'source-marker.txt'
$mutationMarkerPosix = ($mutationMarker -replace '\', '/') -replace '^C:', '/c'
$mutationGoverned = Join-Path $FixtureRoot 'mutation-governed.sh'
@"
#!/usr/bin/env bash
printf 'mutated during build\n' > "$mutationMarkerPosix"
exit 0
"@ | Set-Content -LiteralPath $mutationGoverned -Encoding ascii
$mutationReceipt = Join-Path $FixtureRoot 'source-mutation-negative-receipt.json'
$mutationExit = Invoke-Helper 'arm64' $mutationReceipt $mutationSource $mutationBuild $mutationGoverned
if ($mutationExit -eq 0) { throw 'source mutation fixture unexpectedly passed' }
$mutation = Get-Content -LiteralPath $mutationReceipt -Raw | ConvertFrom-Json
if ($mutation.status -ne 'FAIL' -or $mutation.failure.phase -ne 'source-revalidation-after-build' -or $mutation.failure.message -notmatch 'Source checkout changed during build') {
    throw 'source mutation receipt lacks post-build provenance evidence'
}

$headMoveId = [guid]::NewGuid().ToString('N')
$headMoveSource = Join-Path $FixtureRoot "head-move-source-$headMoveId"
New-GitSourceFixture $headMoveSource
$headMoveBuild = Join-Path $FixtureRoot "head-move-build-$headMoveId"
Copy-FixtureBuild $headMoveBuild $headMoveSource
$headMoveMarkerPosix = ((Join-Path $headMoveSource 'source-marker.txt') -replace '\', '/') -replace '^C:', '/c'
$headMoveSourcePosix = ($headMoveSource -replace '\', '/') -replace '^C:', '/c'
$headMoveGoverned = Join-Path $FixtureRoot "head-move-governed-$headMoveId.sh"
@"
#!/usr/bin/env bash
printf 'head moved during build\n' > "$headMoveMarkerPosix"
git -C "$headMoveSourcePosix" add source-marker.txt
git -C "$headMoveSourcePosix" commit --quiet -m 'head moved during build'
exit 0
"@ | Set-Content -LiteralPath $headMoveGoverned -Encoding ascii
$headMoveReceipt = Join-Path $FixtureRoot 'source-head-move-negative-receipt.json'
$headMoveExit = Invoke-Helper 'arm64' $headMoveReceipt $headMoveSource $headMoveBuild $headMoveGoverned
if ($headMoveExit -eq 0) { throw 'source HEAD move fixture unexpectedly passed' }
$headMove = Get-Content -LiteralPath $headMoveReceipt -Raw | ConvertFrom-Json
if ($headMove.status -ne 'FAIL' -or $headMove.failure.phase -ne 'source-revalidation-after-build' -or $headMove.failure.message -notmatch 'Source HEAD changed during build') {
    throw 'source HEAD move receipt lacks SHA revalidation evidence'
}

[ordered]@{
    schema = 1
    status = 'PASS'
    positive_receipt = $positiveReceipt
    unsafe_test_case_negative_receipt = $unsafeCaseReceipt
    unsafe_test_executable_negative_receipt = $unsafeExecutableReceipt
    arm64ec_positive_receipt = $arm64ecReceipt
    x64_positive_receipt = $x64Receipt
    plain_x64_negative_receipt = $negativeReceipt
    cache_mismatch_negative_receipt = $mismatchReceipt
    source_mutation_negative_receipt = $mutationReceipt
    source_head_move_negative_receipt = $headMoveReceipt
    governed_invocation = (Get-Content -LiteralPath $invocationPath -Raw).Trim()
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $FixtureRoot 'fixture-receipt.json') -Encoding utf8
Write-Output "PASS: $FixtureRoot"
