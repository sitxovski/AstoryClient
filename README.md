<div align="center">

**English** · [Русский](README.ru.md) · [العربية](README.ar.md)

### ❤️ Like the project? Support the hard work on [**Boosty**](https://boosty.to/sitxovski_yurii)

# Astory Client

**The game client of Astory — a remake of a classic MMORPG on Unreal Engine 5.8**

[Astory Client](https://github.com/sitxovski/AstoryClient) · [Astory Server](https://github.com/sitxovski/AstoryServer) · [Astory Admin Center](https://github.com/sitxovski/AstoryAdminCenter)

</div>

---

> ⚠️ **Disclaimer.** This package (Not whole project!) was built using **Claude Code** and may contain issues. Feel free to DM me on Discord (**@sitxovski**) to get support.

## What's inside

| Folder | What it is |
|---|---|
| `Source` | the game in C++ (UE 5.8): networking, UI, combat, world, anti-cheat |
| `Config`, `Plugins` | engine settings and editor plugins |
| `Tools` | Python tools: import, data, release signing |
| `Launcher` | the game launcher with auto-update (C++ / CMake) |
| `DEPLOY` | one-click build, patch and installer scripts |
| `Prerequisites` | what to install, how to build and run |

> 📦 **Game resources are a separate download.** Get [**AstoryClient Resources**](https://drive.google.com/drive/folders/1p9x3ekIwU1PdqFaRMMeuR9ugiUC9DaT9?usp=sharing)
> and copy its `Content` folder into this folder, so you have `AstoryClient\Content\AsdaAssets\...`.

## Quick start

1. **Clone** — into a folder named exactly `AstoryClient`, next to `AstoryServer` and `AstoryAdminCenter` (a GitHub ZIP unpacks as `AstoryClient-main` — rename it).
2. **Install** — run `Prerequisites\install_prerequisites.bat` ([details](Prerequisites/README.md)).
3. **Resources** — put `Content` from [*AstoryClient Resources*](https://drive.google.com/drive/folders/1p9x3ekIwU1PdqFaRMMeuR9ugiUC9DaT9?usp=sharing) into this folder.
4. **Configure** — run `_setup\configure.bat` (your server, hosts and Epic IDs — [where to get them](_setup/SETUP.md)).
   Run it in all three clones; the answers are reused, so the other clones ask only what is new.
5. **Build** — `DEPLOY\Build\1_Editor_Binaries.bat`, then open `AsdaReborn.uproject`.
   First copy `server_public.h` of your server into `Source\AsdaNet\Private\Secure\` (made by [Astory Server](https://github.com/sitxovski/AstoryServer) setup — until then the build stops with a hint).
   Before the first release build make your own keys (pak keys, LogSeal, Easy Anti-Cheat) — [Prerequisites](Prerequisites/README.md).

The client talks to your own game server — see [Astory Server](https://github.com/sitxovski/AstoryServer).
To run the game for players you will also want [Astory Admin Center](https://github.com/sitxovski/AstoryAdminCenter).

## Credits

Made by **@sitxovski** (Discord).

Free to use for anything, no restrictions — [MIT License](LICENSE). **Just credit the original author — @sitxovski (Discord) —
and link to this repository when you copy, fork or reuse any part of it.**

---

<div align="center">

### ❤️ Support the hard work on [**Boosty**](https://boosty.to/sitxovski_yurii)

</div>
