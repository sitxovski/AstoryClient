@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
setlocal
call "%~dp0asda_ue_engine.bat"
set ENGINE=%ASDA_UE_EDITOR_CMD%
set PROJECT=@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject
"%ENGINE%" "%PROJECT%" -ExecutePythonScript="@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\_probe_party_real.py" -unattended -nopause -nosplash -nosound -NullRHI -stdout -FullStdOutLogOutput -abslog="@@ASTORY_DEV_ROOT@@\Reports\_party_real_probe.log"
