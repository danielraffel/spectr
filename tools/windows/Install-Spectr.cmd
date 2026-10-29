@echo off
setlocal EnableExtensions

rem Double-click entry point for the Spectr Windows test package.
rem The PowerShell file is intentionally parameterized and is not a
rem double-click installer by itself.
set "HERE=%~dp0"
set "PACKAGE="

rem Prefer the newest package ZIP beside this extracted handoff directory.
rem This keeps a newly copied test package ahead of older packages in Downloads.
for /f "usebackq delims=" %%Z in (`powershell.exe -NoProfile -Command "$p=Get-ChildItem -LiteralPath '%HERE%' -Filter 'Spectr-windows-*.zip' -File | Sort-Object LastWriteTime -Descending | Select-Object -First 1; if($p){$p.FullName}"`) do if not defined PACKAGE set "PACKAGE=%%Z"
if not defined PACKAGE for /f "usebackq delims=" %%Z in (`powershell.exe -NoProfile -Command "$p=Get-ChildItem -LiteralPath '%HERE%..' -Filter 'Spectr-windows-*.zip' -File | Sort-Object LastWriteTime -Descending | Select-Object -First 1; if($p){$p.FullName}"`) do if not defined PACKAGE set "PACKAGE=%%Z"

if not defined PACKAGE (
  echo No Spectr ZIP was found beside this launcher.
  echo Keep the original Spectr-windows-*.zip in the same folder and run this file again.
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
