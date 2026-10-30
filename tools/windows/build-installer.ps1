[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$PackageRoot,
    [string]$OutputDir = '',
    [ValidateSet('x86_64-win', 'arm64-win', 'arm64ec-win')] [string]$PluginArchitecture = 'arm64ec-win',
    [string]$DisplayArchitecture = ''
)

$ErrorActionPreference = 'Stop'
$PackageRoot = (Resolve-Path -LiteralPath $PackageRoot).Path
if ([string]::IsNullOrWhiteSpace($OutputDir)) { $OutputDir = Join-Path (Split-Path $PackageRoot -Parent) 'installer-output' }
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
if ([string]::IsNullOrWhiteSpace($DisplayArchitecture)) {
    $DisplayArchitecture = switch ($PluginArchitecture) {
        'x86_64-win' { 'Intel64' }
        'arm64-win' { 'ARM64' }
        'arm64ec-win' { 'ARM64EC' }
    }
}
foreach ($required in @('Standalone\Spectr.exe', 'VST3\Spectr.vst3', 'CLAP', 'Launch-Spectr.cmd')) {
    if (-not (Test-Path -LiteralPath (Join-Path $PackageRoot $required))) { throw "Package is missing required installer input: $required" }
}
$iscc = @(
    (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
    (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe')
) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $iscc) { throw 'Inno Setup 6 ISCC.exe was not found' }
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$iss = Join-Path $PSScriptRoot 'Spectr.iss'
& $iscc "/DSourceRoot=$PackageRoot" "/DOutputDir=$OutputDir" "/DPluginArchitecture=$PluginArchitecture" "/DDisplayArchitecture=$DisplayArchitecture" $iss
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed with exit code $LASTEXITCODE" }
Get-ChildItem -LiteralPath $OutputDir -Filter "Spectr-Windows-$DisplayArchitecture-Setup.exe" -File
