@echo off
setlocal EnableExtensions

rem Spectr can run its GUI without opening a platform audio device. This is
rem useful on Windows ARM VMs and RDP sessions with no default WASAPI output;
rem REAPER still owns audio when the VST3 is hosted there.
set "PULP_AUDIO_DEVICE=null"
start "Spectr" "%~dp0Spectr.exe"
exit /b 0
