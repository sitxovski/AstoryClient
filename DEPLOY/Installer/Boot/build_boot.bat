@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
rem build_boot.bat — сборка обёртки установщика AsdaSetupBoot.exe (MSVC x64, статический CRT /MT: на машине игрока
rem рантайма VC++ ещё нет). Результат — build\AsdaSetupBoot.exe, лог — build\build.log. Вызывает deploy.py installer-build.
rem ★ 25.10.2026 (ревизия М4): все DLL, кроме kernel32, — с задержанной загрузкой (до неё exe успевает вызвать
rem   SetDefaultDllDirectories), зависимости ищутся только в System32 (/DEPENDENTLOADFLAG:0x800).
rem Visual Studio ищется через vswhere, как в AstoryClient\Launcher\build_cli.bat.
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSDIR="
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR goto :novs
for %%d in ("%VSWHERE%") do set "PATH=%%~dpd;%PATH%"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
rc /nologo /fo build\boot.res boot.rc >build\build.log 2>&1 || exit /b 1
cl /nologo /std:c++20 /utf-8 /O2 /MT /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /Fobuild\ /Febuild\AsdaSetupBoot.exe AsdaSetupBoot.cpp build\boot.res ^
   /link /SUBSYSTEM:WINDOWS /MANIFEST:NO /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /DEPENDENTLOADFLAG:0x800 ^
   /DELAYLOAD:user32.dll /DELAYLOAD:shell32.dll /DELAYLOAD:ole32.dll /DELAYLOAD:advapi32.dll /DELAYLOAD:bcrypt.dll ^
   delayimp.lib user32.lib shell32.lib ole32.lib advapi32.lib bcrypt.lib >>build\build.log 2>&1
exit /b %ERRORLEVEL%

:novs
echo Visual Studio 2022 with C++ not found (vswhere: "%VSWHERE%")
exit /b 1
