# Installer — установщик AStory Remake

Сборка: `Build\6_Installer_Build.bat` (нужен Inno Setup 6.5+). Результат — `Output\AStoryRemake_Setup_<версия>.exe`,
версия берётся из последнего выпуска лаунчера (`Releases\releases.log`).

**Что делает установщик:**
- язык мастера — русский или английский; лицензия EULA; папка установки на выбор (по умолчанию `C:\Program Files\AStory Remake`);
- ставит лаунчер; папке выдаются права на запись пользователям — лаунчер качает игру в `Game\` и обновляется без прав администратора;
- ставит рантайм Visual C++ x64 (только если его нет или он старее) и службу Easy Anti-Cheat;
- реестр: `HKLM\Software\AStoryRemake` — `InstallDir`, `GameDir`, `LauncherExe`, `Version`, `EacProductId`; язык лаунчера
  (`HKCU\Software\AStoryRemake\Launcher\Lang`) по языку установки, если его ещё не выбирали;
- ярлык **Astory** на рабочем столе (галочка) и в меню «Пуск»; в конце предлагает запустить лаунчер;
- удаление: лаунчер, скачанная игра, служба EAC, записи в реестре; запись в «Приложения и возможности».

**★ 25.10.2026 — без запуска из %TEMP%** (баг игрока «Error 5» на рабочем ПК, где запуск из TEMP запрещён; отчёт
`Reports\launcher_temp\REPORT.md`): Inno собирается без загрузчика (`UseSetupLdr=no`) в `Output\_inner\<версия>\`, а
`Output\AStoryRemake_Setup_<версия>.exe` = обёртка `Boot\build\AsdaSetupBoot.exe` + эти файлы (deploy.py `pack_setup_boot`).
Обёртка разворачивает их в `%LOCALAPPDATA%\AsdaReborn\Setup\s-*` (запасная — папка рядом с exe), проверяя пробным запуском,
и запускает setup оттуда. Раскладка: всё, что запускается от администратора (деинсталлятор и его данные, `AStoryUninstall.exe`,
установщик службы EAC, временный vc_redist), — в `{app}\Setup`, закрытой для пользователей (только чтение); пользователям
«изменение» — `{app}` (лаунчер, `Game`), если папка наша, иначе только `Game`. Права — `icacls`; коды vc_redist и EAC
проверяются. Удаление — `{app}\Setup\AStoryUninstall.exe`: сам повышает права, запускает `unins000.exe` с TEMP в закрытой
папке, ждёт и возвращает код. Ревизия и проверки — `Reports\launcher_temp\` (REVIEW.md, REPORT.md, UAC_CHECK.md).

| Файл | Назначение |
|---|---|
| `AStoryRemake.iss` | сценарий Inno Setup (UTF-8 с BOM); пути — относительно него |
| `Branding\make_branding.py` | генерирует картинки мастера из заставки игры и копирует иконку игры |
| `Branding\*.bmp`, `AStory.ico` | боковая картинка и эмблема шапки для всех DPI; иконка установщика |
| `Output\` | готовые установщики; `Output\_inner\<версия>\` — файлы Inno до упаковки |
| `Boot\AsdaSetupBoot.cpp`, `boot.rc`, `boot.manifest`, `build_boot.bat` | обёртка установщика и помощник удаления (MSVC, /MT, asInvoker) |
