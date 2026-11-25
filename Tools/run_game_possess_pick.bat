@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM Самопроверка хит-зон сцены «одержимости» ПУТЁМ ИГРОКА (закон ui-check-game-path-geometry):
REM   * `-game` (не PIE), `-RenderOffscreen` (окно не создаётся — правило владельца «окон не показывать»),
REM     БЕЗ -NullRHI (нужны кадры/вьюпорт для проекции);
REM   * ASDA_TEST_FLOW=1 — char-select БЕЗ сервера (синтетический персонаж в слоте 0 + силуэт «души»);
REM   * через 25 с (перелёт + открытие шкатулки + фейд) команда снимает геометрию и выходит.
REM Смотреть в логе строки `[PossessPickTest]`: коробка ПИКА vs хулл ВЕРШИН (экранные прямоугольники,
REM   сдвиг краёв в px) и две карты ЛУЧ/ВИДНО с числом расхождений. Расхождений ≈ 0 = хит-зона на теле.
REM Запуск скрыто: powershell -Command "Start-Process -WindowStyle Hidden -Wait -FilePath '%~f0'"
setlocal
call "%~dp0asda_ue_engine.bat"
set ENGINE=%ASDA_UE_EDITOR%
set PROJECT=@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject
set LOG=@@ASTORY_DEV_ROOT@@\Reports\headless_game_possess_pick.log
set ASDA_TEST_FLOW=1
"%ENGINE%" "%PROJECT%" /Game/Map/Login -game -AsdaNoNet -RenderOffscreen -ResX=1600 -ResY=900 -unattended -nopause -nosplash -nosound -stdout -FullStdOutLogOutput -abslog="%LOG%" -ExecCmds="asda.Login.PossessPickTest 25 quit"
