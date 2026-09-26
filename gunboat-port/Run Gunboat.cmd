@echo off
setlocal
if not exist "%~dp0build\gunboat_legacy.exe" (
  echo Build the port first with Build.ps1. This starts the Codex prototype until the faithful port is playable.
  pause
  exit /b 1
)
"%~dp0build\gunboat_legacy.exe" --game-dir "%~dp0..\Original DOS version" %*
if errorlevel 1 (
  echo.
  echo Gunboat could not start. See the error above.
  pause
  exit /b 1
)
