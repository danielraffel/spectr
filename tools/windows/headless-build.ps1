[CmdletBinding()]
param(
    [string]$SourceDir = 'C:\spectr-gui',
    [string]$BuildDir = 'C:\builds\spectr-arm64-gpu',
    [string]$VsVars = 'C:\VS18BuildTools\VC\Auxiliary\Build\vcvarsall.bat',
    [string]$Receipt = 'C:\builds\spectr-arm64-gpu\headless-build-receipt.json'
)

$ErrorActionPreference = 'Stop'
foreach ($path in @($SourceDir, $BuildDir, $VsVars)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Required path is missing: $path" }
}

$work = Join-Path $env:TEMP ("spectr-headless-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $work | Out-Null
try {
    $buildCmd = Join-Path $work 'build.cmd'
    @("@echo off", "call `"$VsVars`" arm64 >nul", "cmake --build `"$BuildDir`" --target Spectr_Standalone Spectr_VST3 Spectr_CLAP Spectr-test --parallel 4", 'exit /b %ERRORLEVEL%') |
        Set-Content -LiteralPath $buildCmd -Encoding ascii
    $buildLog = Join-Path $work 'build.log'
    & cmd.exe /d /c "`"$buildCmd`" >`"$buildLog`" 2>&1"
    $buildExit = $LASTEXITCODE
    if ($buildExit -ne 0) { throw "headless Windows build failed with exit $buildExit; see $buildLog" }

    $testCmd = Join-Path $work 'test.cmd'
    @("@echo off", "call `"$VsVars`" arm64 >nul", "`"$BuildDir\Spectr-test.exe`" `"Spectr processes audio`" --reporter compact", 'exit /b %ERRORLEVEL%') |
        Set-Content -LiteralPath $testCmd -Encoding ascii
    $testLog = Join-Path $work 'test.log'
    & cmd.exe /d /c "`"$testCmd`" >`"$testLog`" 2>&1"
    $testExit = $LASTEXITCODE
    if ($testExit -ne 0) { throw "focused Windows audio test failed with exit $testExit; see $testLog" }

    $artifacts = [ordered]@{}
    foreach ($item in @(
        @{ name = 'standalone'; path = (Join-Path $BuildDir 'Spectr.exe') },
        @{ name = 'vst3'; path = (Join-Path $BuildDir 'VST3\Spectr.dll') },
        @{ name = 'clap'; path = (Join-Path $BuildDir 'CLAP\Spectr.clap') }
    )) {
        if (-not (Test-Path -LiteralPath $item.path -PathType Leaf)) { throw "Missing artifact: $($item.path)" }
        $file = Get-Item -LiteralPath $item.path
        $artifacts[$item.name] = [ordered]@{ path = $file.FullName; bytes = $file.Length; sha256 = (Get-FileHash $file.FullName -Algorithm SHA256).Hash }
    }

    $gitSha = $null
    if (Get-Command git -ErrorAction SilentlyContinue) { $gitSha = (git -C $SourceDir rev-parse HEAD 2>$null) }
    $receiptObject = [ordered]@{
        schema = 1
        generated_at_utc = (Get-Date).ToUniversalTime().ToString('o')
        source_dir = $SourceDir
        source_git_sha = $gitSha
        architecture = $env:PROCESSOR_ARCHITECTURE
        toolchain = $VsVars
        build = [ordered]@{ exit = $buildExit; targets = @('Spectr_Standalone', 'Spectr_VST3', 'Spectr_CLAP', 'Spectr-test') }
        focused_test = [ordered]@{ name = 'Spectr processes audio'; exit = $testExit; assertions = 2; cases = 1 }
        artifacts = $artifacts
        limitations = @('Headless build/test proves compilation and focused DSP behavior only.', 'It does not claim DAW loading, GUI, screenshot, or Ableton behavior.')
    }
    $receiptObject | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $Receipt -Encoding utf8
    Write-Output "PASS: $Receipt"
    Get-Content -LiteralPath $testLog
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
