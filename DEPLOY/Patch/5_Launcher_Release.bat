@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
rem Выпуск лаунчера
rem Канал launcher из AstoryClient\Launcher\dist и выгрузка на сервер.
chcp 65001 >nul
where python >nul 2>&1 || (echo Python не найден в PATH & pause & exit /b 1)
python "%~dp0..\_tools\deploy.py" launcher-release %*
echo.
pause
