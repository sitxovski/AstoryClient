@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM ★ КАДР ОКНА «ДОМ ДЛЯ ПИТОМЦА» 43000 ПУТЁМ ИГРОКА — проверка снятия временной ветки переноса
REM   (AsdaUiHelpers::ApplyJuiTextBounds). Метки «Уровень роста»/«Материал 1»/«Материал 2» обязаны
REM   читаться ЦЕЛИКОМ: именно ради них ветка держалась, и на Regular они влезают в слот.
setlocal
call "%~dp0asda_ue_engine.bat"
set ENGINE=%ASDA_UE_EDITOR_CMD%
set PROJECT=@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject
set TAG=%~1
if "%TAG%"=="" set TAG=petcage
set LOG=@@ASTORY_DEV_ROOT@@\RE\reports\font_regular_2026-09-24\game3_%TAG%.log
set "SEQ=asda.UI.Seq wait 8|asda.Pet.Window|wait 2|shot %TAG%_petcage|asda.UI.DumpTextMetrics|wait 1|quit"
"%ENGINE%" "%PROJECT%" "/Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode" -game -AsdaNoNet -RenderOffscreen -unattended -nopause -nosplash -nosound -ResX=1280 -ResY=800 -stdout -FullStdOutLogOutput -abslog="%LOG%" -ExecCmds="%SEQ%"
echo === Готово. Лог: %LOG% ===
