@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
rem Дев-сборка: Development с EAC
rem Результат: Build\Dev\<дата>\Windows. Запуск игры - Start_Dev.bat (Astory.exe -AsdaEac).
chcp 65001 >nul
where python >nul 2>&1 || (echo Python не найден в PATH & pause & exit /b 1)
python "%~dp0..\_tools\deploy.py" build-dev %*
echo.
pause
