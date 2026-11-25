@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM Сборка ДВУХ эффектов ЗВАНИЙ (ex_mark_003 = «найдено», 0018 = «получено») каналом
REM эффектов на персонажах. ASDA_CHARFX_ONLY — точечная сборка: крылья/соулмейт/хранитель
REM и прочие 90+ эффектов канала НЕ пересобираются (закон «подтверждённые не трогать»).
setlocal
call "%~dp0asda_ue_engine.bat"
set ENGINE=%ASDA_UE_EDITOR_CMD%
set PROJECT=@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject
set SCRIPT=@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_build_char_fx.py
set LOG=@@ASTORY_DEV_ROOT@@\Reports\headless_asda_title_fx_build.log
set ASDA_CHARFX_ONLY=ex_mark_003;0018
"%ENGINE%" "%PROJECT%" -ExecutePythonScript="%SCRIPT%" -unattended -nopause -nosplash -nosound -AsdaNoNet -RenderOffscreen -stdout -FullStdOutLogOutput -abslog="%LOG%"
