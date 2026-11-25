@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
rem Что изменилось для дев-сборки
rem Только анализ: какой режим выберет 2_Dev_Build (ничего не собирает).
chcp 65001 >nul
where python >nul 2>&1 || (echo Python не найден в PATH & pause & exit /b 1)
python "%~dp0..\_tools\deploy.py" build-dev --check %*
echo.
pause
