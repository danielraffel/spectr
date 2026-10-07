[CmdletBinding()]
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\..\build-win'),
    [string]$Receipt = (Join-Path $BuildDir 'windows-validation-receipt.json'),
    [switch]$Build
)

$ErrorActionPreference = 'Stop'
$BuildDir = (Resolve-Path $BuildDir).Path

function Require-File([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label is missing: $Path"
    }
    return (Get-Item -LiteralPath $Path)
}

if ($Build) {
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        throw 'cmake is required for -Build'
    }
    cmake --build $BuildDir --parallel 4
    if ($LASTEXITCODE -ne 0) { throw "cmake build failed with exit $LASTEXITCODE" }
}

$testExe = Require-File (Join-Path $BuildDir 'Spectr-test.exe') 'Spectr test executable'
$clap = Require-File (Join-Path $BuildDir 'CLAP\Spectr.clap') 'Spectr CLAP artifact'
$vst3Binary = Require-File (Join-Path $BuildDir 'VST3\Spectr.vst3\Contents\x86_64-win\Spectr.vst3') 'Spectr VST3 binary'
$standalone = Require-File (Join-Path $BuildDir 'Spectr.exe') 'Spectr standalone executable'

$log = Join-Path $BuildDir 'windows-validation.log'
$savedErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& $testExe 'Spectr processes audio' '--reporter' 'compact' 2>&1 | Out-File -LiteralPath $log -Encoding utf8
$testExit = $LASTEXITCODE
$ErrorActionPreference = $savedErrorActionPreference
if ($testExit -ne 0) {
    throw "focused Windows audio test failed with exit $testExit; see $log"
}

$gitSha = $null
if (Get-Command git -ErrorAction SilentlyContinue) {
    $gitSha = (git -C (Split-Path $BuildDir -Parent) rev-parse HEAD 2>$null)
    if ($LASTEXITCODE -ne 0) { $gitSha = $null }
}

$receiptObject = [ordered]@{
    schema = 1
    generated_at_utc = (Get-Date).ToUniversalTime().ToString('o')
    architecture = $env:PROCESSOR_ARCHITECTURE
    git_sha = $gitSha
    focused_test = 'Spectr processes audio'
    focused_test_exit = $testExit
    artifacts = [ordered]@{
        clap = [ordered]@{ path = $clap.FullName; bytes = $clap.Length; sha256 = (Get-FileHash $clap.FullName -Algorithm SHA256).Hash }
        vst3_binary = [ordered]@{ path = $vst3Binary.FullName; bytes = $vst3Binary.Length; sha256 = (Get-FileHash $vst3Binary.FullName -Algorithm SHA256).Hash }
        standalone = [ordered]@{ path = $standalone.FullName; bytes = $standalone.Length; sha256 = (Get-FileHash $standalone.FullName -Algorithm SHA256).Hash }
    }
    limitations = @('This helper does not claim DAW loading or GUI proof.', 'CTest registration alone is not accepted as a pass.')
}

$receiptObject | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Receipt -Encoding utf8
Write-Output "PASS: $Receipt"
