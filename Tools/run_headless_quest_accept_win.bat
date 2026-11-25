@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM Live PIE check: quest ACCEPT window (JUI 750 QUEST_Confirm_Win) that replaced the on-screen
REM notice 20197 in the 5044 handler. Needs render (no -NullRHI) and editor tick.
REM Report: Reports/quest_accept_window.md
setlocal
REM ★ Т1 (11.10.2026, волна В0 dedup_audit2): запуск через run_headless_safe.py — редактор БЕЗ ОКНА (-RenderOffscreen),
REM   -SCCProvider=None, -AsdaNoNet (SELFTESTS_OFFLINE.md); успех ПО ЛОГУ: код 0 успех / 3 провал / 4 таймаут и
REM   <лог>.result.json (код выхода UE недостоверен). Доп. ключи обёртки — аргументами .bat, напр. --print-cmd (сухо).
REM   chcp 65001 — чтобы маркер с кириллицей дошёл до обёртки целым; прежняя кодовая страница возвращается в конце.
for /f "tokens=2 delims=:" %%C in ('chcp') do set "ASDA_CP_WAS=%%C"
chcp 65001 >nul
set SCRIPT=@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_quest_accept_win_live_check.py
set LOG=@@ASTORY_DEV_ROOT@@\Reports\headless_asda_quest_accept_win_live_check.log
python "%~dp0run_headless_safe.py" "%SCRIPT%" --log "%LOG%" --exec-cmds --render-offscreen --res 1920x1200 --marker "[приём-задания-750] === готово, снимков запрошено" %*
set RC=%ERRORLEVEL%
chcp %ASDA_CP_WAS% >nul
endlocal & exit /b %RC%
