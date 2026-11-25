@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
rem Новая версия патча
rem Подпись и подготовка версии в Releases. Источник: клиент из Inbox или последняя релизная сборка.
chcp 65001 >nul
where python >nul 2>&1 || (echo Python не найден в PATH & pause & exit /b 1)
python "%~dp0..\_tools\deploy.py" patch-make %*
echo.
pause
