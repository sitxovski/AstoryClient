# _tools

`deploy.py` — общий исполнитель батников `Build\` и `Patch\`. Логики сборки и подписи в нём нет: он вызывает готовые
инструменты проекта и ведёт журнал.

| Команда | Вызывает |
|---|---|
| `build-editor` | `<UE>\Engine\Build\BatchFiles\Build.bat AsdaRebornEditor Win64 Development` |
| `build-dev [--check]` | `AsdaReborn\Tools\eac_package.py --config Development [--dry-run]` |
| `build-release [--check]` | `AsdaReborn\Tools\eac_package.py [--dry-run]` |
| `build-launcher` | `Launcher\make_dist.bat` |
| `game-collect` | копирует `Launcher\dist`, `vc_redist.x64.exe` движка, установщик EAC из тулкита, EULA → `DEPLOY\GAME` + `manifest.json` |
| `installer-build [--version 1.2.3]` | `Installer\Branding\make_branding.py` + `ISCC.exe Installer\AStoryRemake.iss` (Inno Setup 6) |
| `patch-process` | `eac_package.py --verify-only` или `--skip-build` над клиентом из `Patch\Inbox` |
| `patch-make [--version V]` | `asda_release.py --channel alpha build --version V --game-root <клиент>` |
| `patch-verify` / `patch-upload [--dry-run]` / `patch-resign` | `asda_release.py verify --deep` / `upload` / `resign` |
| `patch-all [--version V]` | `patch-process` → `patch-make` → `patch-upload` |
| `launcher-release [--version V]` | `asda_release.py --channel launcher build` + `upload` |
| `status`, `keys-status`, `keys-backup` | `asda_release.py status`, `keys status`, `keys export-backup` |

Пути — только от расположения файла (`_tools` → `DEPLOY` → проект → `<ROOT>`, имя папки проекта любое). Движок: `ASDA_UE_DIR`, иначе ассоциация из
`AsdaReborn.uproject` в реестре, иначе `D:\Games\UE_5.8`; найденный путь передаётся дочерним процессам через `ASDA_UE_DIR`.
