@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM ★ КАДР ТУЛТИПА ПРЕДМЕТА — проверка РАНТАЙМ-пути тега |FB (заголовок 5001, флаг 926 = 0x200000).
REM   После правки 24.09 заголовок обязан быть ПСЕВДО-жирным (второй проход глифа), а не Bold-гарнитурой.
setlocal
call "%~dp0asda_ue_engine.bat"
set ENGINE=%ASDA_UE_EDITOR_CMD%
set PROJECT=@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject
set TAG=%~1
if "%TAG%"=="" set TAG=tip
set LOG=@@ASTORY_DEV_ROOT@@\RE\reports\font_regular_2026-09-24\game4_%TAG%.log
set "SEQ=asda.UI.Seq wait 8|asda.ItemTip.Code 20551|wait 2|shot %TAG%_itemtip|wait 1|quit"
"%ENGINE%" "%PROJECT%" "/Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode" -game -AsdaNoNet -RenderOffscreen -unattended -nopause -nosplash -nosound -ResX=1280 -ResY=800 -stdout -FullStdOutLogOutput -abslog="%LOG%" -ExecCmds="%SEQ%"
