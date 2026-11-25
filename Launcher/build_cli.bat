@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
rem Сборка лаунчера (Release, Ninja). Лог — build.log (UTF-8 вывод компилятора в кодировке консоли).
rem ★ 17.10.2026: Visual Studio с C++ ищется через vswhere (Community, BuildTools и т.п.), а не по жёсткому пути.
rem   Внимание: пути со скобками "(x86)" нельзя раскрывать внутри блоков ( ... ) — cmd сочтёт ")" концом блока.
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSDIR="
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR goto :novs
rem vcvars64 сам вызывает vswhere по имени — его папка должна быть в PATH
for %%d in ("%VSWHERE%") do set "PATH=%%~dpd;%PATH%"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
rem Движок UE: ASDA_UE_DIR (выставляет DEPLOY\_tools\deploy.py) перекрывает путь из CMakeCache; без неё — умолчание CMakeLists.txt
set "UEARG="
if defined ASDA_UE_DIR set UEARG="-DUE_ROOT=%ASDA_UE_DIR:\=/%"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release %UEARG% >build_cfg.log 2>&1 || exit /b 1
cmake --build build %* >build.log 2>&1
exit /b %ERRORLEVEL%

:novs
echo Visual Studio 2022 with C++ not found (vswhere: "%VSWHERE%")
exit /b 1
