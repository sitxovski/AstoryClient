@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM Живая PIE-проверка МЕНЮ ЗВАНИЙ (окно Title_Win 18700 + значок Title_Notice_BG 50000).
REM Отличия от run_headless.bat: БЕЗ -NullRHI (нужны кадры и живой Slate) и -ExecCmds="py ..."
REM (нужен ТИК: подсистема наполняет список, строит полосу прокрутки и считает клик-зоны в рантайме).
setlocal
call "%~dp0asda_ue_engine.bat"
set ENGINE=%ASDA_UE_EDITOR_CMD%
set PROJECT=@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject
set SCRIPT=@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_title_live_check.py
set LOG=@@ASTORY_DEV_ROOT@@\Reports\headless_asda_title_live_check.log
"%ENGINE%" "%PROJECT%" -ExecCmds="py %SCRIPT%" -unattended -nopause -nosplash -nosound -AsdaNoNet -RenderOffscreen -ResX=1600 -ResY=1000 -WinX=0 -WinY=0 -stdout -FullStdOutLogOutput -abslog="%LOG%"
