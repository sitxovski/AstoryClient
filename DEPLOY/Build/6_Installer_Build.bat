@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
rem Собрать установщик
rem Inno Setup: DEPLOY\Installer\Output\AStoryRemake_Setup_<версия>.exe (GAME собирается, если его нет).
chcp 65001 >nul
where python >nul 2>&1 || (echo Python не найден в PATH & pause & exit /b 1)
python "%~dp0..\_tools\deploy.py" installer-build %*
echo.
pause
