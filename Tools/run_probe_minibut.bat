@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
setlocal
call "%~dp0asda_ue_engine.bat"
"%ASDA_UE_EDITOR_CMD%" "@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject" -ExecutePythonScript="@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\_probe_minibut.py" -unattended -nopause -nosplash -nosound -NullRHI -stdout -abslog="@@ASTORY_DEV_ROOT@@\Reports\_party_minibut.log"
