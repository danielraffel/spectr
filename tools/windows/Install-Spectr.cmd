@echo off
setlocal EnableExtensions

rem Double-click entry point for the extracted Spectr package.  The
rem PowerShell file stays parameterized so CI and scripted installs can pass
rem explicit roots and receipt paths.
set "HERE=%~dp0"
set "PACKAGE="
for /f "usebackq delims=" %%Z in (`powershell.exe -NoProfile -Command "$p=Get-ChildItem -LiteralPath '%HERE%' -Filter 'Spectr-windows-*.zip' -File | Sort-Object LastWriteTime -Descending | Select-Object -First 1; if($p){$p.FullName}"`) do if not defined PACKAGE set "PACKAGE=%%Z"
if not defined PACKAGE for /f "usebackq delims=" %%Z in (`powershell.exe -NoProfile -Command "$p=Get-ChildItem -LiteralPath '%HERE%..' -Filter 'Spectr-windows-*.zip' -File | Sort-Object LastWriteTime -Descending | Select-Object -First 1; if($p){$p.FullName}"`) do if not defined PACKAGE set "PACKAGE=%%Z"
if not defined PACKAGE (
  echo No Spectr-windows-*.zip was found beside this launcher.
  pause
  exit /b 2
)
echo Installing from %PACKAGE%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%HERE%Install-Spectr.ps1" -Package "%PACKAGE%"
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (
  echo.
  echo Spectr installation failed with exit code %RC%.
  pause
)
exit /b %RC%
