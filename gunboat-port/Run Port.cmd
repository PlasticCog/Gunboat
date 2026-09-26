@echo off
setlocal
rem The faithful port: the title sequence and the main menu so far; what is not ported yet stops
rem with a message. The Codex prototype is Run Gunboat.cmd.
if not exist "%~dp0build\gunboat.exe" (
  echo Build the port first with Build.ps1.
  pause
  exit /b 1
)
"%~dp0build\gunboat.exe" --game-dir "%~dp0..\Original DOS version" %*
