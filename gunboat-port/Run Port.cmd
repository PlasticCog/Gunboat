@echo off
setlocal
rem The faithful port of Gunboat (VGA). Options: --fps N (3D stations, default 15), --sound adlib|speaker,
rem --scale N, --fullscreen.
if not exist "%~dp0build\gunboat.exe" (
  echo Build the port first with Build.ps1.
  pause
  exit /b 1
)
"%~dp0build\gunboat.exe" --game-dir "%~dp0..\Original DOS version" %*
