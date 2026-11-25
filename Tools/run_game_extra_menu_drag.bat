@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM ★★★ КАДРЫ ПЕРЕТАСКИВАНИЯ строки доп-меню «Прочее» (74600) в панель быстрого доступа — ПУТЁМ
REM   ИГРОКА (-game), без окон Unreal на экране.
REM   * -game            — путь игрока, а не PIE (закон ui-check-game-path-geometry: PIE-дамп врёт,
REM                        и вьюпорт PIE в headless всего 700x201 — панель туда не влезает);
REM   * -RenderOffscreen — окон на экране не появляется (закон no-visible-editor-windows);
REM   * БЕЗ -NullRHI     — иначе Slate не рисует, живая геометрия 0x0 и кадров не будет вовсе.
REM   Кадры: Saved\Screenshots\WindowsClient\asda_etcdrag_*.png (имена свои — безымянные
REM   ScreenShotNNNNN.png перетирает параллельный прогон чужого агента).
setlocal
call "%~dp0asda_ue_engine.bat"
set ENGINE=%ASDA_UE_EDITOR_CMD%
set PROJECT=@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject
set LOG=@@ASTORY_DEV_ROOT@@\Reports\headless_asda_extra_menu_drag_game.log
"%ENGINE%" "%PROJECT%" "/Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode" -game -AsdaNoNet -RenderOffscreen -unattended -nopause -nosplash -nosound -ResX=1280 -ResY=800 -stdout -FullStdOutLogOutput -abslog="%LOG%" -ExecCmds="asda.ExtraMenu.DragSelfTest quit"
echo === Готово. Код выхода: %ERRORLEVEL%. Лог: %LOG% ===
