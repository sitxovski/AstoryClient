@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
rem Decrypt.bat — расшифровать запечатанные журналы (.alog/.acrash): перетащите на этот файл один или несколько файлов/папок.
rem Расшифровка кладётся РЯДОМ: <имя>.txt (+ папка <имя>.d с minidump у падений). Ключ — DPAPI владельца на этом ПК.
rem Логика — AstoryClient\Tools\asda_logseal.py (через logseal.py рядом), здесь только запуск.
chcp 65001 >nul
rem папка скрипта запоминается до цикла: shift сдвигает и %0
set "HERE=%~dp0"
where python >nul 2>&1 || (echo Python не найден в PATH & pause & exit /b 1)
if "%~1"=="" (echo Перетащите на Decrypt.bat файл .alog/.acrash или папку с ними & pause & exit /b 1)
set "RC=0"
:next
if "%~1"=="" goto done
python "%HERE%logseal.py" decrypt "%~1" || set "RC=1"
shift
goto next
:done
echo.
if "%RC%"=="0" (echo Готово: ошибок нет) else (echo Есть ошибки — см. выше)
pause
exit /b %RC%
