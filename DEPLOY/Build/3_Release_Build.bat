@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
rem Релизная сборка: Shipping с EAC
rem Результат: Build\EAC\<дата>\Windows и latest.json. Режим выбирается сам (code/skipcook/iterate/full).
chcp 65001 >nul
where python >nul 2>&1 || (echo Python не найден в PATH & pause & exit /b 1)
python "%~dp0..\_tools\deploy.py" build-release %*
echo.
pause
