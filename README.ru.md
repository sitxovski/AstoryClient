<div align="center">

[English](README.md) · **Русский** · [العربية](README.ar.md)

### ❤️ Нравится проект? Поддержите труд на [**Boosty**](https://boosty.to/sitxovski_yurii)

# Astory Client

**Игровой клиент Astory — ремейк классической MMORPG на Unreal Engine 5.8**

[Astory Client](https://github.com/sitxovski/AstoryClient) · [Astory Server](https://github.com/sitxovski/AstoryServer) · [Astory Admin Center](https://github.com/sitxovski/AstoryAdminCenter)

</div>

---

> ⚠️ **Дисклеймер.** Этот пакет (Только репозиторий. Проект - ручная работа с помощью ИИ) собран с помощью **Claude Code** и может содержать ошибки. Пишите мне в личные сообщения в Discord (**@sitxovski**) — помогу.

## Что внутри

| Папка | Что это |
|---|---|
| `Source` | игра на C++ (UE 5.8): сеть, интерфейс, бой, мир, античит |
| `Config`, `Plugins` | настройки движка и плагины редактора |
| `Tools` | инструменты на Python: импорт, данные, подпись выпусков |
| `Launcher` | лаунчер игры с автообновлением (C++ / CMake) |
| `DEPLOY` | сборка, патч и установщик в один клик |
| `Prerequisites` | что установить, как собрать и запустить |

> 📦 **Ресурсы игры скачиваются отдельно.** Возьмите [**AstoryClient Resources**](https://drive.google.com/drive/folders/1p9x3ekIwU1PdqFaRMMeuR9ugiUC9DaT9?usp=sharing)
> и скопируйте папку `Content` в эту папку, чтобы получилось `AstoryClient\Content\AsdaAssets\...`.

## Быстрый старт

1. **Клонирование** — в папку с именем ровно `AstoryClient`, рядом с `AstoryServer` и `AstoryAdminCenter` (ZIP с GitHub распаковывается как `AstoryClient-main` — переименуйте).
2. **Установка** — запустите `Prerequisites\install_prerequisites.bat` ([подробно](Prerequisites/README.md)).
3. **Ресурсы** — положите `Content` из [*AstoryClient Resources*](https://drive.google.com/drive/folders/1p9x3ekIwU1PdqFaRMMeuR9ugiUC9DaT9?usp=sharing) в эту папку.
4. **Настройка** — запустите `_setup\configure.bat` (ваш сервер, адреса и ключи Epic — [где их взять](_setup/SETUP.md)).
   Запустите его во всех трёх клонах; ответы переиспользуются, в других клонах спросят только новое.
5. **Сборка** — `DEPLOY\Build\1_Editor_Binaries.bat`, затем откройте `AsdaReborn.uproject`.
   Сначала положите `server_public.h` вашего сервера в `Source\AsdaNet\Private\Secure\` (его создаёт установка [Astory Server](https://github.com/sitxovski/AstoryServer); до этого сборка останавливается с подсказкой).
   Перед первой выпускной сборкой создайте свои ключи (ключи pak, LogSeal, Easy Anti-Cheat) — [Prerequisites](Prerequisites/README.md).

Клиент подключается к вашему игровому серверу — см. [Astory Server](https://github.com/sitxovski/AstoryServer).
Чтобы вести сервер для игроков, пригодится [Astory Admin Center](https://github.com/sitxovski/AstoryAdminCenter).

## Автор

Автор — **@sitxovski** (Discord).

Можно использовать свободно и без ограничений — [лицензия MIT](LICENSE). **Только укажите автора — @sitxovski (Discord) —
и ссылку на этот репозиторий, когда копируете, форкаете или используете любую часть проекта.**

---

<div align="center">

### ❤️ Поддержите труд на [**Boosty**](https://boosty.to/sitxovski_yurii)

</div>
