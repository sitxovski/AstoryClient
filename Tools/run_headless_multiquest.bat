@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM Живая PIE-проверка облачка выбора задания (JUI 2406 MULTYQUEST_WIN).
REM ★ ЗАПУСКАЕТСЯ ДВАЖДЫ, С РАЗНЫМИ РАЗРЕШЕНИЯМИ: требование владельца «проверить центр на разных
REM   разрешениях». Дамп печатает расхождение центра окна и центра вьюпорта — оно обязано быть
REM   около нуля в обоих прогонах.
setlocal
REM ★ Т1 (11.10.2026, волна В0 dedup_audit2): запуск через run_headless_safe.py — редактор БЕЗ ОКНА (-RenderOffscreen),
REM   -SCCProvider=None, -AsdaNoNet (SELFTESTS_OFFLINE.md); успех ПО ЛОГУ: код 0 успех / 3 провал / 4 таймаут и
REM   <лог>.result.json (код выхода UE недостоверен). Доп. ключи обёртки — аргументами .bat, напр. --print-cmd (сухо).
REM   chcp 65001 — чтобы маркер с кириллицей дошёл до обёртки целым; прежняя кодовая страница возвращается в конце.
for /f "tokens=2 delims=:" %%C in ('chcp') do set "ASDA_CP_WAS=%%C"
chcp 65001 >nul
set SCRIPT=@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_multiquest_live_check.py

echo === Прогон 1: 1920x1200 ===
set ASDA_MQ_OUT=@@ASTORY_DEV_ROOT_FWD@@/Reports/multiquest_live_1920
python "%~dp0run_headless_safe.py" "%SCRIPT%" --log "@@ASTORY_DEV_ROOT@@\Reports\headless_asda_multiquest_live_1920.log" --exec-cmds --render-offscreen --res 1920x1200 --marker "[облачко-заданий] PIE остановлен, выходим" %*
set RC1=%ERRORLEVEL%

echo === Прогон 2: 1280x720 ===
set ASDA_MQ_OUT=@@ASTORY_DEV_ROOT_FWD@@/Reports/multiquest_live_1280
python "%~dp0run_headless_safe.py" "%SCRIPT%" --log "@@ASTORY_DEV_ROOT@@\Reports\headless_asda_multiquest_live_1280.log" --exec-cmds --render-offscreen --res 1280x720 --marker "[облачко-заданий] PIE остановлен, выходим" %*
set RC2=%ERRORLEVEL%
if not "%RC1%"=="0" (set RC=%RC1%) else set RC=%RC2%
chcp %ASDA_CP_WAS% >nul
endlocal & exit /b %RC%
