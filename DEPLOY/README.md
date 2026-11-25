# DEPLOY — сборка и выпуск AStory Remake

Всё запускается двойным кликом по `.bat`. Каждый батник пишет журнал в свою папку `Logs` и в конце печатает **ГОТОВО** или **ОШИБКА** с путём к журналу.

| Папка | Что внутри |
|---|---|
| `Build\` | бинарники редактора, дев-сборка, релизная сборка, лаунчер |
| `Patch\` | обработка клиента, подготовка и подпись патча, проверка, выгрузка на сервер |
| `Patch\Inbox\` | сюда кладётся клиент, из которого делается патч |
| `GAME\` | что ставится игроку: актуальный лаунчер + рантайм VC++ + служба EAC + EULA |
| `Installer\` | установщик Inno Setup с оформлением; готовые `.exe` — в `Installer\Output` |
| `AdminCenter\` | запуск, остановка и состояние админ-центра на сервере (`AdminCenter.bat`) |
| `_tools\` | `deploy.py` — общий исполнитель всех батников |

## Установщик для игроков
`Build\4_Launcher_Build.bat` (если менялся лаунчер) → `Build\5_Collect_GAME.bat` → `Build\6_Installer_Build.bat`.
Установщик ставит лаунчер, рантайм и службу EAC, прописывает реестр и ярлык «Astory»; игру лаунчер скачает сам.

## Обычный цикл выпуска
1. `Build\3_Release_Build.bat` — собрать релизный клиент (Shipping с EAC).
2. `Patch\0_Patch_All.bat` — ввести версию; дальше всё само: подпись, проверка, выгрузка.

Клиент собран на другом компьютере? Положите его в `Patch\Inbox\` (папкой или zip) и запустите `Patch\0_Patch_All.bat`.

## Требования
- Python 3 в PATH (с Pillow — для оформления установщика), Visual Studio 2022 (для лаунчера), Unreal Engine 5.8, Inno Setup 6.5+.
- Путь к движку определяется сам (`ASDA_UE_DIR`, иначе установка UE 5.8 из Epic Games Launcher по `EngineAssociation: 5.8` в `AsdaReborn.uproject`, иначе `D:\Games\UE_5.8`); движок в другом месте — задайте переменную `ASDA_UE_DIR`.
- Перед любой сборкой закройте редактор: скрипты процессы не завершают.
- Для выгрузки нужен ключ `%USERPROFILE%\.ssh\asdaupd_upload`, для подписи — ключи выпуска (`Patch\Keys_Status.bat`).

## Сервер выгрузки обновлений (один раз)
Выгрузка (`Patch\4_Upload.bat`, `Patch\0_Patch_All.bat`, `Patch\5_Launcher_Release.bat` и т.п.) идёт по SFTP на ВАШ сервер обновлений.
1. Значения сервера задаёт `_setup\configure.bat`: `ASTORY_UPDATE_SSH_HOST` (SSH-хост выкладки), `ASTORY_UPDATE_SSH_USER`
   (SFTP-пользователь, лучше в chroot), `ASTORY_UPDATE_SSH_ROOT` (корень сайта обновлений внутри chroot, например `/www`).
   Пока они не заданы, выгрузка останавливается с подсказкой.
2. Ключ выкладки: `ssh-keygen -t ed25519 -f %USERPROFILE%\.ssh\asdaupd_upload`, открытый ключ — в `authorized_keys` этого пользователя на сервере.
3. Закреплённый ключ хоста — файл `Tools\data\updater_known_hosts`. В git он НЕ хранится (у каждого свой сервер), создайте его сами:
   ```
   ssh-keyscan -t ed25519 <ваш SSH-хост> > Tools\data\updater_known_hosts
   ssh-keygen -lf Tools\data\updater_known_hosts
   ```
   Сверьте показанный отпечаток SHA256 с отпечатком самого сервера (на сервере: `ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub`).
   Команды — в `cmd` из корня проекта (в Windows PowerShell 5.1 `>` пишет UTF-16, который OpenSSH не читает:
   там `ssh-keyscan ... | Out-File -Encoding ascii Tools\data\updater_known_hosts`). Не совпал — файл удалить и не выгружать. Выгрузка работает со `StrictHostKeyChecking=yes` только по этому файлу.

Все пути в скриптах — относительно их расположения; папку проекта можно переносить целиком вместе с соседями в `<ROOT>`.
