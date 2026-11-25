# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# eac_package.py — ★ ЗАЩИЩЁННАЯ EAC-СБОРКА КЛИЕНТА (Shipping) ОДНОЙ КОМАНДОЙ (Э3, 16.10.2026, agent-eac-package).
#
# Что делает (по порядку; любой провал — отказ с кодом != 0, архив не считается готовым):
#   1. Проверка, что редактор/кук/UBT не запущены (tasklist) — иначе отказ (не убиваем: правило проекта).
#   2. Tools/gen_build_crypto.py — новый набор AES-256 + RSA подписи pak/IoStore. ★ 16.10.2026 решение владельца: ключи
#      СТАТИЧЕСКИЕ, меняются только при смене major-версии (флаг --rotate-keys) — иначе каждый патч перешифровывает
#      весь ucas и дельта-обновление лаунчера качает 5,4 ГБ (Reports/updater/00_OPTIONS.md).
#   3. RunUAT BuildCookRun Shipping -nodebuginfo — команда из gen_build_crypto.UAT_CMD (`--print-uat`), архив в
#      @@ASTORY_DEV_ROOT@@\Build\EAC\<ГГГГ-ММ-ДД>[_ЧЧММ]; корень игры = <архив>\Windows.
#   4. DLL EOS: AsdaReborn\Binaries\Win64\AsdaEOS\EOSSDK-Win64-Shipping.dll (ред. 3 клиента, E1-17) — есть, md5 = копия
#      SDK, и exe содержит имя подпапки (модуль грузит DLL полным путём <папка exe>\AsdaEOS\…).
#   5. dist тулкита EOS_AntiCheatTools-win32-x64-2.3.0.zip: dist\start_protected_game.exe → корень игры под именем
#      Astory.exe (★ 16.10.2026 решение владельца; бутстрапер ищет EasyAntiCheat\ и Settings.json от СВОЕЙ папки, своё имя
#      нигде не использует — разбор в Reports/anticheat/eac/BRANDING_CONSOLE_REPORT.md §1), dist\EasyAntiCheat\* →
#      EasyAntiCheat\; EasyAntiCheat\BootstrapperIcon.ico — иконка игры (Build/Windows/Application.ico);
#      ★ 17.10.2026 EasyAntiCheat\SplashScreen.png — наша заставка (Build/EAC/SplashScreen.png, Tools/make_eac_splash.py); Settings.json —
#      идентификаторы продукта (CONTRACT.md §0), executable, title; прочие ключи — как в тулките.
#   6. install_eac.bat / uninstall_eac.bat / EULA.txt в корень игры. ★ 16.10.2026 (решение владельца, система обновлений):
#      README_ALPHA.txt больше не кладётся; Manifest_*Files_Win64.txt (список всех ассетов) переносится из корня игры
#      в служебную <архив>\_meta\ — игроку не раздаётся, режим auto читает его оттуда (ufs_manifest_path).
#   7. Каталог целостности: devtools\anticheat_integritytool.exe + .cfg во ВРЕМЕННУЮ папку вне архива, рядом ключи
#      base_private.key/base_public.cer из каталога SDK; в cfg исключены .pak/.ucas/.utoc/.sig (их защищает подпись UE —
#      рекомендация Epic, eac_docs_research.md §4); итог — <корень игры>\EasyAntiCheat\Certificates; временная папка
#      удаляется в любом случае (finally).
#   8. Проверка утечек: в архиве нет *.key, game.exe.eac / *.eac, *.pdb, anticheat_integritytool*, DefaultCrypto.ini,
#      ключа AES (сырые байты и base64) и ClientSecret в открытом виде в незашифрованных файлах; gen_build_crypto --check.
#   9. Проверка дев-ключей в exe (ASCII и UTF-16LE): asda.Eac.Probe, asda.SelfTest.Eac и др. + ★ 16.10.2026: все имена
#      дев-команд и ASDA_DEV_CVAR из Source/ (с границей имени), метка ритейл-защиты AsdaRetailGuard/v1, иконка игры в
#      ресурсах exe (RT_GROUP_ICON = Build/Windows/Application.ico), самопроверка очистки командной строки запуском
#      Shipping-exe с -AsdaRetailGuardSelfCheck (выход до движка: без окна, сети и EAC) — Tools/asda_retail_guard_check.py.
#  10. md5 главных файлов → <архив>\..\<имя>.md5.txt; zip рядом с архивом (<имя>.zip).
#
# ★ РЕЖИМЫ (16.10.2026, решение владельца — скрипт сам определяет, что изменилось с прошлой сборки). По умолчанию — auto:
#   манифест входов прошлой сборки (BUILD_EAC\<сборка>\build_manifest.json, указатель BUILD_EAC\latest.json) сверяется
#   с текущими файлами (категории и правила — Tools/eac_build_manifest.py), выбирается самый лёгкий годный режим:
#   none     — ничего не изменилось: «сборка не нужна» (--force — пересобрать быстрым режимом);
#   code     — менялись только code (Source/**, *.Build.cs, *.Target.cs, ThirdParty, Plugins/*/Source) и/или nonufs
#              (тулкит EAC, DLL EOS из SDK, сам скрипт): UBT Build.bat AsdaReborn Win64 Shipping → robocopy корня базы в
#              новую папку (pak/ucas/utoc без изменений) → подмена exe и (если другая) DLL EOS → dist/Settings.json/bat/
#              EULA → integrity заново → утечки → md5. Допустим, ТОЛЬКО если ключи pak те же, что у базы (exe базы
#              содержит доли ключа текущего набора) — иначе новый exe не откроет старый pak. ≈ 2–4 мин;
#   skipcook — менялся config_pak (Config/*.ini, кроме DefaultCrypto.ini — он лежит В PAK; UFS-файлы проекта) без
#              content: UAT -build -skipcook -stage -pak с прежними ключами; кук берётся из Saved/Cooked (обязан быть
#              куком базы — иначе ступень выше);
#   iterate  — менялся content (Content/**, Plugins/*/Content, .uproject/.uplugin): UAT -build -cook -iterate
#              (инкрементальный кук) -stage -pak с прежними ключами; правки ассетов — сюда;
#   full     — изменился meta (версия движка, набор ключей, PACKAGE_FORMAT, [/Script/UnrealEd.ProjectPackagingSettings],
#              команда UAT) или кук не от базы для iterate: полный кук прежними ключами; --rotate-keys — НОВЫЙ набор ключей
#              (gen_build_crypto) + полный кук — ТОЛЬКО при смене major-версии (решение владельца 16.10.2026). ≈ 23 мин.
#   Нет базы / манифеста / ретро-доказательства → full. Ретро-манифест для старой базы без манифеста строится, если
#   доказуем: файлы с mtime ≤ начала её UAT (по <база>.logs\eac_package.log) = её состояние, позднее — «изменены».
#   Страховки: редактор закрыт; база с текущими ключами для code/skipcook/iterate; после любого режима — integrity,
#   утечки, дев-строки, доли ключа в новом exe; время каждого шага в конце журнала (BUILD_EAC\_auto.logs\).
#
# ★ 17.10.2026 КОНФИГУРАЦИИ (решение владельца, батники AstoryClient\DEPLOY): --config Shipping (релиз, умолчание; архивы
#   Build\EAC) или --config Development (дев-сборка С EAC: консоль и дев-команды, EAC/шифрование/подпись — как у релиза;
#   архивы Build\Dev со своей цепочкой баз; запуск — Start_Dev.bat = Astory.exe -AsdaEac, т.к. вне Shipping EAC
#   включается только флагом; шаг 9 проверяет лишь метку AsdaEacBind/v1). --skip-build/--verify-only: --out — папка архива
#   или сам корень игры, конфигурация — по exe. Пути к папкам сборок и SDK — от расположения скрипта, движок — ASDA_UE_DIR.
#
# ЗАПУСК (обычный python; закон tools-no-import-side-effects — при импорте ничего не делает):
#   python Tools/eac_package.py                      — auto от latest.json
#   python Tools/eac_package.py --base @@ASTORY_DEV_ROOT@@\Build\EAC\2026-10-16\Windows   — auto от указанной базы
#   python Tools/eac_package.py --dry-run            — только «что изменилось и какой режим»
#   python Tools/eac_package.py --code-only --base … — принудительно быстрый режим (= --mode code)
#   python Tools/eac_package.py --mode iterate|skipcook|full   — принудительно; --rotate-keys — новые ключи (только major)
#   python Tools/eac_package.py --zip                — zip (в full — по умолчанию, --no-zip отключает)
#   python Tools/eac_package.py --out D:\…\BUILD_EAC\2026-10-16 --skip-build
#                                                    — [старый путь] только шаги 4–10 над готовым архивом UAT
#   python Tools/eac_package.py --verify-only --out <архив>  — [старый путь] только проверки 4, 8, 9 и md5
#   python Tools/eac_build_manifest.py --selftest    — самотест выбора режима (сценарии none/code/skipcook/iterate/full)
# Пример вывода auto (правка C++ после базы):
#   РЕШЕНИЕ: code
#     причина: code: 6 изм. (… неизвестно с базы 6) → не ниже «code»
#     причина: nonufs: 1 изм. … → не ниже «code»
# Ключи и секреты скрипт НЕ печатает (только имена файлов и признаки «есть/нет»).
import argparse
import datetime
import fnmatch
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))                     # <проект>\Tools
PROJECT_DIR = os.path.dirname(HERE)                                    # <проект> (клон AstoryClient)
PORT_DIR = os.path.dirname(PROJECT_DIR)                                # <ROOT> (★ 17.10.2026: пути — от расположения скрипта)
# ★ 17.10.2026 (решение владельца, AstoryClient\DEPLOY): две конфигурации защищённой сборки — Shipping (релиз) и Development с EAC
#   (дев-сборка: консоль и дев-команды есть, EAC/шифрование/подпись — как у релиза). У каждой своя папка, своя цепочка баз
#   (latest.json) и свой журнал. Выбор — --config; set_config() переключает все зависящие константы.
BUILD_ROOT_BY_CONFIG = {"Shipping": os.path.join(PORT_DIR, "Build", "EAC"),
                        "Development": os.path.join(PORT_DIR, "Build", "Dev")}
BUILD_ROOT = BUILD_ROOT_BY_CONFIG["Shipping"]                          # корень архивов сборок текущей конфигурации
SDK_DIR = os.path.join(PORT_DIR, "ThirdParty", "EOS-SDK-58105819-Release-v1.19.2.1")  # EOS SDK 1.19.2.1 (ключи integrity — в корне)
UE_DIR_DEFAULT = r"D:\Games\UE_5.8"                                    # движок по умолчанию (как в Tools\asda_ue_engine.bat)
CONFIG = "Shipping"                                                    # текущая конфигурация (set_config)
TOOLKIT_ZIP = os.path.join(SDK_DIR, "SDK", "Tools", "EOS_AntiCheatTools-win32-x64-2.3.0.zip")  # тулкит EAC 2.3.0
SDK_EOS_DLL = os.path.join(SDK_DIR, "SDK", "Bin", "EOSSDK-Win64-Shipping.dll")                  # эталон DLL для md5
INTEGRITY_KEYS = ("base_private.key", "base_public.cer")              # ключи integrity — только во временную папку

# Идентификаторы продукта (несекретные, CONTRACT.md §0)
PRODUCT_ID = "@@ASTORY_EOS_PRODUCT_ID@@"
SANDBOX_ID = "@@ASTORY_EOS_SANDBOX_ID@@"
DEPLOYMENT_ID = "@@ASTORY_EOS_DEPLOYMENT_ID@@"
GAME_TITLE = "AStory Remake"                                           # заголовок окна bootstrapper
BOOTSTRAPPER_NAME = "Astory.exe"                                       # ★ 16.10.2026: имя запуска игры (бутстрапер EAC)
TOOLKIT_BOOTSTRAPPER = "start_protected_game.exe"                      # имя бутстрапера в dist тулкита
BOOTSTRAPPER_ICON_REL = r"EasyAntiCheat\BootstrapperIcon.ico"          # иконка окна бутстрапера (eac_docs_research §3)

GAME_EXE_BY_CONFIG = {"Shipping": r"AsdaReborn\Binaries\Win64\AsdaReborn-Win64-Shipping.exe",
                      "Development": r"AsdaReborn\Binaries\Win64\AsdaReborn.exe"}      # настоящий exe игры в архиве
PROJECT_EXE_BY_CONFIG = {"Shipping": "AsdaReborn-Win64-Shipping.exe", "Development": "AsdaReborn.exe"}  # exe UBT в Binaries
GAME_EXE_REL = GAME_EXE_BY_CONFIG["Shipping"]                          # настоящий exe игры (executable в Settings.json)
EOS_DLL_REL = r"AsdaReborn\Binaries\Win64\AsdaEOS\EOSSDK-Win64-Shipping.dll"  # DLL EOS в подпапке (E1-17)
EOS_DLL_SUBDIR = "AsdaEOS"                                             # имя подпапки = ASDA_EOSSDK_DLL_SUBDIR
# ★ 17.10.2026: Development с EAC. Вне Shipping игра включает EAC только с флагом -AsdaEac (AsdaEacFlow::ComputeEnabled);
#   полей параметров у Settings.json бутстрапера нет, а свою командную строку он передаёт игре (eac_docs_research.md §3) —
#   поэтому в корень дев-сборки кладётся Start_Dev.bat: Astory.exe -AsdaEac.
DEV_LAUNCH_BAT = "Start_Dev.bat"
ZIP_PREFIX_BY_CONFIG = {"Shipping": "AStoryRemake_EAC_", "Development": "AStoryRemake_DevEAC_"}

# Процессы, при которых упаковку запускать нельзя (редактор/кук держат DLL и Definitions; UBT/линкер — параллельная сборка)
BLOCKING_PROCESSES = ("UnrealEditor.exe", "UnrealEditor-Cmd.exe", "UnrealBuildTool.exe", "LiveCodingConsole.exe",
                      "AsdaReborn-Win64-Shipping.exe")


def set_config(cfg):
    """Переключить конфигурацию сборки (Shipping | Development): папка архивов, exe игры, блокирующие процессы."""
    global CONFIG, BUILD_ROOT, GAME_EXE_REL, BLOCKING_PROCESSES
    if cfg not in BUILD_ROOT_BY_CONFIG:
        raise ValueError("конфигурация %r: ожидается Shipping или Development" % cfg)
    CONFIG = cfg
    BUILD_ROOT = BUILD_ROOT_BY_CONFIG[cfg]
    GAME_EXE_REL = GAME_EXE_BY_CONFIG[cfg]
    BLOCKING_PROCESSES = ("UnrealEditor.exe", "UnrealEditor-Cmd.exe", "UnrealBuildTool.exe", "LiveCodingConsole.exe",
                          PROJECT_EXE_BY_CONFIG[cfg])


def ue_dir():
    """Корень движка: переменная ASDA_UE_DIR (соглашение Tools\\asda_ue_engine.bat) или D:\\Games\\UE_5.8."""
    return os.environ.get("ASDA_UE_DIR") or UE_DIR_DEFAULT


def localize_engine(cmd):
    """Команда с путём движка по умолчанию → путь этой машины (ASDA_UE_DIR). Шаблон gen_build_crypto.UAT_CMD не меняется —
    иначе изменится uat_cmd_sha1 в meta и auto сочтёт сборку «full»."""
    return cmd.replace(UE_DIR_DEFAULT, ue_dir()) if ue_dir() != UE_DIR_DEFAULT else cmd


def detect_config(game_root):
    """Конфигурация готового корня игры по exe: Shipping | Development | None."""
    for cfg in ("Shipping", "Development"):
        if os.path.isfile(os.path.join(game_root, GAME_EXE_BY_CONFIG[cfg])):
            return cfg
    return None

# Расширения, которые integrity tool НЕ хеширует сверх списка тулкита: pak/IoStore защищены подписью UE (B2)
EXTRA_EXCLUDED_EXT = (".pak", ".ucas", ".utoc", ".sig")

# Строки, которых не должно быть в Shipping-exe (дев-команды и отладочные пути; CLIENT_REPORT.md Р3.7)
DEV_STRINGS = ("asda.Eac.Probe", "asda.SelfTest.Eac", "asda.Eac.Status", "asda.Eac.AllowHttp",
               "dummysecret", "selftest.invalid")
# ★ 16.10: имя EOS_USE_ANTICHEATCLIENTNULL в Shipping-exe теперь ОЖИДАЕМО — по нему Shipping запрещает пустой клиент EAC
#   (AsdaAntiCheatSubsystem.cpp, AsdaEacNullClientAllowed); из списка запрещённых строк убрано.

# Шаблоны имён, которых не должно быть в архиве (секреты и инструменты разработчика)
LEAK_PATTERNS = ("*.key", "base_private*", "*.eac", "*.pdb", "anticheat_integritytool*", "DefaultCrypto.ini",
                 "base_public.cer", "*.pem", "eos.json")

# Расширения, которые в zip кладутся без сжатия (уже сжаты/зашифрованы)
STORE_EXT = (".pak", ".ucas", ".utoc", ".sig", ".png", ".bik", ".mp4")


# ── журнал ─────────────────────────────────────────────────────────────────────────────────────────────────────
class Log:
    """Печать в консоль и в файл журнала упаковки (UTF-8)."""

    def __init__(self, path=None):
        self.path = path      # путь журнала (None — только консоль)
        self.f = None         # открытый файл журнала

    def open(self, path):
        """Открыть файл журнала (дописывание)."""
        os.makedirs(os.path.dirname(path), exist_ok=True)
        self.path = path
        self.f = open(path, "a", encoding="utf-8")

    def __call__(self, msg=""):
        line = "[%s] %s" % (datetime.datetime.now().strftime("%H:%M:%S"), msg)
        print(line, flush=True)
        if self.f:
            self.f.write(line + "\n")
            self.f.flush()


LOG = Log()


class PackageError(Exception):
    """Провал шага упаковки — сообщение для журнала, выход с кодом 1."""


# ── утилиты ────────────────────────────────────────────────────────────────────────────────────────────────────
def md5_file(path, chunk=16 * 1024 * 1024):
    """md5 файла кусками."""
    h = hashlib.md5()
    with open(path, "rb") as f:
        while True:
            b = f.read(chunk)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def file_contains_any(path, needles, chunk=16 * 1024 * 1024):
    """Множество needles (bytes), найденных в файле (поиск кусками с перекрытием)."""
    found = set()
    if not needles:
        return found
    over = max(len(n) for n in needles) - 1
    tail = b""
    with open(path, "rb") as f:
        while True:
            b = f.read(chunk)
            if not b:
                break
            buf = tail + b
            for n in needles:
                if n not in found and n in buf:
                    found.add(n)
            tail = buf[-over:] if over > 0 else b""
    return found


def walk_files(root):
    """Все файлы под root: (абсолютный путь, относительный путь с '\\')."""
    for d, _, files in os.walk(root):
        for fn in files:
            p = os.path.join(d, fn)
            yield p, os.path.relpath(p, root)


def running_blockers():
    """Имена запущенных процессов из BLOCKING_PROCESSES (по tasklist /FO CSV)."""
    out = subprocess.run(["tasklist", "/FO", "CSV", "/NH"], capture_output=True, text=True,
                         encoding="cp866", errors="replace").stdout
    names = set()
    for line in out.splitlines():
        m = re.match(r'"([^"]+)"', line)
        if m:
            names.add(m.group(1).lower())
    return [p for p in BLOCKING_PROCESSES if p.lower() in names]


def load_crypto_module():
    """Импорт gen_build_crypto (без сайд-эффектов при импорте)."""
    sys.path.insert(0, HERE)
    import gen_build_crypto
    return gen_build_crypto


def load_icon_module():
    """Импорт make_app_icon (Application.ico из Data/OwnerArt/icon.png; без сайд-эффектов при импорте)."""
    sys.path.insert(0, HERE)
    import make_app_icon
    return make_app_icon


def load_splash_module():
    """★ 17.10.2026 Импорт make_eac_splash (заставка окна EAC Build/EAC/SplashScreen.png; без сайд-эффектов при импорте)."""
    sys.path.insert(0, HERE)
    import make_eac_splash
    return make_eac_splash


def load_guard_module():
    """Импорт asda_retail_guard_check (проверки ритейл-защиты Shipping-exe; без сайд-эффектов при импорте)."""
    sys.path.insert(0, HERE)
    import asda_retail_guard_check
    return asda_retail_guard_check


def read_client_secret():
    """ClientSecret из Config/DefaultGame.ini (для поиска утечки в открытом виде; значение не печатается)."""
    ini = os.path.join(PROJECT_DIR, "Config", "DefaultGame.ini")
    section = None
    with open(ini, "r", encoding="utf-8-sig", errors="replace") as f:
        for raw in f:
            line = raw.strip()
            if line.startswith("[") and line.endswith("]"):
                section = line[1:-1]
            elif section == "/Script/AsdaAntiCheat.AsdaAntiCheatSettings" and line.startswith("ClientSecret="):
                return line.split("=", 1)[1].strip()
    return ""


# ── шаги ───────────────────────────────────────────────────────────────────────────────────────────────────────
def step_check_editor():
    """Шаг 1: редактор/кук/UBT не запущены."""
    busy = running_blockers()
    if busy:
        raise PackageError("запущены %s — закройте их (скрипт процессы не завершает)" % ", ".join(busy))
    LOG("шаг 1: редактор, кук и UBT не запущены — ОК")


def step_gen_crypto(gbc):
    """Шаг 2: новый набор ключей pak/IoStore (бэкап прошлого — внутри gen_build_crypto)."""
    LOG("шаг 2: gen_build_crypto — новый набор ключей сборки")
    r = subprocess.run([sys.executable, os.path.join(HERE, "gen_build_crypto.py")], capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    for line in (r.stdout + r.stderr).splitlines():
        LOG("  " + line)
    if r.returncode != 0:
        raise PackageError("gen_build_crypto завершился с кодом %d" % r.returncode)
    ini = gbc.read_ini()
    LOG("шаг 2: набор ключей %s — ОК" % (ini or {}).get("build_id"))
    return (ini or {}).get("build_id")


def step_uat(gbc, out_dir, log_path):
    """Шаг 3: RunUAT BuildCookRun <CONFIG> -nodebuginfo, архив в out_dir. Успех: код 0 и «BUILD SUCCESSFUL»."""
    ps_cmd = localize_engine(gbc.UAT_CMD.format(cfg=CONFIG, out=out_dir))
    if "-nodebuginfo" not in ps_cmd:
        raise PackageError("в команде UAT нет -nodebuginfo — pdb попадёт в архив")
    cmd = ps_cmd[2:] if ps_cmd.startswith("& ") else ps_cmd           # PowerShell-вызов «& "…bat"» → строка для cmd
    LOG("шаг 3: UAT: %s" % cmd)
    LOG("  журнал UAT: %s" % log_path)
    t0 = datetime.datetime.now()
    ok_marker = False
    with open(log_path, "w", encoding="utf-8", errors="replace") as lf:
        p = subprocess.Popen('cmd /d /s /c "%s"' % cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             stdin=subprocess.DEVNULL, creationflags=0x08000000,  # CREATE_NO_WINDOW
                             cwd=PROJECT_DIR)
        for raw in p.stdout:
            line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            lf.write(line + "\n")
            if "BUILD SUCCESSFUL" in line:
                ok_marker = True
            if ("BUILD FAILED" in line or "AutomationTool exiting with ExitCode" in line or
                    re.search(r"\berror\b.*:", line) and "Warning" not in line and len(line) < 400):
                LOG("  UAT| " + line)
        rc = p.wait()
    mins = (datetime.datetime.now() - t0).total_seconds() / 60.0
    if rc != 0 or not ok_marker:
        raise PackageError("UAT: код %d, маркер BUILD SUCCESSFUL %s (%.1f мин) — см. %s" %
                           (rc, "есть" if ok_marker else "НЕТ", mins, log_path))
    LOG("шаг 3: UAT BUILD SUCCESSFUL за %.1f мин — ОК" % mins)


def step_check_eos_dll(game_root):
    """Шаг 4: DLL EOS в подпапке AsdaEOS, совпадает с SDK; exe знает имя подпапки (путь собирается из констант)."""
    dll = os.path.join(game_root, EOS_DLL_REL)
    exe = os.path.join(game_root, GAME_EXE_REL)
    if not os.path.isfile(exe):
        raise PackageError("нет exe игры: %s" % exe)
    if not os.path.isfile(dll):
        raise PackageError("нет DLL EOS: %s" % dll)
    if md5_file(dll) != md5_file(SDK_EOS_DLL):
        raise PackageError("DLL EOS в архиве не совпадает с SDK 1.19.2.1")
    stray = os.path.join(os.path.dirname(exe), "EOSSDK-Win64-Shipping.dll")
    if os.path.isfile(stray):
        raise PackageError("лишняя копия DLL рядом с exe (ожидается только в подпапке %s): %s" % (EOS_DLL_SUBDIR, stray))
    # Модуль собирает путь FPaths::Combine(<папка бинарника>, ASDA_EOSSDK_DLL_SUBDIR, ASDA_EOSSDK_DLL_NAME) —
    # обе строки — TEXT(), т.е. UTF-16LE в exe. Папка бинарника монолитной игры = папка exe.
    need = {"AsdaEOS".encode("utf-16-le"), "EOSSDK-Win64-Shipping.dll".encode("utf-16-le")}
    found = file_contains_any(exe, need)
    if found != need:
        raise PackageError("в exe нет строк пути DLL (UTF-16): %s" % [n.decode("utf-16-le") for n in need - found])
    LOG("шаг 4: %s есть, md5 = SDK, exe содержит имя подпапки и DLL — ОК" % EOS_DLL_REL)


def step_dist(game_root):
    """Шаг 5: раскладка dist тулкита и Settings.json."""
    with zipfile.ZipFile(TOOLKIT_ZIP) as z:
        names = z.namelist()
        n = 0
        for name in names:
            if name.endswith("/") or not name.startswith("dist/"):
                continue
            rel = name[len("dist/"):].replace("/", os.sep)
            if rel == TOOLKIT_BOOTSTRAPPER:
                rel = BOOTSTRAPPER_NAME                                # ★ 16.10.2026: бутстрапер EAC кладётся как Astory.exe
            dst = os.path.join(game_root, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with z.open(name) as src, open(dst, "wb") as f:
                shutil.copyfileobj(src, f)
            n += 1
        template = json.loads(z.read("dist/EasyAntiCheat/Settings.json").decode("utf-8"))
    # Бутстрапер под именем тулкита в корне не остаётся (быстрый режим копирует корень прошлой сборки).
    stale = os.path.join(game_root, TOOLKIT_BOOTSTRAPPER)
    if os.path.isfile(stale):
        os.remove(stale)
        LOG("шаг 5: %s из прошлой раскладки удалён (запуск — %s)" % (TOOLKIT_BOOTSTRAPPER, BOOTSTRAPPER_NAME))
    if not os.path.isfile(os.path.join(game_root, BOOTSTRAPPER_NAME)):
        raise PackageError("нет %s в корне игры после раскладки dist" % BOOTSTRAPPER_NAME)
    # Иконка окна бутстрапера: EasyAntiCheat/BootstrapperIcon.ico (иначе он берёт иконку из exe игры — та же, но явно надёжнее).
    ico = load_icon_module().ensure_icon(log=LOG)
    shutil.copyfile(ico, os.path.join(game_root, BOOTSTRAPPER_ICON_REL))
    settings = dict(template)                                          # порядок ключей тулкита сохраняется
    settings["title"] = GAME_TITLE
    settings["executable"] = GAME_EXE_REL
    settings["productid"] = PRODUCT_ID
    settings["sandboxid"] = SANDBOX_ID
    settings["deploymentid"] = DEPLOYMENT_ID
    # ★ 17.10.2026 Заставка окна EAC — наша (город игры + значок AStory Remake) вместо стоковой из тулкита; путь — тот, что
    #   бутстрапер читает из `requested_splash`. Кладётся до каталога целостности (шаг 7) — попадает в него как обычный файл.
    splash_rel = settings.get("requested_splash") or "EasyAntiCheat/SplashScreen.png"
    shutil.copyfile(load_splash_module().ensure_splash(log=LOG), os.path.join(game_root, splash_rel.replace("/", os.sep)))
    if not os.path.isfile(os.path.join(game_root, GAME_EXE_REL)):
        raise PackageError("executable из Settings.json не найден в архиве: %s" % GAME_EXE_REL)
    sp = os.path.join(game_root, "EasyAntiCheat", "Settings.json")
    with open(sp, "w", encoding="utf-8", newline="\r\n") as f:
        json.dump(settings, f, indent="\t", ensure_ascii=False)
        f.write("\n")
    json.load(open(sp, "r", encoding="utf-8"))                          # самопроверка: валидный JSON
    LOG("шаг 5: dist тулкита — %d файлов (бутстрапер — %s, иконка — %s); Settings.json записан (executable=%s, splash=%s) — ОК" %
        (n, BOOTSTRAPPER_NAME, BOOTSTRAPPER_ICON_REL, GAME_EXE_REL, settings.get("requested_splash")))


INSTALL_BAT = r"""@echo off
rem install_eac.bat — установка службы Easy Anti-Cheat для AStory Remake (нужны права администратора, один раз).
chcp 65001 >nul
setlocal
net session >nul 2>&1
if errorlevel 1 goto :elevate
cd /d "%~dp0"
if not exist "%~dp0EasyAntiCheat\EasyAntiCheat_EOS_Setup.exe" goto :nosetup
echo Устанавливается служба Easy Anti-Cheat...
"%~dp0EasyAntiCheat\EasyAntiCheat_EOS_Setup.exe" install {pid}
set RC=%ERRORLEVEL%
if not "%RC%"=="0" goto :fail
echo Готово: служба Easy Anti-Cheat установлена. Запускайте игру через Astory.exe
pause
exit /b 0
:fail
echo ОШИБКА: установка службы Easy Anti-Cheat не удалась, код %RC%.
echo Журнал установщика: %%AppData%%\EasyAntiCheat
pause
exit /b %RC%
:nosetup
echo ОШИБКА: не найден EasyAntiCheat\EasyAntiCheat_EOS_Setup.exe рядом с этим файлом.
pause
exit /b 2
:elevate
echo Нужны права администратора, запрашиваю...
powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
exit /b 0
"""

UNINSTALL_BAT = INSTALL_BAT.replace("install_eac.bat — установка", "uninstall_eac.bat — удаление") \
    .replace("Устанавливается служба", "Удаляется служба") \
    .replace("EasyAntiCheat_EOS_Setup.exe\" install {pid}", "EasyAntiCheat_EOS_Setup.exe\" uninstall {pid}") \
    .replace("Готово: служба Easy Anti-Cheat установлена. Запускайте игру через Astory.exe",
             "Готово: служба Easy Anti-Cheat удалена.") \
    .replace("установка службы Easy Anti-Cheat не удалась", "удаление службы Easy Anti-Cheat не удалось") \
    .replace(", один раз)", ")")




def step_extras(game_root, build_label):
    """Шаг 6: install_eac.bat, uninstall_eac.bat, EULA.txt в корень игры; манифесты стейджа — в <архив>/_meta."""
    # ★ 16.10.2026 (решение владельца: без античита не запускается): загрузчик UE AsdaReborn.exe в корне запускает игру
    #   мимо Astory.exe (бутстрапера EAC) — убираем его из сборки (сама игра в Shipping без EAC тоже закрывается).
    stub = os.path.join(game_root, "AsdaReborn.exe")
    if os.path.isfile(stub):
        os.remove(stub)
        LOG("шаг 6: загрузчик UE AsdaReborn.exe удалён из сборки (запуск только через %s)" % BOOTSTRAPPER_NAME)
    # ★ 16.10.2026 (решение владельца): списки файлов стейджа светят структуру ассетов — из раздачи вон, в _meta рядом
    #   с корнем игры (нужны режиму auto как «состояние базы»). README_ALPHA.txt больше не раздаётся.
    meta = os.path.join(os.path.dirname(game_root), "_meta")
    for fn in ("Manifest_UFSFiles_Win64.txt", "Manifest_NonUFSFiles_Win64.txt"):
        src = os.path.join(game_root, fn)
        if os.path.isfile(src):
            os.makedirs(meta, exist_ok=True)
            shutil.move(src, os.path.join(meta, fn))
            LOG("шаг 6: %s перенесён в %s (не раздаётся)" % (fn, meta))
    old_readme = os.path.join(game_root, "README_ALPHA.txt")
    if os.path.isfile(old_readme):
        os.remove(old_readme)
    def w(name, text, bom=False):
        with open(os.path.join(game_root, name), "w", encoding="utf-8-sig" if bom else "utf-8", newline="\r\n") as f:
            f.write(text)
    w("install_eac.bat", INSTALL_BAT.replace("{pid}", PRODUCT_ID))
    w("uninstall_eac.bat", UNINSTALL_BAT.replace("{pid}", PRODUCT_ID))
    if CONFIG == "Development":
        # ★ 17.10.2026: дев-сборка с EAC — запуск через бутстрапер с -AsdaEac (он передаёт свою командную строку игре).
        w(DEV_LAUNCH_BAT, "@echo off\nrem Development-сборка с EAC: запуск через бутстрапер Astory.exe с флагом -AsdaEac\n"
                          "rem (вне Shipping игра включает EAC только с этим флагом). Доп. аргументы передаются дальше.\n"
                          "cd /d \"%~dp0\"\nstart \"\" \"%~dp0" + BOOTSTRAPPER_NAME + "\" -AsdaEac %*\n")
        LOG("шаг 6: %s (Astory.exe -AsdaEac) — ОК" % DEV_LAUNCH_BAT)
    # ★ 16.10.2026: EULA (англ.) — требование EOS Developer Agreement п. 3.1(c) (отказ от гарантий на материалы Epic).
    eula_src = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "legal", "EULA_EN.txt")
    if not os.path.isfile(eula_src):
        raise SystemExit("ОТКАЗ: нет %s — EULA обязательна в сборке" % eula_src)
    shutil.copyfile(eula_src, os.path.join(game_root, "EULA.txt"))
    LOG("шаг 6: install_eac.bat, uninstall_eac.bat, EULA.txt — ОК")


def patch_integrity_cfg(text):
    """cfg тулкита → добавить EXTRA_EXCLUDED_EXT в exclude_extensions (остальное как в тулките)."""
    m = re.search(r"(exclude_extensions\s*=\s*\[)(.*?)(\];)", text, re.S)
    if not m:
        raise PackageError("в anticheat_integritytool.cfg нет exclude_extensions")
    body = m.group(2)
    present = set(re.findall(r'"([^"]+)"', body))
    add = [e for e in EXTRA_EXCLUDED_EXT if e not in present]
    if add:
        body = body.rstrip() + ",\n" + ",\n".join('        "%s"' % e for e in add) + "\n    "
    return text[:m.start(2)] + body + text[m.end(2):]


def step_integrity(game_root):
    """Шаг 7: каталог целостности во временной папке с ключами; папка удаляется в любом случае."""
    for k in INTEGRITY_KEYS:
        if not os.path.isfile(os.path.join(SDK_DIR, k)):
            raise PackageError("нет ключа integrity %s в %s — скачайте ключи integrity (base_private.key, base_public.cer) "
                               "в Epic Developer Portal → ваш продукт → Anti-Cheat и положите в корень папки SDK "
                               "(рядом с SDK и Samples); в git не класть — см. Prerequisites\\README.md" % (k, SDK_DIR))
    cert_dir = os.path.join(game_root, "EasyAntiCheat", "Certificates")
    if os.path.isdir(cert_dir):
        shutil.rmtree(cert_dir)                                        # каталог прошлого прогона — пересоздаётся
    tmp = tempfile.mkdtemp(prefix="asda_eac_integrity_")
    try:
        with zipfile.ZipFile(TOOLKIT_ZIP) as z:
            for name in ("devtools/anticheat_integritytool.exe", "devtools/anticheat_integritytool.cfg"):
                with z.open(name) as src, open(os.path.join(tmp, os.path.basename(name)), "wb") as f:
                    shutil.copyfileobj(src, f)
        cfg = os.path.join(tmp, "anticheat_integritytool.cfg")
        txt = open(cfg, "r", encoding="utf-8").read()
        with open(cfg, "w", encoding="utf-8", newline="\n") as f:
            f.write(patch_integrity_cfg(txt))
        for k in INTEGRITY_KEYS:
            shutil.copy2(os.path.join(SDK_DIR, k), os.path.join(tmp, k))
        tool = os.path.join(tmp, "anticheat_integritytool.exe")
        args = [tool, "-target_game_dir", game_root, "-productid", PRODUCT_ID]
        LOG("шаг 7: integrity tool (временная папка вне архива): -target_game_dir %s -productid %s" %
            (game_root, PRODUCT_ID))
        r = subprocess.run(args, cwd=tmp, capture_output=True, text=True, encoding="utf-8", errors="replace",
                           creationflags=0x08000000)
        for line in (r.stdout + r.stderr).splitlines():
            if line.strip():
                LOG("  tool| " + line)
        if r.returncode != 0:
            raise PackageError("anticheat_integritytool: код %d" % r.returncode)
        # ★ 16.10.2026: каталог строится ПОСЛЕ переименования — Astory.exe обязан быть среди хешированных, имени тулкита нет.
        hashed = [m.group(1) for m in re.finditer(r"Hashing '([^']+)'", r.stdout + r.stderr)]
        if BOOTSTRAPPER_NAME not in hashed:
            raise PackageError("%s нет в каталоге целостности (хешировано: %s)" % (BOOTSTRAPPER_NAME, ", ".join(hashed)))
        if TOOLKIT_BOOTSTRAPPER in hashed:
            raise PackageError("в каталоге целостности остался %s" % TOOLKIT_BOOTSTRAPPER)
        LOG("шаг 7: в каталоге %d файлов, %s — есть, %s — нет" % (len(hashed), BOOTSTRAPPER_NAME, TOOLKIT_BOOTSTRAPPER))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
        if os.path.exists(tmp):
            raise PackageError("временная папка с ключами integrity не удалена: %s" % tmp)
        LOG("шаг 7: временная папка с ключами удалена")
    files = sorted(os.listdir(cert_dir)) if os.path.isdir(cert_dir) else []
    if not files:
        raise PackageError("нет %s после integrity tool" % cert_dir)
    LOG("шаг 7: EasyAntiCheat\\Certificates: %s — ОК" % ", ".join(
        "%s (%d б)" % (f, os.path.getsize(os.path.join(cert_dir, f))) for f in files))


def step_leaks(game_root, gbc):
    """Шаг 8: нет секретов и инструментов разработчика в архиве. Возвращает число нарушений."""
    errs = 0
    for p, rel in walk_files(game_root):
        base = os.path.basename(rel)
        for pat in LEAK_PATTERNS:
            if fnmatch.fnmatch(base.lower(), pat.lower()):
                LOG("  УТЕЧКА: файл %s (шаблон %s)" % (rel, pat))
                errs += 1
    # Содержимое: ключ AES (сырые байты и base64), закрытая экспонента RSA, ClientSecret, ключи integrity —
    # во всех файлах, кроме зашифрованных контейнеров pak/IoStore (в них — только шифротекст).
    ini = gbc.read_ini() or {}
    needles = {}
    if ini.get("EncryptionKey"):
        import base64
        needles[base64.b64decode(ini["EncryptionKey"])] = "ключ AES (байты)"
        needles[ini["EncryptionKey"].encode()] = "ключ AES (base64)"
    if ini.get("SigningPrivateExponent"):
        needles[ini["SigningPrivateExponent"][:64].encode()] = "закрытая экспонента RSA (base64)"
    secret = read_client_secret()
    if secret:
        needles[secret.encode()] = "ClientSecret (ASCII)"
        needles[secret.encode("utf-16-le")] = "ClientSecret (UTF-16)"
    for k in INTEGRITY_KEYS[:1]:
        kb = open(os.path.join(SDK_DIR, k), "rb").read()
        mid = kb[len(kb) // 2: len(kb) // 2 + 48]                       # фрагмент из середины (не заголовок PEM)
        if len(mid) >= 32:
            needles[mid] = "закрытый ключ integrity"
    scanned = 0
    for p, rel in walk_files(game_root):
        if os.path.splitext(rel)[1].lower() in (".pak", ".ucas", ".utoc"):
            continue
        hit = file_contains_any(p, set(needles))
        scanned += 1
        for h in hit:
            LOG("  УТЕЧКА: %s в %s" % (needles[h], rel))
            errs += 1
    LOG("  проверено содержимое %d файлов (кроме pak/ucas/utoc) на %d образцов секретов" % (scanned, len(needles)))
    # Логика gen_build_crypto --check: ini↔заголовок, ключ в exe/стейдже
    LOG("  gen_build_crypto --check:")
    r = subprocess.run([sys.executable, os.path.join(HERE, "gen_build_crypto.py"), "--check"], capture_output=True,
                       text=True, encoding="utf-8", errors="replace")
    for line in r.stdout.splitlines():
        LOG("    " + line)
    if r.returncode != 0:
        errs += 1
    # Ключ AES в exe архива (check() смотрит Binaries проекта — exe архива проверяем отдельно)
    LOG("шаг 8: проверка утечек — %s" % ("ОК" if errs == 0 else "%d НАРУШЕНИЙ" % errs))
    return errs


def step_dev_strings(game_root):
    """Шаг 9: дев-ключей нет в exe (ASCII и UTF-16LE). Возвращает число нарушений."""
    exe = os.path.join(game_root, GAME_EXE_REL)
    if CONFIG == "Development":
        # ★ 17.10.2026: в Development дев-команды и консоль ожидаемы; ритейл-защита (самопроверка очистки командной строки)
        #   есть только в Shipping. Проверяется лишь метка привязки EAC.
        bind = file_contains_any(exe, {b"AsdaEacBind/v1"})
        LOG("  метка AsdaEacBind/v1 в exe: %s" % ("есть" if bind else "НЕТ"))
        LOG("шаг 9: Development — дев-строки ожидаемы, проверка ритейл-защиты пропущена; метка привязки %s" %
            ("есть, ОК" if bind else "НЕТ"))
        return 0 if bind else 1
    # ★ 19.10.2026 (журналы релиза, часть A): имена дев-ключей asda.* проверяются как в asda_retail_guard_check (1а) — упоминание
    #   внутри формата строки журнала из Source не нарушение (Shipping пишет запечатанный журнал), отдельная строка-имя и
    #   любое другое вхождение — нарушение. Прочие строки (заглушки секретов, тестовые хосты) — по-прежнему любое вхождение.
    guard = load_guard_module()
    needles = {}
    named = [s for s in DEV_STRINGS if s.startswith("asda.")]
    for s in DEV_STRINGS:
        if s in named:
            continue
        needles[s.encode("ascii")] = s + " (ASCII)"
        needles[s.encode("utf-16-le")] = s + " (UTF-16)"
    hit = sorted(needles[h] for h in file_contains_any(exe, set(needles)))
    data = open(exe, "rb").read()
    formats = guard.log_format_literals()
    for s in named:
        bad, _ = guard.classify_name_hits(data, s, formats, bounded=False)   # как раньше: любое вхождение
        if bad:
            hit.append(s + " (вне строк журнала)")
    del data
    for h in hit:
        LOG("  ДЕВ-СТРОКА в exe: %s" % h)
    bind = file_contains_any(exe, {b"AsdaEacBind/v1"})
    LOG("  метка AsdaEacBind/v1 в exe: %s" % ("есть" if bind else "НЕТ"))
    errs = len(hit) + (0 if bind else 1)
    # ★ 16.10.2026 (ритейл-защита, BRANDING_CONSOLE_REPORT.md): все дев-команды/ASDA_DEV_CVAR из Source, метка защиты,
    #   самопроверка очистки командной строки запуском exe (скрыто, выход до движка), иконка игры в ресурсах exe.
    errs += load_guard_module().shipping_all(exe, load_icon_module().APP_ICO, log=LOG)
    LOG("шаг 9: дев-строки в exe — %s" % ("нет, ОК" if errs == 0 else "%d НАРУШЕНИЙ" % errs))
    return errs


def step_md5(game_root, out_txt):
    """Шаг 10а: md5 и размеры главных файлов → out_txt (вне архива)."""
    main = [GAME_EXE_REL, EOS_DLL_REL, BOOTSTRAPPER_NAME, BOOTSTRAPPER_ICON_REL,
            r"EasyAntiCheat\EasyAntiCheat_EOS_Setup.exe", r"EasyAntiCheat\Settings.json",
            "install_eac.bat", "uninstall_eac.bat", "EULA.txt"] + ([DEV_LAUNCH_BAT] if CONFIG == "Development" else [])
    cert = os.path.join(game_root, "EasyAntiCheat", "Certificates")
    if os.path.isdir(cert):
        main += [os.path.join("EasyAntiCheat", "Certificates", f) for f in sorted(os.listdir(cert))]
    paks = os.path.join(game_root, "AsdaReborn", "Content", "Paks")
    if os.path.isdir(paks):
        main += [os.path.join("AsdaReborn", "Content", "Paks", f) for f in sorted(os.listdir(paks))]
    lines = []
    for rel in main:
        p = os.path.join(game_root, rel)
        if not os.path.isfile(p):
            lines.append("%-32s %14s  %s" % ("НЕТ", "-", rel))
            continue
        lines.append("%s %14d  %s" % (md5_file(p), os.path.getsize(p), rel))
    total = sum(os.path.getsize(p) for p, _ in walk_files(game_root))
    count = sum(1 for _ in walk_files(game_root))
    lines.append("итого файлов: %d, байт: %d" % (count, total))
    with open(out_txt, "w", encoding="utf-8", newline="\r\n") as f:
        f.write("\n".join(lines) + "\n")
    for l in lines:
        LOG("  " + l)
    LOG("шаг 10: md5 → %s" % out_txt)


def step_zip(game_root, zip_path, top):
    """Шаг 10б: zip корня игры (верхняя папка top); pak/IoStore без сжатия."""
    tmp = zip_path + ".part"
    if os.path.exists(tmp):
        os.remove(tmp)
    with zipfile.ZipFile(tmp, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as z:
        for p, rel in walk_files(game_root):
            ct = zipfile.ZIP_STORED if os.path.splitext(rel)[1].lower() in STORE_EXT else zipfile.ZIP_DEFLATED
            z.write(p, top + "/" + rel.replace(os.sep, "/"), compress_type=ct)
    with zipfile.ZipFile(tmp) as z:
        bad = z.testzip() if os.path.getsize(tmp) < 512 * 1024 * 1024 else None   # полный CRC-тест — только для малых
        n = len(z.namelist())
    if bad:
        raise PackageError("zip повреждён: %s" % bad)
    os.replace(tmp, zip_path)
    LOG("шаг 10: zip %s — %d файлов, %d б — ОК" % (zip_path, n, os.path.getsize(zip_path)))


# ── auto: база, ключи, кук, режимы (16.10.2026, agent-eac-diaglog) ──────────────────────────────────────────────
class StepTimer:
    """Время шагов для итоговой таблицы."""

    def __init__(self):
        self.rows = []        # [(имя шага, секунды)]
        self.t0 = None        # начало текущего шага
        self.name = None      # имя текущего шага

    def start(self, name):
        """Закрыть прошлый шаг и начать новый."""
        self.stop()
        self.name, self.t0 = name, datetime.datetime.now()

    def stop(self):
        """Закрыть текущий шаг."""
        if self.name:
            self.rows.append((self.name, (datetime.datetime.now() - self.t0).total_seconds()))
            self.name = None

    def report(self):
        """Строки итоговой таблицы времени."""
        self.stop()
        total = sum(sec for _, sec in self.rows)
        return ["  %-44s %8.1f с" % (n, sec) for n, sec in self.rows] + ["  %-44s %8.1f с (%.1f мин)" % ("ИТОГО", total, total / 60)]


def load_manifest_module():
    """Импорт eac_build_manifest (без сайд-эффектов при импорте)."""
    sys.path.insert(0, HERE)
    import eac_build_manifest
    return eac_build_manifest


def resolve_base(arg):
    """Папка базовой сборки BUILD_EAC\\<имя> из --base (допускается путь к её Windows) или latest.json; None — нет."""
    if arg:
        p = os.path.abspath(arg).rstrip("\\/")
        if os.path.basename(p).lower() == "windows":
            p = os.path.dirname(p)
        return p if os.path.isdir(os.path.join(p, "Windows")) else None
    bm = load_manifest_module()
    p = bm.read_latest(BUILD_ROOT)
    return p if p and os.path.isdir(os.path.join(p, "Windows")) else None


def base_build_window(build_dir):
    """(t0, t_end) успешной сборки базы по её журналу <имя>.logs\\eac_package.log: начало шага 3 (UAT) и маркер
    «UAT BUILD SUCCESSFUL». None — журнала нет или успешного UAT в нём нет."""
    name = os.path.basename(build_dir.rstrip("\\/"))
    log = os.path.join(os.path.dirname(build_dir), name + ".logs", "eac_package.log")
    if not os.path.isfile(log):
        return None
    day, t0, win = None, None, None
    with open(log, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.match(r"\[(\d\d:\d\d:\d\d)\] === eac_package: (\d{4}-\d\d-\d\d) ", line)
            if m:
                day, t0 = m.group(2), None
                continue
            m = re.match(r"\[(\d\d:\d\d:\d\d)\] шаг 3: UAT: ", line)
            if m and day:
                t0 = datetime.datetime.strptime(day + " " + m.group(1), "%Y-%m-%d %H:%M:%S")
                continue
            m = re.match(r"\[(\d\d:\d\d:\d\d)\] шаг 3: UAT BUILD SUCCESSFUL", line)
            if m and day and t0:
                win = (t0, datetime.datetime.strptime(day + " " + m.group(1), "%Y-%m-%d %H:%M:%S"))
    return win


def base_keys_ok(base_game_root, gbc):
    """(bool, пояснение): exe базы собран текущим набором ключей — в нём все 4 замаскированные доли ключа pak и модуль
    RSA подписи из текущего AsdaPakKey.generated.h (сами ключи не печатаются)."""
    exe = os.path.join(base_game_root, GAME_EXE_REL)
    hdr = gbc.read_header()
    if not os.path.isfile(exe) or not hdr or "error" in hdr:
        return False, "нет exe базы или заголовка ключей"
    need = {bytes(part): "доля %d" % i for i, (_, _, part) in enumerate(hdr["parts"])}
    if hdr.get("modulus"):
        need[bytes(hdr["modulus"][:64])] = "модуль подписи"
    found = file_contains_any(exe, set(need))
    miss = [need[n] for n in need if n not in found]
    if miss:
        return False, "в exe базы нет: %s (набор %s)" % (", ".join(miss), hdr.get("build_id"))
    return True, "exe базы содержит доли ключа и модуль подписи набора %s" % hdr.get("build_id")


COOKED_DIR = os.path.join(PROJECT_DIR, "Saved", "Cooked", "Windows", "AsdaReborn")   # кук проекта (общий для всех сборок)
COOKED_STAMP_FILES = ("Metadata/CookMetadata.ucookmeta", "AssetRegistry.bin")     # отметки кука


def cooked_stamp():
    """{отн. путь: mtime_ns} отметок текущего кука (пусто — кука нет)."""
    out = {}
    for rel in COOKED_STAMP_FILES:
        p = os.path.join(COOKED_DIR, rel.replace("/", os.sep))
        if os.path.isfile(p):
            out[rel] = os.stat(p).st_mtime_ns
    return out


def cooked_crypto_matches(gbc):
    """Ключ в Saved/Cooked/…/Metadata/Crypto.json кука = ключу DefaultCrypto.ini (значения не печатаются)."""
    p = os.path.join(COOKED_DIR, "Metadata", "Crypto.json")
    ini = gbc.read_ini() or {}
    try:
        j = json.load(open(p, "r", encoding="utf-8"))
        return bool(ini.get("EncryptionKey")) and j.get("EncryptionKey", {}).get("Key") == ini.get("EncryptionKey")
    except (OSError, ValueError, AttributeError):
        return False


def cooked_ok_for(base_manifest, gbc):
    """(bool, пояснение): Saved/Cooked — тот самый кук, из которого собрана база."""
    want = (base_manifest or {}).get("cooked_stamp") or {}
    cur = cooked_stamp()
    if not want:
        return False, "в манифесте базы нет отметки кука"
    if cur != {k: int(v) for k, v in want.items()}:
        return False, "отметки Saved/Cooked изменились после базы (кук перезаписан)"
    if not cooked_crypto_matches(gbc):
        return False, "Crypto.json кука не совпадает с DefaultCrypto.ini"
    return True, "Saved/Cooked — кук базы (отметки и Crypto.json совпадают)"


COOK_STATE = os.path.join(PORT_DIR, "Build", "_cook_state.json")   # ★ 10.11.2026: последний успешный кук ЛЮБОЙ цепочки


def write_cook_state(meta, mode):
    """Запомнить отметки Saved/Cooked после успешного UAT (общий файл цепочек Dev и Shipping)."""
    data = {"stamp": cooked_stamp(), "meta": meta, "config": CONFIG, "mode": mode,
            "written": datetime.datetime.now().isoformat(" ", "seconds")}
    tmp = COOK_STATE + ".part"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False)
    os.replace(tmp, COOK_STATE)


def cooked_trusted(cur_meta, gbc):
    """(bool, пояснение): Saved/Cooked — целый успешный кук другой цепочки (Dev ↔ Shipping) с тем же meta и ключами.
    Кук от конфигурации клиента не зависит (UAT зовёт один и тот же -run=Cook -TargetPlatform=Windows), поэтому
    инкрементальный кук (-iterate) на нём законен; оборванный/посторонний кук отметок не совпадёт → не доверяем."""
    try:
        st = json.load(open(COOK_STATE, "r", encoding="utf-8"))
    except (OSError, ValueError):
        return False, "общей отметки кука нет (%s)" % COOK_STATE
    cur = cooked_stamp()
    if not cur or cur != {k: int(v) for k, v in (st.get("stamp") or {}).items()}:
        return False, "Saved/Cooked изменён после последнего успешного кука (оборван или посторонний)"
    if (st.get("meta") or {}) != cur_meta:
        return False, "meta последнего кука отличается от текущего"
    if not cooked_crypto_matches(gbc):
        return False, "Crypto.json кука не совпадает с DefaultCrypto.ini"
    return True, "Saved/Cooked — успешный кук цепочки %s от %s (отметки, meta и ключи совпадают)" % (st.get("config"), st.get("written"))


def ufs_manifest_path(game_root):
    """Manifest_UFSFiles_Win64.txt сборки: с 16.10.2026 в <архив>/_meta, у старых сборок — в корне игры."""
    meta = os.path.join(os.path.dirname(game_root), "_meta", "Manifest_UFSFiles_Win64.txt")
    return meta if os.path.isfile(meta) else os.path.join(game_root, "Manifest_UFSFiles_Win64.txt")


def project_categories(bm, base_game_root):
    """Категории входов проекта (UFS-манифест базы — для config_pak и удалённого content)."""
    ufs = bm.ufs_project_entries(ufs_manifest_path(base_game_root)) if base_game_root else {}
    mi = load_icon_module()
    # ★ 16.10.2026: иконка (исходник, генератор, ico — его встраивает UBT) и проверки ритейл-защиты — тоже входы сборки.
    extra = [TOOLKIT_ZIP, SDK_EOS_DLL, os.path.abspath(__file__), os.path.join(HERE, "eac_build_manifest.py"),
             mi.SRC_PNG, os.path.abspath(mi.__file__), mi.APP_ICO, os.path.join(HERE, "asda_retail_guard_check.py"),
             os.path.join(HERE, "data", "legal", "EULA_EN.txt")]
    return bm.classify_project_files(PROJECT_DIR, extra, ufs), ufs


def ensure_base_manifest(bm, base_dir, gbc, cache, cur_meta):
    """Манифест базы; если его нет — ретроспективный (если доказуем) с записью в папку базы. (манифест|None, пояснение)."""
    man = bm.read_manifest(base_dir)
    if man:
        return man, "манифест базы: %s" % os.path.join(base_dir, bm.MANIFEST_NAME)
    win = base_build_window(base_dir)
    if not win:
        return None, "у базы нет манифеста и журнала с успешным UAT — ретро-манифест недоказуем"
    t0, t_end = win
    stamp = cooked_stamp()
    t0e, te = t0.timestamp(), t_end.timestamp()
    stamp_in_window = bool(stamp) and all(t0e <= v / 1e9 <= te + 60 for v in stamp.values())
    game_root = os.path.join(base_dir, "Windows")
    cats, ufs = project_categories(bm, game_root)
    # meta базы: всё, что в meta, к моменту t0 уже было в нынешнем виде — тогда meta базы = текущей; иначе недоказуемо.
    meta_files = [bm.ENGINE_BUILD_VERSION, os.path.join(PROJECT_DIR, "Config", "DefaultGame.ini"),
                  os.path.join(PROJECT_DIR, "Config", "DefaultCrypto.ini")]
    late = [p for p in meta_files if os.path.isfile(p) and os.path.getmtime(p) > t0e]
    if late:
        return None, "meta базы недоказуем: после начала её сборки менялись %s" % ", ".join(os.path.basename(p) for p in late)
    note = ("ретро-манифест: сборка базы %s — %s; файлы с mtime ≤ начала UAT считаются состоянием базы, позднее — "
            "«неизвестно» (= изменены); удалённые после базы content/config — по Manifest_UFSFiles базы" %
            (t0.strftime("%Y-%m-%d %H:%M:%S"), t_end.strftime("%H:%M:%S")))
    man = bm.build_retro_manifest(cats, cache, t0e, dict(cur_meta), ufs, note)
    man["build_window"] = [t0.isoformat(" "), t_end.isoformat(" ")]
    man["cooked_stamp"] = stamp if stamp_in_window else {}
    man["cooked_note"] = ("отметки Saved/Cooked внутри окна сборки базы" if stamp_in_window else
                          "отметки Saved/Cooked вне окна сборки базы — кук базы не доказан")
    bm.write_manifest(base_dir, man)
    return man, note


def print_changes(diffs, limit=12):
    """Что изменилось — по категориям, первые limit файлов каждого вида."""
    for c, d in diffs.items():
        for kind, title in (("modified", "изменён"), ("added", "добавлен"), ("removed", "удалён"),
                            ("unknown", "изменён после базы (mtime позже её сборки)")):
            items = d.get(kind) or []
            for rel in items[:limit]:
                LOG("    %-10s %s: %s" % (c, title, rel))
            if len(items) > limit:
                LOG("    %-10s … и ещё %d (%s)" % (c, len(items) - limit, title))


def uat_cmd_for(gbc, mode, out_dir):
    """Команда UAT режима: full — как раньше; skipcook — -skipcook вместо -cook; iterate — -cook -iterate."""
    cmd = localize_engine(gbc.UAT_CMD.format(cfg=CONFIG, out=out_dir))
    if mode == "skipcook":
        cmd = re.sub(r"\s-cook\s", " -skipcook ", cmd)
    elif mode == "iterate":
        cmd = re.sub(r"\s-cook\s", " -cook -iterate ", cmd)
    return cmd


def step_ubt_shipping(log_path):
    """Быстрый режим: UBT Build.bat AsdaReborn Win64 <CONFIG>. Успех — код 0 и «Result: Succeeded»."""
    bat = os.path.join(ue_dir(), "Engine", "Build", "BatchFiles", "Build.bat")
    cmd = '"%s" AsdaReborn Win64 %s -Project="%s" -WaitMutex' % (bat, CONFIG, os.path.join(PROJECT_DIR, "AsdaReborn.uproject"))
    LOG("UBT: %s" % cmd)
    t0 = datetime.datetime.now()
    with open(log_path, "w", encoding="utf-8", errors="replace") as lf:
        r = subprocess.run('cmd /d /s /c "%s"' % cmd, stdout=lf, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                           creationflags=0x08000000, cwd=PROJECT_DIR)
    text = open(log_path, "r", encoding="utf-8", errors="replace").read()
    ok = r.returncode == 0 and "Result: Succeeded" in text
    for line in text.splitlines():
        if re.search(r"\berror\b", line) and len(line) < 400:
            LOG("  UBT| " + line)
    if not ok:
        raise PackageError("UBT %s: код %d, «Result: Succeeded» %s — см. %s" % (CONFIG, r.returncode, "есть" if "Result: Succeeded" in text else "НЕТ", log_path))
    exe = os.path.join(PROJECT_DIR, "Binaries", "Win64", PROJECT_EXE_BY_CONFIG[CONFIG])
    LOG("UBT %s — Succeeded за %.1f с; exe %s (%d б, %s)" % (CONFIG, (datetime.datetime.now() - t0).total_seconds(), exe,
        os.path.getsize(exe), datetime.datetime.fromtimestamp(os.path.getmtime(exe)).strftime("%H:%M:%S")))
    return exe


def base_game_files(base_dir, base_manifest):
    """Множество отн. путей корня игры базы НА МОМЕНТ ЕЁ СБОРКИ: из манифеста (game_files) или — для старой базы —
    файлы с mtime ≤ mtime её <имя>.md5.txt (пишется последним шагом). Всё прочее в папке базы появилось позже
    (пример 16.10: игра при живом запуске записала AsdaReborn/system/Shortcutkey.ini) и в новую сборку не идёт."""
    files = (base_manifest or {}).get("game_files")
    if files:
        return set(files)
    md5_txt = os.path.join(os.path.dirname(base_dir), os.path.basename(base_dir) + ".md5.txt")
    if not os.path.isfile(md5_txt):
        return None
    limit = os.path.getmtime(md5_txt) + 1
    root = os.path.join(base_dir, "Windows")
    return {rel for p, rel in walk_files(root) if os.path.getmtime(p) <= limit}


def step_copy_base(base_game_root, game_root, allowed=None):
    """Быстрый режим: копия корня игры базы (robocopy /E) и удаление из КОПИИ файлов, которых не было в сборке базы.
    Код robocopy < 8 — успех."""
    os.makedirs(game_root, exist_ok=True)
    r = subprocess.run(["robocopy", base_game_root, game_root, "/E", "/COPY:DAT", "/DCOPY:T", "/R:1", "/W:1", "/MT:8",
                        "/NFL", "/NDL", "/NP", "/NJH"], capture_output=True, text=True, encoding="cp866", errors="replace")
    if r.returncode >= 8:
        raise PackageError("robocopy базы: код %d\n%s" % (r.returncode, r.stdout[-2000:]))
    if allowed is None:
        raise PackageError("состав базы на момент её сборки неизвестен (нет game_files в манифесте и <база>.md5.txt)")
    for p, rel in list(walk_files(game_root)):
        if rel not in allowed:
            os.remove(p)                                               # копия постороннего файла (оригинал в базе не трогаем)
            LOG("  не из сборки базы (появился позже) — в новую сборку не идёт: %s" % rel)
    n = sum(1 for _ in walk_files(game_root))
    LOG("копия базы %s → %s: %d файлов (robocopy код %d)" % (base_game_root, game_root, n, r.returncode))


def step_swap_binaries(game_root, new_exe):
    """Быстрый режим: подмена Shipping-exe и (если отличается) DLL EOS в подпапке AsdaEOS."""
    dst = os.path.join(game_root, GAME_EXE_REL)
    old_md5 = md5_file(dst) if os.path.isfile(dst) else "-"
    shutil.copy2(new_exe, dst)
    LOG("exe: %s → %s (md5 %s → %s)" % (new_exe, GAME_EXE_REL, old_md5, md5_file(dst)))
    src_dll = os.path.join(PROJECT_DIR, "Source", "ThirdParty", "AsdaEOSSDK", "SDK", "Bin", "EOSSDK-Win64-Shipping.dll")
    dll = os.path.join(game_root, EOS_DLL_REL)
    if os.path.isfile(src_dll) and (not os.path.isfile(dll) or md5_file(src_dll) != md5_file(dll)):
        os.makedirs(os.path.dirname(dll), exist_ok=True)
        shutil.copy2(src_dll, dll)
        LOG("DLL EOS заменена из %s" % src_dll)
    else:
        LOG("DLL EOS не менялась")


def step_exe_has_current_keys(game_root, gbc):
    """После сборки: exe архива содержит доли ключа текущего набора (pak архива открывается этим exe)."""
    ok, why = base_keys_ok(game_root, gbc)
    if not ok:
        raise PackageError("exe архива не от текущего набора ключей: %s" % why)
    LOG("exe архива: %s — ОК" % why.replace("exe базы", "exe"))


# ── main ───────────────────────────────────────────────────────────────────────────────────────────────────────
def pick_out_dir(with_time=False):
    """BUILD_EAC\\<ГГГГ-ММ-ДД>, при занятости (или with_time) — <ГГГГ-ММ-ДД_ЧЧММ>."""
    now = datetime.datetime.now()
    d = os.path.join(BUILD_ROOT, now.strftime("%Y-%m-%d"))
    if with_time or os.path.exists(d):
        d = os.path.join(BUILD_ROOT, now.strftime("%Y-%m-%d_%H%M"))
    return d


def step_chunk_warnings(uat_log):
    """★ 16.10.2026 (система обновлений): карта без правила чанка ([ЧАНКИ] от UAsdaAssetManager) — отказ, а не тихий чанк 99."""
    hits = []
    if os.path.isfile(uat_log):
        with open(uat_log, encoding="utf-8", errors="replace") as f:
            hits = sorted({ln.strip() for ln in f if "[ЧАНКИ]" in ln})
    if hits:
        for h in hits[:10]:
            LOG("  " + h)
        raise PackageError("кук: %d папок карт без правила чанка — добавь карту через Tools/asda_map_registry.py" % len(hits))
    LOG("чанки: предупреждений [ЧАНКИ] в логе кука нет — ОК")


def step_pak_verify(game_root):
    """★ 16.10.2026: шифрование и подпись КАЖДОГО контейнера (Tools/asda_pak_verify.py, ключ сборки не печатается)."""
    sys.path.insert(0, HERE)
    import asda_pak_verify
    if asda_pak_verify.run(game_root, use_key=True, samples=64) != 0:
        raise PackageError("asda_pak_verify: контейнеры не прошли проверку шифрования/подписи — архив НЕ готов")


def tail_steps(game_root, build_label, gbc, out_dir, name, do_zip, timer):
    """Общий хвост всех режимов: DLL, dist, extras, integrity, утечки, дев-строки, md5, zip."""
    timer.start("проверка DLL EOS")
    step_check_eos_dll(game_root)
    timer.start("dist тулкита + Settings.json + bat/EULA")
    step_dist(game_root)
    step_extras(game_root, build_label)
    timer.start("integrity-каталог")
    step_integrity(game_root)
    timer.start("проверки утечек и дев-строк")
    errs = step_leaks(game_root, gbc) + step_dev_strings(game_root)
    if errs:
        raise PackageError("проверки не пройдены: %d нарушений — архив НЕ готов" % errs)
    step_exe_has_current_keys(game_root, gbc)
    timer.start("шифрование каждого контейнера")
    step_pak_verify(game_root)
    timer.start("md5")
    step_md5(game_root, os.path.join(os.path.dirname(out_dir), name + ".md5.txt"))
    if do_zip:
        timer.start("zip")  # имя архива — по конфигурации (EAC_ / DevEAC_)
        zp = ZIP_PREFIX_BY_CONFIG[CONFIG]
        step_zip(game_root, os.path.join(os.path.dirname(out_dir), "%s%s.zip" % (zp, name)), "%s%s" % (zp, name))


def run_auto(a, gbc):
    """Режимы auto / --mode / --code-only: решение по манифестам и сборка выбранным способом. Код возврата."""
    bm = load_manifest_module()
    timer = StepTimer()
    # ★ 16.10.2026: Application.ico из Data/OwnerArt/icon.png — ДО анализа (новая иконка = изменение входа, UBT перелинкует exe).
    load_icon_module().ensure_icon(log=LOG)
    timer.start("анализ изменений (манифест, хеши)")
    cache = bm.HashCache(os.path.join(BUILD_ROOT, bm.HASH_CACHE_NAME))
    key_id = (gbc.read_ini() or {}).get("build_id")
    meta = bm.current_meta(PROJECT_DIR, key_id, gbc.UAT_CMD)
    base_dir = resolve_base(a.base)
    base_game_root = os.path.join(base_dir, "Windows") if base_dir else None
    base_man, keys_ok, cook_ok, cook_trusted = None, False, False, False
    LOG("база: %s" % (base_dir or "НЕТ (ни --base, ни %s)" % os.path.join(BUILD_ROOT, bm.LATEST_NAME)))
    if base_dir:
        base_man, why = ensure_base_manifest(bm, base_dir, gbc, cache, meta)
        LOG("  " + why)
        keys_ok, kwhy = base_keys_ok(base_game_root, gbc)
        LOG("  ключи базы: %s — %s" % ("совпадают" if keys_ok else "НЕ совпадают", kwhy))
        cook_ok, cwhy = cooked_ok_for(base_man, gbc)
        LOG("  кук: %s" % cwhy)
        if not cook_ok:
            cook_trusted, twhy = cooked_trusted(meta, gbc)
            LOG("  кук другой цепочки: %s" % twhy)
    cats, _ = project_categories(bm, base_game_root)
    cur = bm.snapshot(cats, cache, (base_man or {}).get("categories"))
    cache.save()
    LOG("  хеши: из кэша %d, перехешировано %d файлов (%.1f МБ)" % (cache.hits, cache.hashed, cache.hashed_bytes / 2 ** 20))
    mode, reasons, diffs = bm.decide(base_man, cur, meta, keys_ok, cook_ok, release=a.rotate_keys, force=a.force,
                                     cooked_trusted=cook_trusted)
    forced = "code" if a.code_only else (a.mode if a.mode != "auto" else None)
    if forced:
        reasons.append("ручное принуждение: режим %s (auto выбрал бы %s)" % (forced, mode))
        if forced in ("code", "skipcook", "iterate") and not keys_ok:
            raise PackageError("режим %s требует базу с текущим набором ключей — её нет" % forced)
        if forced in ("skipcook",) and not cook_ok:
            raise PackageError("режим skipcook требует кук базы в Saved/Cooked — его нет")
        mode = forced
    if a.rotate_keys and mode != "full":
        # ★ ревизия 16.10: раньше --rotate-keys при принудительном не-full режиме молча игнорировался
        raise PackageError("--rotate-keys требует полный кук (режим full), а выбран %s" % mode)
    LOG("ИЗМЕНЕНИЯ С БАЗЫ:")
    print_changes(diffs)
    LOG("РЕШЕНИЕ: %s" % mode)
    for r in reasons:
        LOG("  причина: " + r)
    timer.stop()
    if a.dry_run:
        LOG("--dry-run: только решение, сборка не выполняется")
        for line in timer.report():
            LOG(line)
        return 0
    if mode == "none":
        LOG("сборка не нужна; последняя: %s" % (base_game_root or "—"))
        return 0
    step_check_editor()
    out_dir = pick_out_dir(with_time=True)
    name = os.path.basename(out_dir)
    game_root = os.path.join(out_dir, "Windows")
    logs = os.path.join(os.path.dirname(out_dir), name + ".logs")
    os.makedirs(logs, exist_ok=True)
    LOG("новая сборка: %s (режим %s, база %s)" % (out_dir, mode, base_dir or "—"))
    new_keys = mode == "full" and a.rotate_keys
    if mode == "code":
        timer.start("UBT %s exe" % CONFIG)
        exe = step_ubt_shipping(os.path.join(logs, "ubt.log"))
        timer.start("копия базовой сборки (robocopy)")
        step_copy_base(base_game_root, game_root, base_game_files(base_dir, base_man))
        # ★ ревизия 16.10: манифесты стейджа базы лежат в <база>/_meta — без них следующий auto не видит состояние сборки
        base_meta = os.path.join(base_dir, "_meta")
        if os.path.isdir(base_meta):
            shutil.copytree(base_meta, os.path.join(out_dir, "_meta"), dirs_exist_ok=True)
        timer.start("подмена exe/DLL")
        step_swap_binaries(game_root, exe)
    else:
        if new_keys:
            timer.start("gen_build_crypto (новые ключи)")
            step_gen_crypto(gbc)
            key_id = (gbc.read_ini() or {}).get("build_id")
            meta = bm.current_meta(PROJECT_DIR, key_id, gbc.UAT_CMD)
            cur["code"] = bm.snapshot({"code": cats["code"]}, cache, {"code": cur["code"]})["code"]
        timer.start("UAT (%s)" % mode)
        os.makedirs(out_dir, exist_ok=True)
        cmd = uat_cmd_for(gbc, mode, out_dir)
        orig = gbc.UAT_CMD
        try:
            gbc.UAT_CMD = cmd.replace("{", "{{").replace("}", "}}")   # step_uat форматирует cfg/out — уже подставлены
            step_uat(gbc, out_dir, os.path.join(logs, "uat.log"))
        finally:
            gbc.UAT_CMD = orig
        step_chunk_warnings(os.path.join(logs, "uat.log"))
        if not os.path.isdir(game_root):
            raise PackageError("нет корня игры %s" % game_root)
        if mode != "skipcook":
            write_cook_state(meta, mode)   # ★ 10.11.2026: кук успешен — другая цепочка может идти от него в iterate
    tail_steps(game_root, "%s, режим %s, ключи %s" % (name, mode, key_id), gbc, out_dir, name,
               a.zip or (mode == "full" and not a.no_zip), timer)
    timer.start("манифест + latest.json")
    man = {"version": bm.MANIFEST_VERSION, "retro": False, "mode": mode, "reasons": reasons,
           "base": base_dir, "meta": meta, "categories": cur, "unknown": {},
           "cooked_stamp": cooked_stamp() if (mode != "code" or cook_ok) else {},
           "created": datetime.datetime.now().isoformat(" ", "seconds"),
           "game_files": sorted(rel for _, rel in walk_files(game_root))}   # состав корня игры на момент сборки
    if mode == "code" and base_man and cook_ok:
        man["cooked_stamp"] = base_man.get("cooked_stamp") or {}
    cache.save()
    LOG("манифест: %s" % bm.write_manifest(out_dir, man))
    LOG("latest: %s" % bm.write_latest(BUILD_ROOT, out_dir, mode))
    LOG("ВРЕМЯ ШАГОВ (режим %s):" % mode)
    for line in timer.report():
        LOG(line)
    LOG("=== ГОТОВО: %s ===" % game_root)
    return 0


def main(argv=None):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass
    ap = argparse.ArgumentParser(description="Защищённая EAC-сборка AsdaReborn: Shipping (релиз) или Development с EAC (по умолчанию — auto)")
    ap.add_argument("--mode", choices=("auto", "code", "skipcook", "iterate", "full"), default="auto",
                    help="auto (умолчание) — режим по изменениям с базы; остальное — ручное принуждение")
    ap.add_argument("--code-only", action="store_true", help="принудительно быстрый режим (= --mode code)")
    ap.add_argument("--base", help="базовая сборка (BUILD_EAC\\<имя> или её Windows); по умолчанию — latest.json")
    ap.add_argument("--rotate-keys", action="store_true",
                    help="новый набор ключей pak + полный кук — ТОЛЬКО при смене major-версии (ключи статические)")
    ap.add_argument("--release", action="store_true", help=argparse.SUPPRESS)  # старый флаг: отказ, см. main
    ap.add_argument("--force", action="store_true", help="собрать, даже если изменений нет (быстрый режим)")
    ap.add_argument("--dry-run", action="store_true", help="только напечатать, что изменилось и какой режим выбран")
    ap.add_argument("--zip", action="store_true", help="сделать zip (в full — по умолчанию, отключается --no-zip)")
    ap.add_argument("--out", help="[старый путь] каталог архива UAT для --skip-build/--verify-only")
    ap.add_argument("--skip-build", action="store_true", help="[старый путь] только шаги 4–10 над готовым архивом --out")
    ap.add_argument("--verify-only", action="store_true", help="[старый путь] только проверки готового архива --out")
    ap.add_argument("--no-zip", action="store_true", help="не делать zip")
    ap.add_argument("--config", choices=("Shipping", "Development"),
                    help="★ 17.10.2026: Shipping (релиз, умолчание; архивы Build\\EAC) или Development с EAC (дев-сборка, "
                         "архивы Build\\Dev). Для --skip-build/--verify-only по умолчанию определяется по exe в архиве")
    a = ap.parse_args(argv)
    if a.release:
        # ★ 16.10.2026: ключи статические (дельта-обновления); прежний --release менял их на каждый релиз.
        ap.error("--release упразднён: ключи pak статические. Смена ключей — только при major-версии: --rotate-keys")
    if (a.skip_build or a.verify_only) and not a.out:
        ap.error("--skip-build/--verify-only требуют --out")
    gbc = load_crypto_module()
    if not (a.skip_build or a.verify_only):
        set_config(a.config or "Shipping")
        log_dir = os.path.join(BUILD_ROOT, "_auto.logs")
        LOG.open(os.path.join(log_dir, "eac_package_%s.log" % datetime.datetime.now().strftime("%Y-%m-%d_%H%M%S")))
        LOG("=== eac_package (auto): %s ===" % datetime.datetime.now().isoformat(" ", "seconds"))
        try:
            return run_auto(a, gbc)
        except PackageError as e:
            LOG("ОТКАЗ: %s" % e)
            return 1
    out_dir = os.path.abspath(a.out).rstrip("\\/")
    # ★ 17.10.2026 (AstoryClient\DEPLOY\Patch\Inbox): --out — папка архива (с подпапкой Windows) ИЛИ сам корень игры.
    if os.path.isdir(os.path.join(out_dir, "Windows")):
        game_root = os.path.join(out_dir, "Windows")
    else:
        game_root = out_dir
    name = os.path.basename(out_dir)
    set_config(a.config or detect_config(game_root) or "Shipping")
    LOG.open(os.path.join(os.path.dirname(out_dir), name + ".logs", "eac_package.log"))
    LOG("=== eac_package: %s (архив %s, корень игры %s, конфигурация %s) ===" %
        (datetime.datetime.now().isoformat(" ", "seconds"), out_dir, game_root, CONFIG))
    try:
        if not a.verify_only:
            build_label = "%s, ключи %s" % (name, (gbc.read_ini() or {}).get("build_id"))
            if not os.path.isdir(game_root):
                raise PackageError("нет корня игры %s" % game_root)
            step_check_eos_dll(game_root)
            step_dist(game_root)
            step_extras(game_root, build_label)
            step_integrity(game_root)
        else:
            step_check_eos_dll(game_root)
        errs = step_leaks(game_root, gbc) + step_dev_strings(game_root)
        if errs:
            raise PackageError("проверки не пройдены: %d нарушений — архив НЕ готов" % errs)
        step_pak_verify(game_root)   # ★ 16.10.2026: шифрование каждого контейнера
        step_md5(game_root, os.path.join(os.path.dirname(out_dir), name + ".md5.txt"))
        if not a.no_zip and not a.verify_only:
            zp = ZIP_PREFIX_BY_CONFIG[CONFIG]
            step_zip(game_root, os.path.join(os.path.dirname(out_dir), "%s%s.zip" % (zp, name)), "%s%s" % (zp, name))
    except PackageError as e:
        LOG("ОТКАЗ: %s" % e)
        return 1
    LOG("=== ГОТОВО: %s ===" % game_root)
    return 0


if __name__ == "__main__":
    sys.exit(main())
