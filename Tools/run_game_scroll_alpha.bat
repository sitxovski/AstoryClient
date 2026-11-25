@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM ★ КАДРЫ ПОЛОС ПРОКРУТКИ `Nomal_Scr` ВО ВСЕХ ОКНАХ, ГДЕ ПОЛОСУ СТРОИТ КОД (волна 13, 25.09.2026).
REM   Зачем: сверка «до/после» правки тинта `FAsdaScrollBarNomal::Build` по данным скина (JuiSkin.ini:
REM   ES0 дорожка FFFFFFFF, ES1-ES3 стрелки/бегунок 64FFFFFF). Окна: сумка 494, книга навыков 12291,
REM   склад 4189, магазин NPC 1365, звания 18735, аукцион 13125/13249/13470, премиум-гача 68310,
REM   смена параметров 50351/50371; для сравнения — окно 78200 (планы из ассета, тинт из данных).
REM   * -game            — путь игрока, а не PIE (закон ui-check-game-path-geometry);
REM   * -RenderOffscreen — окон Unreal на экране нет (закон no-visible-editor-windows);
REM   * последовательность — общая команда `asda.UI.Seq` (AsdaUiWave1Probe.cpp), свой код не заводим.
REM   Аргумент 1 — метка кадров (before / after). Кадры: @@ASTORY_DEV_ROOT@@\Reports\gear_upgrade\scroll_alpha_shots\
setlocal
call "%~dp0asda_ue_engine.bat"
set ENGINE=%ASDA_UE_EDITOR_CMD%
set PROJECT=@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject
set TAG=%~1
if "%TAG%"=="" set TAG=shot
set D=../gear_upgrade/scroll_alpha_shots/%TAG%
set LOG=@@ASTORY_DEV_ROOT@@\Reports\gear_upgrade\scroll_alpha_shots\game_%TAG%.log
set "SEQ=asda.UI.Seq wait 8"
set "SEQ=%SEQ%|asda.Inven.Open 1|wait 2|shot %D%_1_inven|asda.Inven.Open 0|wait 0.5"
set "SEQ=%SEQ%|asda.Skill.Class 1|asda.Skill.Open|wait 2|shot %D%_2_skill|asda.Skill.Close|wait 0.5"
set "SEQ=%SEQ%|asda.Warehouse.Open 1|wait 2|shot %D%_3_warehouse|asda.Warehouse.Close|wait 0.5"
set "SEQ=%SEQ%|asda.Shop.Open 5 3|wait 2|shot %D%_4_npcshop|asda.Shop.Close|wait 0.5"
set "SEQ=%SEQ%|asda.Title.Open|wait 2|shot %D%_5_title|asda.Title.Open|wait 0.5"
set "SEQ=%SEQ%|asda.Exchange.Open 1|wait 2|shot %D%_6_exchange_m1|asda.Exchange.Open 0|wait 0.5"
set "SEQ=%SEQ%|asda.Exchange.Open 3|wait 2|shot %D%_6_exchange_m3|asda.Exchange.Open 0|wait 0.5"
set "SEQ=%SEQ%|asda.Exchange.Open 5|wait 2|shot %D%_6_exchange_m5|asda.Exchange.Open 0|wait 0.5"
set "SEQ=%SEQ%|asda.Gacha.Open|wait 3|asda.UI.SimClickWidget Premium_PickBAR_Win Premium_PickBAR_1_B1|wait 2|shot %D%_7_gacha|asda.Gacha.Close|wait 0.5"
set "SEQ=%SEQ%|asda.ItemOption.Open|wait 2|shot %D%_8_itemoption|asda.ItemOption.Close|wait 0.5"
set "SEQ=%SEQ%|asda.HighRefine.Window|wait 2|shot %D%_9_highrefine_ref|wait 1|quit"
if not exist "@@ASTORY_DEV_ROOT@@\Reports\gear_upgrade\scroll_alpha_shots" mkdir "@@ASTORY_DEV_ROOT@@\Reports\gear_upgrade\scroll_alpha_shots"
"%ENGINE%" "%PROJECT%" "/Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode" -game -AsdaNoNet -RenderOffscreen -unattended -nopause -nosplash -nosound -ResX=1280 -ResY=800 -stdout -FullStdOutLogOutput -abslog="%LOG%" -ExecCmds="%SEQ%"
echo === Готово. Код выхода: %ERRORLEVEL%. Лог: %LOG% ===
