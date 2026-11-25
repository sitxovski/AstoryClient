@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
rem Собрать DEPLOY\GAME
rem Актуальный лаунчер (Launcher\dist) + рантайм VC++ + установщик службы EAC + EULA.
chcp 65001 >nul
where python >nul 2>&1 || (echo Python не найден в PATH & pause & exit /b 1)
python "%~dp0..\_tools\deploy.py" game-collect %*
echo.
pause
