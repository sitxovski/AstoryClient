@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
rem Ключи выпуска
rem Какие ключи есть и доступны.
chcp 65001 >nul
where python >nul 2>&1 || (echo Python не найден в PATH & pause & exit /b 1)
python "%~dp0..\_tools\deploy.py" keys-status %*
echo.
pause
