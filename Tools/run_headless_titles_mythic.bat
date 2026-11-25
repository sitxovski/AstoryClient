@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM Правка соревновательных званий (id 65..69): грейд 8 «Мифический» + очки 188.
REM Идёт ТОЛЬКО через редактор достижений: патч-CSV -> Astory.Titles.ApplyPatch <путь> export.
REM Ключ export собирает заодно выгрузку серверного FameInfo.txt (режим «только правки сессии»),
REM   и делать это можно ИМЕННО здесь: снимок Baseline живёт ровно одну загрузку данных.
REM Без -NullRHI (внешний вид не проверяем, но DataTable сохраняется штатным путём), окно скрыто.
setlocal
call "%~dp0asda_ue_engine.bat"
set ENGINE=%ASDA_UE_EDITOR_CMD%
set PROJECT=@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject
set PATCH=@@ASTORY_DEV_ROOT_FWD@@/Reports/merged_titles/title_patch_competitive_mythic.csv
set LOG=@@ASTORY_DEV_ROOT@@\Reports\headless_titles_mythic.log
REM ★ Команды в -ExecCmds разделяются ЗАПЯТОЙ, а не «|»: с «|» весь хвост уезжает
REM   аргументами в первую команду, и ни SelfCheck, ни QUIT_EDITOR не выполняются.
"%ENGINE%" "%PROJECT%" -ExecCmds="Astory.Titles.ApplyPatch %PATCH% export,Astory.Titles.SelfCheck,QUIT_EDITOR" -unattended -nopause -nosplash -nosound -AsdaNoNet -RenderOffscreen -ResX=1280 -ResY=720 -stdout -FullStdOutLogOutput -abslog="%LOG%"
