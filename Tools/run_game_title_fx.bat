@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM ЖИВАЯ ПРОВЕРКА ПУТЁМ ИГРОКА (-game) ДВУХ ЭФФЕКТОВ ЗВАНИЙ + БАННЕРА «ЗВАНИЕ ПОЛУЧЕНО».
REM   1) asda.Fx.MotionProbe — доказывает, что ОБА собранных BP реально строят геометрию
REM      (эмиттеры, точка эмиссии, радиус), а не пустые заготовки;
REM   2) asda.Title.Sim secret/grant — гонит ШТАТНЫЕ обработчики 6085/6065 (закон
REM      diagnostic-must-use-real-path): эффект → значок HUD → баннер.
REM ⛔ Окна редактора не показываем: -RenderOffscreen + запуск через Start-Process -WindowStyle Hidden.
setlocal
call "%~dp0asda_ue_engine.bat"
set ENGINE=%ASDA_UE_EDITOR_CMD%
set PROJECT=@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject
set LOG=@@ASTORY_DEV_ROOT@@\Reports\headless_asda_title_fx_game.log
"%ENGINE%" "%PROJECT%" /Game/Map/Login -game -AsdaNoNet -RenderOffscreen -unattended -nopause -nosplash -nosound -ResX=1600 -ResY=1000 -stdout -FullStdOutLogOutput -abslog="%LOG%" -ExecCmds="asda.Fx.MotionProbe ex_mark_003 2.5 1 1, asda.Fx.MotionProbe 0018 5 1 1, asda.Title.State, asda.Title.Sim secret 65, asda.Title.Sim grant 66, asda.Title.State, quit"
