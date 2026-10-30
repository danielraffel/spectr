@echo off
setlocal EnableExtensions
rem Use the null device for a standalone GUI in a VM or RDP session.  REAPER
rem owns the real audio device when it hosts the VST3 plugin.
set "PULP_AUDIO_DEVICE=null"
start "Spectr" "%~dp0Spectr.exe"
exit /b 0
