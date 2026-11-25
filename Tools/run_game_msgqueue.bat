@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM ★★★ САМОТЕСТ ОЧЕРЕДИ ОКОН-СООБЩЕНИЙ ONEBUT 1500 / TWOBUT 1520 ПУТЁМ ИГРОКА (-game), БЕЗ ОКОН НА ЭКРАНЕ (26.09.2026).
REM   * -game            — путь игрока, а не PIE (закон ui-check-game-path-geometry);
REM   * -RenderOffscreen — окон Unreal на экране не появляется (закон no-visible-editor-windows);
REM   * БЕЗ -NullRHI     — иначе Slate не рисует, геометрия нулевая.
REM   Сервера нет: самотест `asda.MsgBox.SelfTest` (Private/UI/AsdaMsgBoxQueueSelfTest.cpp) зовёт общие точки
REM   ShowGameMessage/ShowGameConfirm, жмёт кнопки делегатом OnClicked, ESC — CloseTopmostOnEsc.
REM   Строки проверок — «САМОТЕСТ-ОЧЕРЕДЬ», итог — «САМОТЕСТ-ОЧЕРЕДЬ ИТОГ». Первый аргумент — доп. команды перед самотестом.
setlocal
call "%~dp0asda_ue_engine.bat"
set ENGINE=%ASDA_UE_EDITOR_CMD%
set PROJECT=@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject
set LOG=@@ASTORY_DEV_ROOT@@\Reports\msgqueue\headless_msgqueue_game.log
set EXTRA=%~1
"%ENGINE%" "%PROJECT%" "/Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode" -game -AsdaNoNet -RenderOffscreen -unattended -nopause -nosplash -nosound -ResX=1280 -ResY=800 -stdout -FullStdOutLogOutput -abslog="%LOG%" -ExecCmds="%EXTRA%asda.MsgBox.SelfTest delay=8 quit"
echo === Готово. Код выхода: %ERRORLEVEL%. Лог: %LOG% ===
