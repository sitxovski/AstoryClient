@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
rem FetchAndDecrypt.bat — забрать новые журналы игроков с dev-сервера (LogDrop) и расшифровать
rem в @@ASTORY_DEV_ROOT@@\_player_logs\<логин>\<дата>\ (вне депо Perforce). С 23.11.2026 — по SSH (scp), не по шаре.
rem Аргументы передаются как есть: --src <локальная папка> --dest <папка> --all --kinds alog,acrash --limit N.
chcp 65001 >nul
where python >nul 2>&1 || (echo Python не найден в PATH & pause & exit /b 1)
python "%~dp0fetch_and_decrypt.py" %*
set "RC=%ERRORLEVEL%"
pause
exit /b %RC%
