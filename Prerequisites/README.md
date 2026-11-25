# Astory Client — prerequisites, build, run

Copyright (c) 2026 @sitxovski (Discord)

The client is an **Unreal Engine 5.8** C++ project (`AsdaReborn.uproject` in the repository root) with a launcher (`Launcher`, C++/CMake),
a player installer (`DEPLOY\Installer`, Inno Setup) and Python tools (`Tools`, `DEPLOY\_tools`).

**Folder layout.** `<ASTORY_DEV_ROOT>` is the folder that holds the clones side by side (`AstoryClient`, `AstoryServer`, `AstoryAdminCenter`)
plus shared folders the tools create or expect there (`ThirdParty`, `Build`, `Releases`, `Donors`, ...). Clone this repository into a folder
named exactly **`AstoryClient`** (`git clone https://github.com/sitxovski/AstoryClient <ASTORY_DEV_ROOT>\AstoryClient`); a GitHub
"Download ZIP" unpacks as `AstoryClient-main` — rename it to `AstoryClient`, many tools in `Tools` look for that name.
Unless marked `<ASTORY_DEV_ROOT>\…`, every path and command below is relative to the repository root (`<ASTORY_DEV_ROOT>\AstoryClient`).

## 1. Install

| What | Version | How |
|---|---|---|
| Windows 10/11 x64 | — | 16 GB RAM or more recommended, ~120 GB free disk (engine + project + derived data) |
| Visual Studio 2022 | 17.8+ | workloads *Game development with C++*, *Desktop development with C++*, *.NET desktop development*; Windows 11 SDK; .NET Framework 4.8 SDK — `install_prerequisites.bat` installs it with these workloads |
| Unreal Engine | 5.8 | Epic Games Launcher → *Unreal Engine* → *Library* → *+* → 5.8 (the project uses `"EngineAssociation": "5.8"`, found through the registry). Engine elsewhere (source build, other folder) — set the environment variable **`ASDA_UE_DIR`** to the engine root (the folder that contains `Engine`); the build scripts, the launcher build and the tools in `Tools` read it. Without it the tools fall back to `D:\Games\UE_5.8` |
| Python | 3.11+ | winget `Python.Python.3.13`; packages: `pip install -r Prerequisites\requirements.txt` |
| CMake + Ninja | 3.20+ | winget `Kitware.CMake`, `Ninja-build.Ninja` (launcher) |
| Inno Setup | 6.5+ | winget `JRSoftware.InnoSetup` (installer) |
| Game resources | — | [**AstoryClient Resources**](https://drive.google.com/drive/folders/1p9x3ekIwU1PdqFaRMMeuR9ugiUC9DaT9?usp=sharing) (Google Drive): copy its `Content` folder into the repository root (→ `Content\AsdaAssets\…`) |
| Epic Online Services | — | a product in <https://dev.epicgames.com/portal> with Easy Anti-Cheat enabled (IDs go into `_setup\configure.bat`) |
| EOS SDK 1.19.2.1 | required | download **EOS-SDK-58105819-Release-v1.19.2.1** from <https://dev.epicgames.com/portal> (*SDK Download & Credentials*) and unpack it into `<ASTORY_DEV_ROOT>\ThirdParty\EOS-SDK-58105819-Release-v1.19.2.1\` (next to the repositories; inside are `SDK`, `Samples`, ...). Copy its `SDK\Lib` and `SDK\Bin` folders into `Source\ThirdParty\AsdaEOSSDK\SDK\` (the headers are already there; without them the editor build stops with an `AsdaEOSSDK` error). The repository holds no binaries |
| EAC integrity keys | required for EAC builds | Developer Portal → your product → **Anti-Cheat** → download the integrity tool keys **`base_private.key`** and **`base_public.cer`** and put both into the root of the SDK folder: `<ASTORY_DEV_ROOT>\ThirdParty\EOS-SDK-58105819-Release-v1.19.2.1\` (next to `SDK` and `Samples`). They are secrets of your product — never commit them. Needed by `2_Dev_Build`, `3_Release_Build` and `Patch\1_Process_Client` |

Run `Prerequisites\install_prerequisites.bat` (asks before each package; `-Yes` — no questions). It prints the manual steps at the end.

## 2. Configure

1. `_setup\configure.bat` — Epic IDs, update / auth hosts, update upload server, keys. Answer **y** to generate the pak keys
   (or later: `python Tools\gen_build_crypto.py`).
2. Right-click `AsdaReborn.uproject` → **Generate Visual Studio project files** (if the engine is not picked up, **Switch Unreal Engine version…** → 5.8).
3. Sealed-log key (needed before the **first Shipping build**; the editor and Development builds work without it):
   `python Tools\asda_logseal.py keygen`. It creates `Tools\data\logseal\current.txt` + `logseal_<keyId>.pub` and
   `Source\AsdaNet\Private\LogSeal\AsdaLogSealKey.generated.h` (public key only); the private key goes to the Windows DPAPI store
   of your account (`%APPDATA%\AsdaReborn\LogSealKeys`) — back it up with `python Tools\asda_logseal.py export-backup`, without it
   player logs cannot be decrypted. Put the keyId from `current.txt` into `_setup\configure.bat` (`ASTORY_LOGSEAL_KEY_ID`) and rebuild
   the launcher. A Shipping build without the key stops with `#error "AsdaLogSeal: ... keygen"`.
4. Release signing keys (only if you publish updates): `python Tools\asda_release.py keys init`, put the hex of
   `<ASTORY_DEV_ROOT>\Releases\keys\root.pub` into `_setup\configure.bat` (`ASTORY_LAUNCHER_ROOT_PUBKEY`), back up with
   `DEPLOY\Patch\Keys_Backup.bat`. A launcher built without this key builds (CMake prints a warning) but refuses to update.
5. Update upload server (only if you publish updates): SSH host, user and root in `_setup\configure.bat`
   (`ASTORY_UPDATE_SSH_HOST`, `ASTORY_UPDATE_SSH_USER`, `ASTORY_UPDATE_SSH_ROOT`), an upload key `%USERPROFILE%\.ssh\asdaupd_upload`
   and a pinned host key `Tools\data\updater_known_hosts` made with `ssh-keyscan` **after checking the fingerprint** — it is not in git;
   steps in `DEPLOY\README.md`.

## 3. Build

| What | How |
|---|---|
| editor binaries | `DEPLOY\Build\1_Editor_Binaries.bat` (or open `AsdaReborn.sln` → *Development Editor* / *Win64* → Build) |
| development game (with EAC) | `DEPLOY\Build\2_Dev_Build.bat` (`2_Dev_Check.bat` — checks only) |
| release game (Shipping, encrypted and signed paks, EAC) | `DEPLOY\Build\3_Release_Build.bat` |
| launcher | `DEPLOY\Build\4_Launcher_Build.bat` (or `Launcher\build_cli.bat`) |
| player installer | `DEPLOY\Build\5_Collect_GAME.bat` → `DEPLOY\Build\6_Installer_Build.bat` (output `DEPLOY\Installer\Output`) |
| update for players | `DEPLOY\Patch\0_Patch_All.bat` (process → patch → sign → verify → upload) |

Close the Unreal Editor before any build. Every script writes a log into its `Logs` folder and ends with **DONE** or **ERROR**.
Builds go to `<ASTORY_DEV_ROOT>\Build`, update releases to `<ASTORY_DEV_ROOT>\Releases`. Details: `DEPLOY\README.md`.

## 4. Run

* **Editor:** double-click `AsdaReborn.uproject` → Play (the login map opens first). The client connects to your game server
  (`AstoryServer`) at the address set in `_setup\configure.bat`.
* **Game:** the built game is started by the launcher (`AsdaLauncher.exe`), which checks updates at `ASTORY_UPDATE_HOST`.
* **Offline** (no connection to a game server, e.g. for self-tests): add `-AsdaNoNet` to the command line.

## 5. Connect to your server

* `ASTORY_AUTH_HOST` (set by `_setup\configure.bat`) is compiled into the client: a domain whose DNS A record points to your
  server's public IP. The client logs in on port **15001** of that host and then joins the game servers (15601 / 15621 / 15651).
* Configure the server with the **same** values — server setup, SQL Server and the router ports:
  [Astory Server → Prerequisites](https://github.com/sitxovski/AstoryServer/blob/main/Prerequisites/README.md).
* Rebuild the client after changing the domain.
