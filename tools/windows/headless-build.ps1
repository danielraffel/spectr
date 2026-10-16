[CmdletBinding()]
param(
    [string]$SourceDir = 'C:\spectr-gui',
    [string]$BuildDir = 'C:\builds\spectr-arm64-gpu',
    [string]$VsVars = 'C:\VS18BuildTools\VC\Auxiliary\Build\vcvarsall.bat',
    [ValidateSet('arm64', 'arm64ec', 'x64')]
    [string]$TargetArchitecture = 'arm64',
    [ValidateSet('auto', 'arm64', 'x64')]
    [string]$ToolHostArchitecture = 'auto',
    [string]$GovernedBuild = $(if ($env:SPECTR_GOVERNED_BUILD) { $env:SPECTR_GOVERNED_BUILD } else { 'C:\pulp\tools\ci\governed-build.sh' }),
    [string]$Bash = 'C:\Program Files\Git\bin\bash.exe',
    [string]$TestCase = 'Spectr processes audio',
    [string]$TestExecutable = '',
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
    schema = 3
    status = 'FAIL'
    run_id = $runId
    generated_at_utc = (Get-Date).ToUniversalTime().ToString('o')
    evidence_dir = $runDir
    source = [ordered]@{ directory = $SourceDir; git_sha = $null; clean = $false; git_sha_after_build = $null; clean_after_build = $false; git_sha_final = $null; clean_final = $false; cmake_home_directory = $null }
    host = [ordered]@{
        architecture = $null
        process_architecture = [Environment]::GetEnvironmentVariable('PROCESSOR_ARCHITECTURE')
        process_architecture_override = [Environment]::GetEnvironmentVariable('PROCESSOR_ARCHITEW6432')
    }
    target = [ordered]@{ requested_architecture = $TargetArchitecture; package_policy = $null; pe_machines = @(); arm64x_section_required = $false }
    toolchain = [ordered]@{ vcvars = $VsVars; vcvars_arguments = $null; governed_build = $GovernedBuild; governed_build_sha256 = $null; bash = $Bash; host_architecture = $null; target_architecture = $null }
    build = [ordered]@{ exit = $null; arguments = $null; command_line = $null; log = $buildLog }
    focused_test = [ordered]@{ name = $TestCase; executable = $null; sha256 = $null; exit = $null; summary = $null; assertions = $null; test_cases = $null; command_line = $null; log = $testLog }
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
    if ($override) { return $override.ToUpperInvariant() }
    return ([Environment]::GetEnvironmentVariable('PROCESSOR_ARCHITECTURE')).ToUpperInvariant()
}

function Get-CanonicalPath([string]$Path) {
    $fullPath = [IO.Path]::GetFullPath($Path)
    if (Test-Path -LiteralPath $Path) {
        $fullPath = (Resolve-Path -LiteralPath $Path).Path
    }
    return $fullPath.TrimEnd('\', '/')
}

function Assert-SafeCommandArgument([string]$Value, [string]$Label) {
    if ($null -eq $Value -or $Value -match '[\x00-\x1F\x7F&|<>^%!"`]') { throw "$Label contains unsupported shell metacharacters: $Value" }
}

function Quote-CmdArgument([string]$Value, [string]$Label) {
    Assert-SafeCommandArgument $Value $Label
    return '"' + $Value + '"'
}

function Get-VsInvocation([string]$HostArch, [string]$TargetArch) {
    $hostFamily = if ($HostArch -match '^ARM64$') { 'arm64' } else { 'x64' }
    $targetFamily = if ($TargetArch -eq 'x64') { 'x64' } else { 'arm64' }
    if ($hostFamily -eq $targetFamily) { return $targetFamily }
    return "$hostFamily`_$targetFamily"
}

function Get-CMakeHomeDirectory([string]$Path) {
    $cache = Join-Path $Path 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cache -PathType Leaf)) { throw "CMakeCache.txt is missing: $cache" }
    $line = Get-Content -LiteralPath $cache | Where-Object { $_ -match '^CMAKE_HOME_DIRECTORY(?::[^=]+)?=' } | Select-Object -First 1
    if (-not $line) { throw "CMakeCache.txt lacks CMAKE_HOME_DIRECTORY: $cache" }
    $value = ($line -split '=', 2)[1].Trim().Trim('"')
    if (-not [IO.Path]::IsPathRooted($value)) { throw "CMAKE_HOME_DIRECTORY is not absolute: $value" }
    return (Get-CanonicalPath $value)
}

function Assert-CMakeSource([string]$Build, [string]$Source) {
    $cacheSource = Get-CMakeHomeDirectory $Build
    $canonicalSource = Get-CanonicalPath $Source
    if (-not [String]::Equals($cacheSource, $canonicalSource, [StringComparison]::OrdinalIgnoreCase)) {
        throw "CMakeCache source $cacheSource does not match SourceDir $canonicalSource"
    }
    return $cacheSource
}

function Assert-SourceUnchanged([string]$ExpectedSha, [string]$Checkpoint) {
    $actualSha = Invoke-Git @('-C', $SourceDir, 'rev-parse', '--verify', 'HEAD')
    $sourceStatus = Invoke-Git @('-C', $SourceDir, 'status', '--porcelain')
    if ($sourceStatus) {
        throw "Source checkout changed during $Checkpoint: $SourceDir ($sourceStatus)"
    }
    if ($actualSha -ne $ExpectedSha) {
        throw "Source HEAD changed during $Checkpoint: expected $ExpectedSha, got $actualSha"
    }
    return $actualSha
}

$receiptObject.host.architecture = Get-HostArchitecture
$receiptObject.toolchain.host_architecture = if ($ToolHostArchitecture -eq 'auto') { if ($receiptObject.host.architecture -match '^ARM64$') { 'arm64' } else { 'x64' } } else { $ToolHostArchitecture }
$receiptObject.toolchain.target_architecture = if ($TargetArchitecture -eq 'x64') { 'x64' } else { 'arm64' }
$receiptObject.toolchain.vcvars_arguments = Get-VsInvocation $receiptObject.toolchain.host_architecture $receiptObject.toolchain.target_architecture
$policyByTarget = @{ arm64 = 'arm64-win'; arm64ec = 'arm64ec-win'; x64 = 'x86_64-win' }
$receiptObject.target.package_policy = $policyByTarget[$TargetArchitecture]
$receiptObject.target.arm64x_section_required = ($TargetArchitecture -eq 'arm64ec')

try {
    $phase = 'provenance'
    $packageValidation = Join-Path $PSScriptRoot 'package-validation.ps1'
    foreach ($path in @($SourceDir, $BuildDir, $VsVars, $GovernedBuild, $Bash, $packageValidation)) {
        if (-not (Test-Path -LiteralPath $path)) { throw "Required path is missing: $path" }
    }
    . $packageValidation
    foreach ($pathInfo in @(@{ value = $VsVars; label = 'VsVars' }, @{ value = $GovernedBuild; label = 'GovernedBuild' }, @{ value = $Bash; label = 'Bash' }, @{ value = $BuildDir; label = 'BuildDir' })) {
        Assert-SafeCommandArgument $pathInfo.value $pathInfo.label
    }
    Assert-SafeCommandArgument $TestCase 'TestCase'
    if ($TestExecutable) { Assert-SafeCommandArgument $TestExecutable 'TestExecutable' }
    $receiptObject.toolchain.governed_build_sha256 = (Get-FileHash $GovernedBuild -Algorithm SHA256).Hash
    $sourceSha = Invoke-Git @('-C', $SourceDir, 'rev-parse', '--verify', 'HEAD')
    $sourceStatus = Invoke-Git @('-C', $SourceDir, 'status', '--porcelain')
    if ($sourceStatus) { throw "Source checkout is not clean: $SourceDir" }
    $receiptObject.source.git_sha = $sourceSha
    $receiptObject.source.clean = $true
    $receiptObject.source.cmake_home_directory = Assert-CMakeSource $BuildDir $SourceDir

    $phase = 'build'
    $buildArguments = @('cmake', '--build', $BuildDir, '--target', 'Spectr_Standalone', 'Spectr_VST3', 'Spectr_CLAP', 'Spectr-test')
    $quotedBuildArguments = @('cmake', '--build', (Quote-CmdArgument $BuildDir 'BuildDir'), '--target', 'Spectr_Standalone', 'Spectr_VST3', 'Spectr_CLAP', 'Spectr-test')
    $buildLine = ((Quote-CmdArgument $Bash 'Bash'), (Quote-CmdArgument $GovernedBuild 'GovernedBuild') + $quotedBuildArguments) -join ' '
    $receiptObject.build.arguments = $buildArguments
    $receiptObject.build.command_line = $buildLine
    @('@echo off', "call `"$VsVars`" $($receiptObject.toolchain.vcvars_arguments)", 'if errorlevel 1 exit /b %ERRORLEVEL%', 'echo SPECTR_TOOL_HOST_ARCH=%VSCMD_ARG_HOST_ARCH%', 'echo SPECTR_TOOL_TARGET_ARCH=%VSCMD_ARG_TGT_ARCH%', $buildLine, 'exit /b %ERRORLEVEL%') |
        Set-Content -LiteralPath $buildCmd -Encoding ascii
    & cmd.exe /d /c "`"$buildCmd`" >`"$buildLog`" 2>&1"
    $buildExit = $LASTEXITCODE
    $receiptObject.build.exit = $buildExit
    if ($buildExit -ne 0) { throw "governed Windows build failed with exit $buildExit; see $buildLog" }
    $toolHostMatch = [regex]::Match((Get-Content -LiteralPath $buildLog -Raw), '(?m)^SPECTR_TOOL_HOST_ARCH=([^\r\n]*)')
    $toolTargetMatch = [regex]::Match((Get-Content -LiteralPath $buildLog -Raw), '(?m)^SPECTR_TOOL_TARGET_ARCH=([^\r\n]*)')
    if (-not $toolHostMatch.Success -or -not $toolTargetMatch.Success) { throw "Build log lacks Visual Studio host/target selection evidence; see $buildLog" }
    if ($toolHostMatch.Groups[1].Value.Trim().ToLowerInvariant() -ne $receiptObject.toolchain.host_architecture -or $toolTargetMatch.Groups[1].Value.Trim().ToLowerInvariant() -ne $receiptObject.toolchain.target_architecture) {
        throw "Visual Studio selected host/target $($toolHostMatch.Groups[1].Value.Trim())/$($toolTargetMatch.Groups[1].Value.Trim()), expected $($receiptObject.toolchain.host_architecture)/$($receiptObject.toolchain.target_architecture)"
    }
    $phase = 'source-revalidation-after-build'
    $receiptObject.source.git_sha_after_build = Assert-SourceUnchanged $sourceSha 'build'
    $receiptObject.source.clean_after_build = $true
    $receiptObject.source.cmake_home_directory = Assert-CMakeSource $BuildDir $SourceDir

    $phase = 'focused-test'
    $testExe = if ($TestExecutable) { $TestExecutable } else { Join-Path $BuildDir 'Spectr-test.exe' }
    if (-not (Test-Path -LiteralPath $testExe -PathType Leaf)) { throw "Missing test executable: $testExe" }
    $testItem = Get-Item -LiteralPath $testExe
    $receiptObject.focused_test.executable = $testItem.FullName
    $receiptObject.focused_test.sha256 = (Get-FileHash -LiteralPath $testItem.FullName -Algorithm SHA256).Hash
    $testLine = ((Quote-CmdArgument $testItem.FullName 'TestExecutable'), (Quote-CmdArgument $TestCase 'TestCase'), '--reporter', 'compact') -join ' '
    $receiptObject.focused_test.command_line = $testLine
    @('@echo off', "call `"$VsVars`" $($receiptObject.toolchain.vcvars_arguments)", 'if errorlevel 1 exit /b %ERRORLEVEL%', $testLine, 'exit /b %ERRORLEVEL%') |
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
        Assert-SpectrPeArchitecture -Path $file.FullName -Architecture $receiptObject.target.package_policy
        $machine = Get-SpectrPeMachine -Path $file.FullName
        $arm64x = Test-SpectrArm64XPe -Path $file.FullName
        $receiptObject.target.pe_machines += ('0x{0:X4}' -f $machine)
        $receiptObject.artifacts[$item.name] = [ordered]@{ path = $file.FullName; bytes = $file.Length; sha256 = (Get-FileHash $file.FullName -Algorithm SHA256).Hash; pe_machine = ('0x{0:X4}' -f $machine); arm64x_section = $arm64x }
    }

    $phase = 'source-revalidation-final'
    $receiptObject.source.git_sha_final = Assert-SourceUnchanged $sourceSha 'finalization'
    $receiptObject.source.clean_final = $true
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
