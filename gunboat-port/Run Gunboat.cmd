@echo off
setlocal
if not exist "%~dp0build\gunboat.exe" (
  echo Build the port first with Build.ps1.
  pause
  exit /b 1
)
"%~dp0build\gunboat.exe" --game-dir "%~dp0..\Original DOS version" %*
if errorlevel 1 (
  echo.
  echo Gunboat could not start. See the error above.
  pause
  exit /b 1
)
