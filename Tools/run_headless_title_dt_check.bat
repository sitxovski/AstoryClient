@echo off
REM Доказательство переимпорта DT_title_list: читаем грейд/очки прямо из ассета.
setlocal
call "%~dp0asda_ue_engine.bat"
set ENGINE=%ASDA_UE_EDITOR_CMD%
set PROJECT=@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject
set SCRIPT=@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_title_dt_check.py
set LOG=@@ASTORY_DEV_ROOT@@\Reports\headless_title_dt_check.log
"%ENGINE%" "%PROJECT%" -ExecCmds="py %SCRIPT%" -unattended -nopause -nosplash -nosound -AsdaNoNet -RenderOffscreen -ResX=1280 -ResY=720 -stdout -FullStdOutLogOutput -abslog="%LOG%"
rem Copyright (c) 2026 @sitxovski (Discord)
