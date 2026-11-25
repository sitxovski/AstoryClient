@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM Headless-запуск UE-Python С ТИКОМ РЕДАКТОРА (для скриншотов). ★ -ExecCmds="py ...", а НЕ
REM -ExecutePythonScript: второй завершает редактор сразу после скрипта, и машина состояний на
REM register_slate_post_tick_callback не успевает сделать НИ ОДНОГО тика.
setlocal
REM ★ Т1 (11.10.2026, волна В0 dedup_audit2): запуск через run_headless_safe.py — редактор БЕЗ ОКНА (-RenderOffscreen),
REM   -SCCProvider=None, -AsdaNoNet (SELFTESTS_OFFLINE.md); успех ПО ЛОГУ: код 0 успех / 3 провал / 4 таймаут и
REM   <лог>.result.json (код выхода UE недостоверен). Доп. ключи обёртки — аргументами .bat, напр. --print-cmd (сухо).
REM   chcp 65001 — чтобы маркер с кириллицей дошёл до обёртки целым; прежняя кодовая страница возвращается в конце.
for /f "tokens=2 delims=:" %%C in ('chcp') do set "ASDA_CP_WAS=%%C"
chcp 65001 >nul
REM ★ С5 (11.10.2026, ревизия wave0_REVIEW.md): скрипт на тике сам редактор не закрывает — без маркера вердикт был бы
REM   «УСПЕХ» по любому выходу (в т.ч. по исключению). Маркер (аргумент 2) ОБЯЗАТЕЛЕН; путь скрипта — абсолютный или от Tools;
REM   аргументы с 3-го — доп. ключи run_headless_safe.py.
set "SCRIPT=%~1"
if not exist "%SCRIPT%" if exist "%~dp0%SCRIPT%" set "SCRIPT=%~dp0%SCRIPT%"
if "%~2"=="" (
  echo run_headless_rhi_tick: нужен маркер успеха вторым аргументом ^(строка, которую скрипт печатает ТОЛЬКО на успехе^)
  chcp %ASDA_CP_WAS% >nul
  endlocal & exit /b 2
)
for %%F in ("%SCRIPT%") do set NAME=%%~nF
set LOG=@@ASTORY_DEV_ROOT@@\Reports\headless_%NAME%.log
python "%~dp0run_headless_safe.py" "%SCRIPT%" --log "%LOG%" --exec-cmds --render-offscreen --marker "%~2" %3 %4 %5 %6 %7 %8 %9
set RC=%ERRORLEVEL%
chcp %ASDA_CP_WAS% >nul
endlocal & exit /b %RC%
