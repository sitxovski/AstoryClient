@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM САМОТЕСТ ЗНАЧКА HUD 50000 — ПУТЁМ ИГРОКА (-game), вьюпорт настоящий 1600x1000.
REM   PIE не годится: значок сидит на Y=381 базовых 768, а PIE-вьюпорт headless-редактора 700x201,
REM   узел ниже кромки не перерисовывается и GetCachedGeometry честно отдаёт нули.
REM   Шаги разнесены таймерами внутри самой команды (-ExecCmds выполняет всё на нулевом кадре).
setlocal
call "%~dp0asda_ue_engine.bat"
set ENGINE=%ASDA_UE_EDITOR_CMD%
set PROJECT=@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject
set LOG=@@ASTORY_DEV_ROOT@@\Reports\headless_asda_title_notice.log
"%ENGINE%" "%PROJECT%" /Game/Map/Login -game -AsdaNoNet -RenderOffscreen -unattended -nopause -nosplash -nosound -ResX=1600 -ResY=1000 -stdout -FullStdOutLogOutput -abslog="%LOG%" -ExecCmds="asda.Title.NoticeTest quit"
