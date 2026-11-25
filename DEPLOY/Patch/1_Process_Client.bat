@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
rem Обработать клиент из Inbox
rem Готовая EAC-сборка - проверка; голая сборка UAT - EAC-обработка (Astory.exe, Settings.json, целостность).
chcp 65001 >nul
where python >nul 2>&1 || (echo Python не найден в PATH & pause & exit /b 1)
python "%~dp0..\_tools\deploy.py" patch-process %*
echo.
pause
