@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
rem Бинарники редактора
rem UBT AsdaRebornEditor Win64 Development. Редактор должен быть закрыт.
chcp 65001 >nul
where python >nul 2>&1 || (echo Python не найден в PATH & pause & exit /b 1)
python "%~dp0..\_tools\deploy.py" build-editor %*
echo.
pause
