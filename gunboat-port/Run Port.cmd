@echo off
setlocal
rem The faithful port of Gunboat (VGA). The launcher shows first (the game folder, the enhancements, the
rem display and the sound; gunboat.exe --help lists the options). The game folder next to gunboat-port
rem is used when it is there.
if not exist "%~dp0build\gunboat.exe" (
  echo Build the port first with Build.ps1.
  pause
  exit /b 1
)
if exist "%~dp0..\Original DOS version\GB.EXE" (
  "%~dp0build\gunboat.exe" --game-dir "%~dp0..\Original DOS version" %*
) else (
  "%~dp0build\gunboat.exe" %*
)
