@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
rem Текущие версии каналов
rem alpha и launcher.
chcp 65001 >nul
where python >nul 2>&1 || (echo Python не найден в PATH & pause & exit /b 1)
python "%~dp0..\_tools\deploy.py" status %*
echo.
pause
