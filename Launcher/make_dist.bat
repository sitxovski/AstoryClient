@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
rem make_dist.bat — папка выпуска лаунчера dist\ (канал «launcher»): AsdaLauncher.exe + лицензии сторонних библиотек.
rem Затем: python ..\Tools\asda_release.py --channel launcher build --version launcher-X.Y.Z-ММ.ГГГГ
rem ★ 17.10.2026: новая папка собирается в dist.new и подменяет dist только целиком — при ошибке старая dist остаётся.
cd /d "%~dp0"
call "%~dp0build_cli.bat" || exit /b 1
if not exist build\AsdaLauncher.exe exit /b 1
if exist dist.new rmdir /s /q dist.new
mkdir dist.new || exit /b 1
copy /y build\AsdaLauncher.exe dist.new\ >nul || exit /b 1
(
echo AStory Remake Launcher — third-party components
echo.
echo OpenSSL 1.1.1t ^(libcrypto^)   — see OpenSSL_LICENSE.txt
echo Zstandard 1.4.7 ^(decompress^) — see zstd_LICENSE.txt
echo RapidJSON 1.1.0             — see RapidJSON_LICENSE.txt
) > dist.new\THIRD_PARTY_NOTICES.txt
copy /y licenses\*.txt dist.new\ >nul || exit /b 1
if exist dist rmdir /s /q dist
move dist.new dist >nul || exit /b 1
echo dist ready: %~dp0dist
