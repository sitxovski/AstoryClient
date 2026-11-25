@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
rem Что изменилось для релизной сборки
rem Только анализ: какой режим выберет 3_Release_Build (ничего не собирает).
chcp 65001 >nul
where python >nul 2>&1 || (echo Python не найден в PATH & pause & exit /b 1)
python "%~dp0..\_tools\deploy.py" build-release --check %*
echo.
pause
