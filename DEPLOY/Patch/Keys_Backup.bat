@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
rem Копия ключей выпуска
rem Закрытые ключи под пароль владельца.
chcp 65001 >nul
where python >nul 2>&1 || (echo Python не найден в PATH & pause & exit /b 1)
python "%~dp0..\_tools\deploy.py" keys-backup %*
echo.
pause
