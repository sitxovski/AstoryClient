@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM Headless-импорт скиннутых моделей: без редактора-UI, без модальных диалогов (-unattended),
REM без GPU (-NullRHI) — не виснет и безопасно по RDP (нет TDR). Лог пишется в файл, ошибки читаемы.
setlocal
call "%~dp0asda_ue_engine.bat"
set ENGINE=%ASDA_UE_EDITOR_CMD%
set PROJECT=@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject
set SCRIPT=@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_import_skinned_models.py
set LOG=@@ASTORY_DEV_ROOT@@\Reports\import_skinned.log

echo === Headless import. Лог: %LOG% ===
"%ENGINE%" "%PROJECT%" -ExecutePythonScript="%SCRIPT%" -unattended -nopause -nosplash -nosound -NullRHI -stdout -FullStdOutLogOutput -abslog="%LOG%"
echo.
echo === Готово. Код выхода: %ERRORLEVEL%. Полный лог: %LOG% ===
rem ПАУЗА УБРАНА 2026-08-26: при фоновом/автоматическом запуске скрипт висел
rem на ней вечно и лога не оставлял — задача выглядела зависшей.
rem Код выхода и путь к логу напечатаны выше, для ручного запуска этого хватает.
