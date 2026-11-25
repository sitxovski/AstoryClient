# Build — сборка

| Батник | Что делает | Результат | Время |
|---|---|---|---|
| `1_Editor_Binaries.bat` | бинарники редактора (AsdaRebornEditor, Development) | `AsdaReborn\Binaries\Win64` | 1–17 мин |
| `2_Dev_Check.bat` | только анализ: что изменилось и какой режим выберет дев-сборка | журнал | ~2 мин |
| `2_Dev_Build.bat` | **дев-сборка**: Development с EAC, шифрованием и подписью, консоль включена | `Build\Dev\<дата>\Windows` | 3–25 мин |
| `3_Release_Check.bat` | только анализ для релизной сборки | журнал | ~20 с |
| `3_Release_Build.bat` | **релизная сборка**: Shipping с EAC | `Build\EAC\<дата>\Windows` + `latest.json` | 3–25 мин |
| `4_Launcher_Build.bat` | лаунчер | `AstoryClient\Launcher\dist` | ~1 мин |
| `5_Collect_GAME.bat` | папка для установки игроку: лаунчер + рантайм VC++ + служба EAC + EULA | `DEPLOY\GAME` | секунды |
| `6_Installer_Build.bat` | брендированный установщик (Inno Setup) из `GAME` | `DEPLOY\Installer\Output\AStoryRemake_Setup_<версия>.exe` | ~10 с |

- Режим сборки выбирается сам по изменениям с прошлой сборки: только код — несколько минут, контент — инкрементальный кук, смена движка или ключей — полная сборка.
- Дев-сборка запускается через `Start_Dev.bat` в её корне (`Astory.exe -AsdaEac`): вне Shipping EAC включается только этим флагом.
- У дев- и релизной сборки отдельные цепочки баз, они друг другу не мешают.
- Смена ключей шифрования — только при смене major-версии: `python AsdaReborn\Tools\eac_package.py --mode full --rotate-keys`.

Журналы — `Logs\`; подробный журнал упаковки — рядом со сборкой (`<дата>.logs\`).
