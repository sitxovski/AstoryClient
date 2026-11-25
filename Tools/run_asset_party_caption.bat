@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM Кегль подписей панели отряда 2200 (правка в АССЕТЕ). ASDA_PARTY_CAPTION=revert — откат.
setlocal
call "%~dp0asda_ue_engine.bat"
set ENGINE=%ASDA_UE_EDITOR_CMD%
set PROJECT=@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject
set SCRIPT=@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_party_caption_size.py
set LOG=@@ASTORY_DEV_ROOT@@\Reports\_party_caption_size.log
"%ENGINE%" "%PROJECT%" -ExecutePythonScript="%SCRIPT%" -unattended -nopause -nosplash -nosound -NullRHI -stdout -FullStdOutLogOutput -abslog="%LOG%"
