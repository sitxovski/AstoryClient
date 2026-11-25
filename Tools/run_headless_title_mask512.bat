@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM Живая PIE-проверка РАСШИРЕНИЯ МАСКИ СЛАВЫ ДО 512 БИТ (окно Title_Win 18700).
REM Как и run_headless_title.bat: БЕЗ -NullRHI (нужны кадры и живой Slate), окно offscreen.
setlocal
call "%~dp0asda_ue_engine.bat"
set ENGINE=%ASDA_UE_EDITOR_CMD%
set PROJECT=@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject
set SCRIPT=@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_title_mask512_check.py
set LOG=@@ASTORY_DEV_ROOT@@\Reports\headless_asda_title_mask512.log
"%ENGINE%" "%PROJECT%" -ExecCmds="py %SCRIPT%" -unattended -nopause -nosplash -nosound -AsdaNoNet -RenderOffscreen -ResX=1600 -ResY=1000 -WinX=0 -WinY=0 -stdout -FullStdOutLogOutput -abslog="%LOG%"
