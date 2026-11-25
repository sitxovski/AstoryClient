@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
rem Продлить подпись канала
rem Та же версия, новый срок свежести подписи (если давно не было патчей).
chcp 65001 >nul
where python >nul 2>&1 || (echo Python не найден в PATH & pause & exit /b 1)
python "%~dp0..\_tools\deploy.py" patch-resign %*
echo.
pause
