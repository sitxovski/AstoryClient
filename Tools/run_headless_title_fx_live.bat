@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM ЖИВАЯ (PIE) проверка ДВУХ ЭФФЕКТОВ ЗВАНИЙ на персонаже + баннера «звание получено».
REM   * БЕЗ -NullRHI — эффект строит геометрию тиком, Slate обязан рисовать (ui-check-game-path-geometry);
REM   * -ExecCmds="py ..." (а не -ExecutePythonScript) — нужен ТИК редактора для машины состояний;
REM   * -RenderOffscreen — окон редактора не показываем (no-visible-editor-windows).
setlocal
call "%~dp0asda_ue_engine.bat"
set ENGINE=%ASDA_UE_EDITOR_CMD%
set PROJECT=@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject
set SCRIPT=@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_title_fx_live_check.py
set LOG=@@ASTORY_DEV_ROOT@@\Reports\headless_asda_title_fx_live.log
"%ENGINE%" "%PROJECT%" -ExecCmds="py %SCRIPT%" -unattended -nopause -nosplash -nosound -AsdaNoNet -RenderOffscreen -ResX=1600 -ResY=1000 -WinX=0 -WinY=0 -stdout -FullStdOutLogOutput -abslog="%LOG%"
