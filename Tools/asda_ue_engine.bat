rem Copyright (c) 2026 @sitxovski (Discord)
@rem asda_ue_engine.bat - ЕДИНСТВЕННОЕ место пути к движку UE для .bat-обёрток Tools (гигиена 15.09.2026, §4 п.3).
@rem   Вызов из обёртки:  call "%~dp0asda_ue_engine.bat"   (затем %ASDA_UE_EDITOR_CMD% / %ASDA_UE_EDITOR%).
@rem   Другая машина: set ASDA_UE_DIR=<корень движка> ДО запуска обёртки. Без неё - D:\Games\UE_5.8, как было.
@rem   Python-сторона пути к движку держит свою константу: Tools/asda_pie_harness.py UE_CMD.
@if not defined ASDA_UE_DIR set "ASDA_UE_DIR=D:\Games\UE_5.8"
@set "ASDA_UE_EDITOR_CMD=%ASDA_UE_DIR%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
@set "ASDA_UE_EDITOR=%ASDA_UE_DIR%\Engine\Binaries\Win64\UnrealEditor.exe"
