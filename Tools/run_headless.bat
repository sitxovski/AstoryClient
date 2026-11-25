@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM Универсальный headless-запуск любого UE-Python-скрипта (без редактора, без диалогов, без GPU).
REM Использование:  run_headless.bat <путь_к_скрипту.py>
REM   либо перетащи .py-файл на этот .bat.
REM Без аргумента — запускает импорт скелетных моделей.
setlocal
call "%~dp0asda_ue_engine.bat"
set ENGINE=%ASDA_UE_EDITOR_CMD%
set PROJECT=@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject
set SCRIPT=%~1
if "%SCRIPT%"=="" set SCRIPT=@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_import_skinned_models.py
for %%F in ("%SCRIPT%") do set NAME=%%~nF
set LOG=@@ASTORY_DEV_ROOT@@\Reports\headless_%NAME%.log

echo === Headless: %SCRIPT% ===
echo === Лог: %LOG% ===
"%ENGINE%" "%PROJECT%" -ExecutePythonScript="%SCRIPT%" -unattended -nopause -nosplash -nosound -NullRHI -stdout -FullStdOutLogOutput -abslog="%LOG%"
echo.
echo === Готово. Код выхода: %ERRORLEVEL% ===
echo === ВНИМАНИЕ: этот код НЕДОСТОВЕРЕН — UnrealEditor-Cmd почти всегда падает ===
echo ===   с 0xC0000005 (-1073741819) в teardown УЖЕ ПОСЛЕ 'Log file closed'.   ===
echo ===   Для автоматики/CI успех определяй ПО ЛОГУ через run_headless_safe.py ===
echo ===   (он пишет ^<log^>.result.json и возвращает осмысленный код). Лог: %LOG% ===
rem ПАУЗА УБРАНА 2026-08-26: при фоновом/автоматическом запуске скрипт висел
rem на ней вечно и лога не оставлял — задача выглядела зависшей.
rem 31.08.2026: доказано, что %ERRORLEVEL% этого .bat = код AV на выходе, а не признак
rem   успеха скрипта. Ручной прогон терпит, автоматика ДОЛЖНА идти через run_headless_safe.py.
