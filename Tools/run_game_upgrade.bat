@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM ★★★ САМОТЕСТ ОКНА «УЛУЧШЕНИЕ СНАРЯЖЕНИЯ» 20000 ПУТЁМ ИГРОКА (-game), БЕЗ ОКОН НА ЭКРАНЕ (25.09.2026).
REM   * -game            — путь игрока, а не PIE (закон ui-check-game-path-geometry);
REM   * -RenderOffscreen — окон Unreal на экране не появляется (закон no-visible-editor-windows);
REM   * БЕЗ -NullRHI     — иначе Slate не рисует, геометрия нулевая и кадров не будет.
REM   Сервера нет: самотест кладёт тестовые предметы в МОДЕЛЬ инвентаря и синтезирует ответы 6505,
REM   которые идут БОЕВЫМ приёмником окна (asda.Upgrade.SelfTest, AsdaItemUpgradeSubsystem.cpp).
REM   Кадры: @@ASTORY_DEV_ROOT@@\AstoryClient\Saved\Screenshots\WindowsEditor\asda_upgrade_*.png
REM   ★ 25.09.2026 (волны 5 и 7): после окна 20000 (≈23 с) тем же прогоном идёт самотест окна 78200
REM   (asda.HighRefine.SelfTest delay=24, AsdaHighRefineSubsystem.cpp); кадры asda_highrefine_*.png.
REM   Строки журнала звуков/анимаций волны 5 — «САМОТЕСТ-В5», окна 78200 — «САМОТЕСТ-78200».
setlocal
call "%~dp0asda_ue_engine.bat"
set ENGINE=%ASDA_UE_EDITOR_CMD%
set PROJECT=@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject
set LOG=@@ASTORY_DEV_ROOT@@\Reports\gear_upgrade\headless_upgrade_game.log
set EXTRA=%~1
"%ENGINE%" "%PROJECT%" "/Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode" -game -AsdaNoNet -RenderOffscreen -unattended -nopause -nosplash -nosound -ResX=1280 -ResY=800 -stdout -FullStdOutLogOutput -abslog="%LOG%" -ExecCmds="%EXTRA%asda.Upgrade.SelfTest shot,asda.HighRefine.SelfTest delay=24 shot quit"
echo === Готово. Код выхода: %ERRORLEVEL%. Лог: %LOG% ===
