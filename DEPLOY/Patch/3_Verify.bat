@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
rem Проверка хранилища выпусков
rem Подписи, хэши, каждый кусок.
chcp 65001 >nul
where python >nul 2>&1 || (echo Python не найден в PATH & pause & exit /b 1)
python "%~dp0..\_tools\deploy.py" patch-verify %*
echo.
pause
