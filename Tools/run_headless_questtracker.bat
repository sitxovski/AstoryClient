@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM Живая PIE-проверка ЭКРАННОГО ТРЕКЕРА ЗАДАНИЙ (JUI 1800). Большой вьюпорт: окно трекера прижато
REM к ПРАВОМУ краю (Align "2 -222 8"), и в стандартном headless-окне оно уезжает за край кадра —
REM получилось бы ложное «окна нет».
setlocal
REM ★ Т1 (11.10.2026, волна В0 dedup_audit2): запуск через run_headless_safe.py — редактор БЕЗ ОКНА (-RenderOffscreen),
REM   -SCCProvider=None, -AsdaNoNet (SELFTESTS_OFFLINE.md); успех ПО ЛОГУ: код 0 успех / 3 провал / 4 таймаут и
REM   <лог>.result.json (код выхода UE недостоверен). Доп. ключи обёртки — аргументами .bat, напр. --print-cmd (сухо).
REM   chcp 65001 — чтобы маркер с кириллицей дошёл до обёртки целым; прежняя кодовая страница возвращается в конце.
for /f "tokens=2 delims=:" %%C in ('chcp') do set "ASDA_CP_WAS=%%C"
chcp 65001 >nul
set SCRIPT=@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_questtracker_live_check.py
set LOG=@@ASTORY_DEV_ROOT@@\Reports\headless_asda_questtracker_live_check.log
REM ПРЕДЗАПУСК: положить в GameUserSettings.ini сохранённую позицию окна 1800 (имитация
REM "в прошлой сессии игрок перетащил окно"). Делается ДО старта редактора: GConfig читает
REM ini один раз при запуске и переписывает секцию при выходе.
python "%SCRIPT%"
python "%~dp0run_headless_safe.py" "%SCRIPT%" --log "%LOG%" --exec-cmds --render-offscreen --res 1920x1200 --marker "[трекер-живой] === готово, снимков запрошено" %*
set RC=%ERRORLEVEL%
chcp %ASDA_CP_WAS% >nul
endlocal & exit /b %RC%
