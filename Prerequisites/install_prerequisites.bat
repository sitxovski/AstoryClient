@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
rem Astory Client - install prerequisites (see README.md next to this file). Argument: -Yes
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install_prerequisites.ps1" %*
echo.
pause
