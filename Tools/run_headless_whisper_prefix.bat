@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM ЖИВАЯ (PIE) проверка видимой обратной связи при выборе шёпота (жалоба владельца 30.08.2026).
REM   БЕЗ -NullRHI: нужен живой Slate и КАДР. -RenderOffscreen: окон на экране не показывается.
setlocal
call "%~dp0asda_ue_engine.bat"
set ENGINE=%ASDA_UE_EDITOR_CMD%
set PROJECT=@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject
set SCRIPT=@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_whisper_prefix_live_check.py
set LOG=@@ASTORY_DEV_ROOT@@\Reports\headless_asda_whisper_prefix.log
"%ENGINE%" "%PROJECT%" -ExecCmds="py %SCRIPT%" -unattended -nopause -nosplash -nosound -AsdaNoNet -RenderOffscreen -ResX=1600 -ResY=1000 -WinX=0 -WinY=0 -stdout -FullStdOutLogOutput -abslog="%LOG%"
