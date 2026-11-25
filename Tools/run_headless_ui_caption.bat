@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM Живая PIE-проверка ПОЛНОГО ПЕРЕИМПОРТА JUI (состояния подписи кнопок во всех классах окон).
REM   Как и у проверки окна разговора: нужен ТИК редактора (-ExecCmds="py ...", а не
REM   -ExecutePythonScript) — иначе машина шагов на register_slate_post_tick_callback не сделает
REM   ни одного шага; и нужен БОЛЬШОЙ вьюпорт, иначе окна с базой 1024x768 не помещаются в кадр.
setlocal
REM ★ Т1 (11.10.2026, волна В0 dedup_audit2): запуск через run_headless_safe.py — редактор БЕЗ ОКНА (-RenderOffscreen),
REM   -SCCProvider=None, -AsdaNoNet (SELFTESTS_OFFLINE.md); успех ПО ЛОГУ: код 0 успех / 3 провал / 4 таймаут и
REM   <лог>.result.json (код выхода UE недостоверен). Доп. ключи обёртки — аргументами .bat, напр. --print-cmd (сухо).
REM   chcp 65001 — чтобы маркер с кириллицей дошёл до обёртки целым; прежняя кодовая страница возвращается в конце.
for /f "tokens=2 delims=:" %%C in ('chcp') do set "ASDA_CP_WAS=%%C"
chcp 65001 >nul
set SCRIPT=@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_ui_caption_states_live_check.py
set LOG=@@ASTORY_DEV_ROOT@@\Reports\headless_asda_ui_caption_states_live_check.log
python "%~dp0run_headless_safe.py" "%SCRIPT%" --log "%LOG%" --exec-cmds --render-offscreen --res 1920x1200 --marker "[переимпорт-живой] === готово, снимков запрошено" %*
set RC=%ERRORLEVEL%
chcp %ASDA_CP_WAS% >nul
endlocal & exit /b %RC%
