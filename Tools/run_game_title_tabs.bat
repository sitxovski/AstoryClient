@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM ★★★ КАДРЫ ВКЛАДОК 18713 ПУТЁМ ИГРОКА (-game), БЕЗ ОКОН НА ЭКРАНЕ.
REM   Зачем: числа `GetCachedGeometry` трижды рапортовали «вкладки в порядке», пока владелец
REM   видел плоские «Соревнование»/«Секретные». Доказательством вида считается только КАДР.
REM   * -game            — путь игрока, а не PIE (закон ui-check-game-path-geometry);
REM   * -RenderOffscreen — окон Unreal на экране не появляется (закон no-visible-editor-windows);
REM   * БЕЗ -NullRHI     — иначе Slate не рисует и снимков не будет вовсе.
REM   Снимки движок кладёт в @@ASTORY_DEV_ROOT@@\AstoryClient\Saved\Screenshots\WindowsEditor\.
setlocal
call "%~dp0asda_ue_engine.bat"
set ENGINE=%ASDA_UE_EDITOR_CMD%
set PROJECT=@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject
set LOG=@@ASTORY_DEV_ROOT@@\Reports\headless_title_tabs_game.log
"%ENGINE%" "%PROJECT%" "/Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode" -game -AsdaNoNet -RenderOffscreen -unattended -nopause -nosplash -nosound -ResX=1280 -ResY=800 -stdout -FullStdOutLogOutput -abslog="%LOG%" -ExecCmds="asda.Title.SelfTest shot quit"
