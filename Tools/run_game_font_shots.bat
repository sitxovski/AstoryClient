@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM ★ КАДРЫ ПУТЁМ ИГРОКА (-game) для сверки начертания подписей JUI (правка 2026-09-24).
REM   Снимает 4 разных окна; тег кадров — первый аргумент (before / after).
REM   * -game            — путь игрока, а не PIE (закон ui-check-game-path-geometry);
REM   * -RenderOffscreen — окон Unreal на экране не появляется (закон no-visible-editor-windows);
REM   * БЕЗ -NullRHI     — иначе Slate не рисует и снимков не будет вовсе.
REM   Кадры: <проект>\Saved\Screenshots\<тег>_<окно>.png (asda.UI.Shot пишет ИМЕННО это имя).
setlocal
call "%~dp0asda_ue_engine.bat"
set ENGINE=%ASDA_UE_EDITOR_CMD%
set PROJECT=@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject
set TAG=%~1
if "%TAG%"=="" set TAG=after
set LOG=@@ASTORY_DEV_ROOT@@\RE\reports\font_regular_2026-09-24\game_%TAG%.log
set U=/Game/AsdaAssets/UI/Player_UI
set "SEQ=asda.UI.Seq wait 8"
set "SEQ=%SEQ%|asda.UI.OpenByPath NPCSHOP_WIN %U%/Trade/WBP_NPCSHOP_WIN 100|wait 1|shot %TAG%_npcshop|asda.UI.JuiCloseAll"
set "SEQ=%SEQ%|asda.UI.OpenByPath CHARAINFO_WIN %U%/Character/WBP_CHARAINFO_WIN 100|wait 1|shot %TAG%_charinfo|asda.UI.JuiCloseAll"
set "SEQ=%SEQ%|asda.UI.OpenByPath WARE_WIN %U%/Inventory/WBP_WARE_WIN 100|wait 1|shot %TAG%_ware|asda.UI.JuiCloseAll"
set "SEQ=%SEQ%|asda.UI.OpenByPath Item_Union_Win %U%/Craft/WBP_Item_Union_Win 100|wait 1|shot %TAG%_union|asda.UI.JuiCloseAll"
set "SEQ=%SEQ%|wait 1|quit"
"%ENGINE%" "%PROJECT%" "/Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode" -game -AsdaNoNet -RenderOffscreen -unattended -nopause -nosplash -nosound -ResX=1280 -ResY=800 -stdout -FullStdOutLogOutput -abslog="%LOG%" -ExecCmds="%SEQ%"
echo === Готово. Код выхода: %ERRORLEVEL% (НЕДОСТОВЕРЕН). Лог: %LOG% ===
