@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM ЖИВАЯ ПРОВЕРКА ПУТЁМ ИГРОКА (-game) с НАСТОЯЩИМ ПАВНОМ: эффекты званий должны СЕСТЬ НА ПЕРСОНАЖА.
REM   Asda.World.FakeSelfSpawn — тот же путь, что и пакет 4003 (офлайн-крюк, своей ветки спавна нет).
REM   Затем штатные обработчики 6085 и 6065 через asda.Title.Sim (diagnostic-must-use-real-path).
setlocal
call "%~dp0asda_ue_engine.bat"
set ENGINE=%ASDA_UE_EDITOR_CMD%
set PROJECT=@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject
set LOG=@@ASTORY_DEV_ROOT@@\Reports\headless_asda_title_fx_game2.log
"%ENGINE%" "%PROJECT%" /Game/Map/alpen/alpen -game -AsdaNoNet -RenderOffscreen -unattended -nopause -nosplash -nosound -ResX=1600 -ResY=1000 -stdout -FullStdOutLogOutput -abslog="%LOG%" -ExecCmds="Asda.World.FakeSelfSpawn 256 256 ТестЗвание, asda.Title.Sim secret 65, asda.Title.Sim grant 66, asda.Title.CharFx 0, asda.Title.Sim secret 67, quit"
