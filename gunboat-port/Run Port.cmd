@echo off
setlocal
rem The faithful port of Gunboat (VGA). The launcher shows first (the game folder, the enhancements, the
rem display and the sound; gunboat.exe --help lists the options). The game files go in the folder Game
rem at the top of the repository (Game\README.md), which gunboat.exe finds by itself.
if not exist "%~dp0build\gunboat.exe" (
  echo Build the port first with Build.ps1.
  pause
  exit /b 1
)
"%~dp0build\gunboat.exe" %*
