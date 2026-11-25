@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM Headless-запуск UE-Python С ЖИВЫМ RHI (нужен для скриншотов). Аргумент 1 — путь к .py (абсолютный или от Tools),
REM   аргумент 2 (необязателен) — маркер успеха скрипта; дальше — доп. ключи run_headless_safe.py (напр. --print-cmd).
REM ★ С6 (11.10.2026, ревизия wave0_REVIEW.md): прежде UnrealEditor-Cmd с -ExecutePythonScript БЕЗ -NullRHI/-RenderOffscreen
REM   поднимал ОКНО редактора (закон no-visible-editor-windows). Теперь — run_headless_safe.py: --exec-cmds --render-offscreen
REM   (живой RHI без окна) + --quit-after (`py <скрипт>,quit` — синхронный скрипт отработал → выход, как у -ExecutePythonScript).
REM   Успех ПО ЛОГУ: код 0 успех / 3 провал / 4 таймаут и <лог>.result.json. Маркер не задан — успех по чистому выходу UE.
setlocal
for /f "tokens=2 delims=:" %%C in ('chcp') do set "ASDA_CP_WAS=%%C"
chcp 65001 >nul
set "SCRIPT=%~1"
if not exist "%SCRIPT%" if exist "%~dp0%SCRIPT%" set "SCRIPT=%~dp0%SCRIPT%"
for %%F in ("%SCRIPT%") do set NAME=%%~nF
set LOG=@@ASTORY_DEV_ROOT@@\Reports\headless_%NAME%.log
python "%~dp0run_headless_safe.py" "%SCRIPT%" --log "%LOG%" --exec-cmds --render-offscreen --quit-after --marker "%~2" %3 %4 %5 %6 %7 %8 %9
set RC=%ERRORLEVEL%
chcp %ASDA_CP_WAS% >nul
endlocal & exit /b %RC%
