[CmdletBinding()]
param(
    [string]$SourceDir = 'C:\spectr-gui',
    [string]$BuildDir = 'C:\builds\spectr-arm64-gpu',
    [string]$VsVars = 'C:\VS18BuildTools\VC\Auxiliary\Build\vcvarsall.bat',
    [ValidateSet('arm64', 'arm64ec', 'x64')]
    [string]$TargetArchitecture = 'arm64',
    [string]$GovernedBuild = $(if ($env:SPECTR_GOVERNED_BUILD) { $env:SPECTR_GOVERNED_BUILD } else { 'C:\pulp\tools\ci\governed-build.sh' }),
    [string]$Bash = 'C:\Program Files\Git\bin\bash.exe',
    [string]$TestCase = 'Spectr processes audio',
    [string]$Receipt = 'C:\builds\spectr-arm64-gpu\headless-build-receipt.json'
)

$ErrorActionPreference = 'Stop'
$runId = [guid]::NewGuid().ToString('N')
$receiptParent = Split-Path -Parent $Receipt
if (-not $receiptParent) { $receiptParent = (Get-Location).Path }
New-Item -ItemType Directory -Force -Path $receiptParent | Out-Null
$runDir = Join-Path $receiptParent ("headless-build-$((Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ'))-$runId")
New-Item -ItemType Directory -Force -Path $runDir | Out-Null
$buildLog = Join-Path $runDir 'build.log'
$testLog = Join-Path $runDir 'test.log'
$buildCmd = Join-Path $runDir 'build.cmd'
$testCmd = Join-Path $runDir 'test.cmd'
$phase = 'initialization'
$buildExit = $null
$testExit = $null
$receiptObject = [ordered]@{
    schema = 2
    status = 'FAIL'
    run_id = $runId
    generated_at_utc = (Get-Date).ToUniversalTime().ToString('o')
    evidence_dir = $runDir
    source = [ordered]@{ directory = $SourceDir; git_sha = $null; clean = $false }
    host = [ordered]@{
        architecture = $null
        process_architecture = [Environment]::GetEnvironmentVariable('PROCESSOR_ARCHITECTURE')
        process_architecture_override = [Environment]::GetEnvironmentVariable('PROCESSOR_ARCHITEW6432')
    }
    target = [ordered]@{ requested_architecture = $TargetArchitecture; pe_machine = $null; compiler_environment_target = $null }
    toolchain = [ordered]@{ vcvars = $VsVars; governed_build = $GovernedBuild; governed_build_sha256 = $null; bash = $Bash }
    build = [ordered]@{ exit = $null; command = $null; log = $buildLog }
    focused_test = [ordered]@{ name = $TestCase; exit = $null; summary = $null; assertions = $null; test_cases = $null; log = $testLog }
    artifacts = [ordered]@{}
    limitations = @('Headless build/test proves compilation and focused DSP behavior only.', 'It does not claim DAW loading, GUI, screenshot, modulation, audio-device behavior, or release acceptance.')
}

function Write-Receipt {
    $temporary = "$Receipt.$runId.tmp"
    $receiptObject | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $temporary -Encoding utf8
    Move-Item -LiteralPath $temporary -Destination $Receipt -Force
}

function Invoke-Git([string[]]$Arguments) {
    $output = & git @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw "git $($Arguments -join ' ') failed: $($output -join ' ')" }
    return (($output -join "`n").Trim())
}

function Get-HostArchitecture {
    $override = [Environment]::GetEnvironmentVariable('PROCESSOR_ARCHITEW6432')
    if ($override) { return $override }
    return [Environment]::GetEnvironmentVariable('PROCESSOR_ARCHITECTURE')
}

function Get-PeMachine([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    try {
        $reader = New-Object IO.BinaryReader($stream)
        if ($reader.ReadUInt16() -ne 0x5a4d) { throw "Not a PE file: $Path" }
        $stream.Position = 0x3c
        $peOffset = $reader.ReadInt32()
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) { throw "Invalid PE signature: $Path" }
        return ('0x{0:X4}' -f $reader.ReadUInt16())
    } finally { $stream.Dispose() }
}

$machineByArchitecture = @{ arm64 = '0xAA64'; arm64ec = '0xA641'; x64 = '0x8664' }
$receiptObject.host.architecture = Get-HostArchitecture
$receiptObject.target.pe_machine = $machineByArchitecture[$TargetArchitecture]
$receiptObject.target.compiler_environment_target = if ($TargetArchitecture -eq 'arm64ec') { 'arm64' } else { $TargetArchitecture }

try {
    $phase = 'provenance'
    foreach ($path in @($SourceDir, $BuildDir, $VsVars, $GovernedBuild, $Bash)) {
        if (-not (Test-Path -LiteralPath $path)) { throw "Required path is missing: $path" }
    }
    $receiptObject.toolchain.governed_build_sha256 = (Get-FileHash $GovernedBuild -Algorithm SHA256).Hash
    $sourceSha = Invoke-Git @('-C', $SourceDir, 'rev-parse', '--verify', 'HEAD')
    $sourceStatus = Invoke-Git @('-C', $SourceDir, 'status', '--porcelain')
    if ($sourceStatus) { throw "Source checkout is not clean: $SourceDir" }
    $receiptObject.source.git_sha = $sourceSha
    $receiptObject.source.clean = $true

    $phase = 'build'
    $vsTarget = $receiptObject.target.compiler_environment_target
    $buildArguments = @('cmake', '--build', $BuildDir, '--target', 'Spectr_Standalone', 'Spectr_VST3', 'Spectr_CLAP', 'Spectr-test')
    $receiptObject.build.command = "bash `"$GovernedBuild`" $($buildArguments -join ' ')"
    @('@echo off', "call `"$VsVars`" $vsTarget", 'if errorlevel 1 exit /b %ERRORLEVEL%', "`"$Bash`" `"$GovernedBuild`" $($buildArguments -join ' ')", 'exit /b %ERRORLEVEL%') |
        Set-Content -LiteralPath $buildCmd -Encoding ascii
    & cmd.exe /d /c "`"$buildCmd`" >`"$buildLog`" 2>&1"
    $buildExit = $LASTEXITCODE
    $receiptObject.build.exit = $buildExit
    if ($buildExit -ne 0) { throw "governed Windows build failed with exit $buildExit; see $buildLog" }

    $phase = 'focused-test'
    $testExe = Join-Path $BuildDir 'Spectr-test.exe'
    if (-not (Test-Path -LiteralPath $testExe -PathType Leaf)) { throw "Missing test executable: $testExe" }
    @('@echo off', "call `"$VsVars`" $vsTarget", 'if errorlevel 1 exit /b %ERRORLEVEL%', "`"$testExe`" `"$TestCase`" --reporter compact", 'exit /b %ERRORLEVEL%') |
        Set-Content -LiteralPath $testCmd -Encoding ascii
    & cmd.exe /d /c "`"$testCmd`" >`"$testLog`" 2>&1"
    $testExit = $LASTEXITCODE
    $receiptObject.focused_test.exit = $testExit
    $testText = Get-Content -LiteralPath $testLog -Raw
    $receiptObject.focused_test.summary = (($testText -split "`r?`n") | Where-Object { $_ -match 'assertions? in .*test cases?' } | Select-Object -Last 1)
    $summaryMatch = [regex]::Match($testText, '(?i)(\d+)\s+assertions?\s+in\s+(\d+)\s+test cases?')
    if (-not $summaryMatch.Success) { throw "Focused test output lacks a parseable assertion/test-case summary; see $testLog" }
    $receiptObject.focused_test.assertions = [int]$summaryMatch.Groups[1].Value
    $receiptObject.focused_test.test_cases = [int]$summaryMatch.Groups[2].Value
    if ($testExit -ne 0) { throw "focused Windows audio test failed with exit $testExit; see $testLog" }

    $phase = 'artifact-verification'
    foreach ($item in @(
        @{ name = 'standalone'; path = (Join-Path $BuildDir 'Spectr.exe') },
        @{ name = 'vst3'; path = (Join-Path $BuildDir 'VST3\Spectr.dll') },
        @{ name = 'clap'; path = (Join-Path $BuildDir 'CLAP\Spectr.clap') }
    )) {
        if (-not (Test-Path -LiteralPath $item.path -PathType Leaf)) { throw "Missing artifact: $($item.path)" }
        $file = Get-Item -LiteralPath $item.path
        $machine = Get-PeMachine $file.FullName
        if ($machine -ne $receiptObject.target.pe_machine) { throw "$($item.name) PE machine $machine does not match target $TargetArchitecture ($($receiptObject.target.pe_machine))" }
        $receiptObject.artifacts[$item.name] = [ordered]@{ path = $file.FullName; bytes = $file.Length; sha256 = (Get-FileHash $file.FullName -Algorithm SHA256).Hash; pe_machine = $machine }
    }

    $receiptObject.status = 'PASS'
    Write-Receipt
    Write-Output "PASS: $Receipt"
    Get-Content -LiteralPath $testLog
    exit 0
} catch {
    $receiptObject.failure = [ordered]@{ phase = $phase; message = $_.Exception.Message; type = $_.Exception.GetType().FullName }
    $receiptObject.build.exit = $buildExit
    $receiptObject.focused_test.exit = $testExit
    try { Write-Receipt } catch { Write-Error "Unable to write failure receipt $Receipt`: $($_.Exception.Message)" }
    Write-Error "FAIL [$phase]: $($_.Exception.Message)"
    Write-Error "Durable evidence: $runDir"
    exit 1
}
