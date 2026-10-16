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
printf '%s\n' "$*" > __INVOCATION__
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
foreach ($artifact in $positive.artifacts.psobject.Properties.Value) {
    if ($artifact.pe_machine -ne '0xAA64') { throw "positive artifact machine mismatch: $($artifact.pe_machine)" }
}

$negativeReceipt = Join-Path $FixtureRoot 'negative-receipt.json'
$negativeExit = Invoke-Helper 'arm64ec' $negativeReceipt
if ($negativeExit -eq 0) { throw 'negative target fixture unexpectedly passed' }
$negative = Get-Content -LiteralPath $negativeReceipt -Raw | ConvertFrom-Json
if ($negative.status -ne 'FAIL') { throw 'negative fixture did not produce FAIL receipt' }
if ($negative.failure.phase -ne 'artifact-verification') { throw "unexpected negative phase: $($negative.failure.phase)" }
if ($negative.failure.message -notmatch 'does not match target') { throw 'negative receipt lacks target mismatch evidence' }
if (-not (Test-Path -LiteralPath $negative.evidence_dir)) { throw 'negative evidence directory was deleted' }
foreach ($log in @($negative.build.log, $negative.focused_test.log)) {
    if (-not (Test-Path -LiteralPath $log -PathType Leaf)) { throw "negative log was not retained: $log" }
}

[ordered]@{
    schema = 1
    status = 'PASS'
    positive_receipt = $positiveReceipt
    negative_receipt = $negativeReceipt
    governed_invocation = (Get-Content -LiteralPath $invocationPath -Raw).Trim()
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $FixtureRoot 'fixture-receipt.json') -Encoding utf8
Write-Output "PASS: $FixtureRoot"
