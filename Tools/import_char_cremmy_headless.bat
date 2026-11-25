@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM Headless import of char-select preview character (SkeletalMesh cremmy + idle anim).
REM RDP-safe: -unattended -NullRHI (no GPU), log to file.
setlocal
call "%~dp0asda_ue_engine.bat"
set ENGINE=%ASDA_UE_EDITOR_CMD%
set PROJECT=@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject
set SCRIPT=@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\import_char_cremmy.py
set LOG=@@ASTORY_DEV_ROOT@@\Reports\import_char_cremmy.log

echo === Headless import char cremmy. Log: %LOG% ===
"%ENGINE%" "%PROJECT%" -ExecutePythonScript="%SCRIPT%" -unattended -nopause -nosplash -nosound -NullRHI -stdout -FullStdOutLogOutput -abslog="%LOG%"
echo.
echo === Done. Exit code: %ERRORLEVEL%. Full log: %LOG% ===
