@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM Живая PIE-проверка ОКНА «ПОИСК ОТРЯДА» (Find_Party_WIn 8500) и пункта доп-меню (команда 7).
REM Отличия от run_headless.bat: БЕЗ -NullRHI (нужны кадры и живой Slate) и через -ExecCmds="py ..."
REM (нужен ТИК: подсистема строит окно, наполняет комбобоксы и двигает рамки в рантайме).
setlocal
call "%~dp0asda_ue_engine.bat"
set ENGINE=%ASDA_UE_EDITOR_CMD%
set PROJECT=@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject
set SCRIPT=@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_partyfind_live_check.py
set LOG=@@ASTORY_DEV_ROOT@@\Reports\headless_asda_partyfind_live_check.log
"%ENGINE%" "%PROJECT%" -ExecCmds="py %SCRIPT%" -unattended -nopause -nosplash -nosound -AsdaNoNet -RenderOffscreen -ResX=1600 -ResY=1000 -WinX=0 -WinY=0 -stdout -FullStdOutLogOutput -abslog="%LOG%"
