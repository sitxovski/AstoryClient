@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM Живая PIE-проверка окна разговора с NPC (JUI 570). Отличие от run_headless_rhi_tick.bat —
REM РАЗМЕР ОКНА РЕДАКТОРА: окно диалога рассчитано на базу 1024x768 и привязано к правому-нижнему
REM углу, в стандартном 960x777 вьюпорт слишком мал и верх окна уезжает за край (ложное «пусто»).
setlocal
REM ★ Т1 (11.10.2026, волна В0 dedup_audit2): запуск через run_headless_safe.py — редактор БЕЗ ОКНА (-RenderOffscreen),
REM   -SCCProvider=None, -AsdaNoNet (SELFTESTS_OFFLINE.md); успех ПО ЛОГУ: код 0 успех / 3 провал / 4 таймаут и
REM   <лог>.result.json (код выхода UE недостоверен). Доп. ключи обёртки — аргументами .bat, напр. --print-cmd (сухо).
REM   chcp 65001 — чтобы маркер с кириллицей дошёл до обёртки целым; прежняя кодовая страница возвращается в конце.
for /f "tokens=2 delims=:" %%C in ('chcp') do set "ASDA_CP_WAS=%%C"
chcp 65001 >nul
set SCRIPT=@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_npc_dialog_live_check.py
set LOG=@@ASTORY_DEV_ROOT@@\Reports\headless_asda_npc_dialog_live_check.log
python "%~dp0run_headless_safe.py" "%SCRIPT%" --log "%LOG%" --exec-cmds --render-offscreen --res 1920x1200 --marker "[npc-диалог-живой] === готово, снимков запрошено" %*
set RC=%ERRORLEVEL%
chcp %ASDA_CP_WAS% >nul
endlocal & exit /b %RC%
