@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM ★★★ КАДРЫ ОКНА «ПОИСК ОТРЯДА» 8500 ПУТЁМ ИГРОКА (-game), БЕЗ ОКОН НА ЭКРАНЕ.
REM   Зачем: владелец жалуется на ВИД (тёмная плитка карт, зелёная рамка выбора уезжает в угол).
REM   Числа `GetCachedGeometry` доказывают, ЧТО МЫ ПОСТАВИЛИ, но не то, что вышло на экран, —
REM   доказательством вида считается только КАДР (закон из брифинга, обожглись на вкладках 18713).
REM   * -game            — путь игрока, а не PIE (закон ui-check-game-path-geometry);
REM   * -RenderOffscreen — окон Unreal на экране не появляется (закон no-visible-editor-windows);
REM   * БЕЗ -NullRHI     — иначе Slate не рисует, геометрия нулевая и снимков не будет вовсе.
REM   Снимки движок кладёт в @@ASTORY_DEV_ROOT@@\AstoryClient\Saved\Screenshots\WindowsEditor\
REM   под ИМЕНАМИ asda_partyfind_map<IDNum>.png (безымянные перетирает чужой параллельный прогон).
setlocal
call "%~dp0asda_ue_engine.bat"
set ENGINE=%ASDA_UE_EDITOR_CMD%
set PROJECT=@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject
set LOG=@@ASTORY_DEV_ROOT@@\Reports\headless_partyfind_game.log
set EXTRA=%~1
"%ENGINE%" "%PROJECT%" "/Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode" -game -AsdaNoNet -RenderOffscreen -unattended -nopause -nosplash -nosound -ResX=1280 -ResY=800 -stdout -FullStdOutLogOutput -abslog="%LOG%" -ExecCmds="%EXTRA%asda.PartyFind.SelfTest shot quit"
echo === Готово. Код выхода: %ERRORLEVEL%. Лог: %LOG% ===
