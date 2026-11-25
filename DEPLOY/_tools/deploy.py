# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""deploy.py — общий исполнитель батников AstoryClient\\DEPLOY (сборка и патч), 17.10.2026.

Батники в DEPLOY\\Build и DEPLOY\\Patch — однострочные обёртки над командами этого файла. Сам файл ничего не собирает
и не подписывает: он вызывает готовые инструменты проекта (Tools\\eac_package.py — защищённая сборка,
Tools\\asda_release.py — выпуск, подпись, проверка, выгрузка; Launcher\\make_dist.bat — лаунчер),
пишет журнал в DEPLOY\\<Build|Patch>\\Logs и печатает итог «ГОТОВО» или «ОШИБКА».

Пути — ТОЛЬКО от расположения этого файла (DEPLOY\\_tools → DEPLOY → проект → <ROOT>). Движок UE: переменная ASDA_UE_DIR,
иначе ассоциация движка из AsdaReborn.uproject в реестре, иначе D:\\Games\\UE_5.8.
При импорте ничего не делает (закон tools-no-import-side-effects).

Команды (python deploy.py <команда>):
  build-editor            бинарники редактора: UBT AsdaRebornEditor Win64 Development
  build-dev [--check]     Development с EAC → Build\\Dev\\<дата> (--check — только «что изменилось и какой режим»)
  build-release [--check] Shipping с EAC → Build\\EAC\\<дата> (режим auto: code / skipcook / iterate / full)
  build-launcher          лаунчер → AstoryClient\\Launcher\\dist
  game-collect            DEPLOY\\GAME: актуальный лаунчер + рантайм VC++ + установщик службы EAC + EULA
  installer-build         установщик Inno Setup → DEPLOY\\Installer\\Output\\AStoryRemake_Setup_<версия>.exe
  patch-process           обработать клиент из Patch\\Inbox (готовая EAC-сборка → проверка; голая → EAC-обработка)
  patch-make              новая версия канала alpha в Releases (подпись Ed25519 + глубокая проверка); версия — вводом
  patch-verify            глубокая проверка хранилища выпусков
  patch-upload            выгрузка канала alpha на сервер обновлений (без подтверждения — решение владельца)
  patch-all               patch-process (если Inbox не пуст) → patch-make → patch-upload
  patch-resign            продлить срок подписи канала alpha без новой версии
  launcher-release        выпуск канала launcher из Launcher\\dist (версия — вводом) и его выгрузка
  status                  текущие версии каналов alpha и launcher
  keys-status             какие ключи выпуска есть и доступны
  keys-backup             копия закрытых ключей выпуска под пароль владельца
"""
import argparse
import datetime
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import zipfile

# ── пути (только относительные к этому файлу) ───────────────────────────────────────────────────────────────────
HERE = os.path.dirname(os.path.abspath(__file__))              # DEPLOY\_tools
DEPLOY = os.path.dirname(HERE)                                 # DEPLOY
PROJECT = os.path.dirname(DEPLOY)                              # UE-проект = корень клона (папка над DEPLOY, имя любое)
PORT = os.path.dirname(PROJECT)                                # <ROOT>: папка с клонами (Build, Releases, ThirdParty)
DEPLOY_TOOLS = os.path.dirname(os.path.abspath(__file__))     # DEPLOY\_tools (ue_queue.py)
TOOLS = os.path.join(PROJECT, "Tools")                         # инструменты проекта
UPROJECT = os.path.join(PROJECT, "AsdaReborn.uproject")        # файл проекта
LAUNCHER = os.path.join(PROJECT, "Launcher")                   # исходники и dist лаунчера
BUILD_EAC = os.path.join(PORT, "Build", "EAC")                 # релизные сборки (eac_package, Shipping)
BUILD_DEV = os.path.join(PORT, "Build", "Dev")                 # дев-сборки (eac_package, Development с EAC)
INBOX = os.path.join(DEPLOY, "Patch", "Inbox")                 # сюда кладётся клиент для патча
INBOX_STATE = os.path.join(INBOX, "_state.json")               # итог patch-process: какой корень игры готов
LOG_DIRS = {"build": os.path.join(DEPLOY, "Build", "Logs"), "patch": os.path.join(DEPLOY, "Patch", "Logs")}
UE_DEFAULT = r"D:\Games\UE_5.8"                                # движок по умолчанию (как Tools\asda_ue_engine.bat)
GAME_CHANNEL, LAUNCHER_CHANNEL = "alpha", "launcher"           # каналы обновлений (asda_release.py)
EDITOR_BLOCKERS = ("UnrealEditor.exe", "UnrealEditor-Cmd.exe", "LiveCodingConsole.exe", "UnrealBuildTool.exe")
INBOX_IGNORE = {"readme.md", "_state.json"}                    # служебное в Inbox (не клиент)


# ── журнал ───────────────────────────────────────────────────────────────────────────────────────────────────────
class Log:
    """Консоль + файл журнала запуска (UTF-8)."""

    def __init__(self):
        self.f = None        # открытый файл журнала
        self.path = None     # путь журнала

    def open(self, area, name):
        """Журнал DEPLOY\\<area>\\Logs\\<name>_<время>.log."""
        d = LOG_DIRS[area]
        os.makedirs(d, exist_ok=True)
        self.path = os.path.join(d, "%s_%s.log" % (name, datetime.datetime.now().strftime("%Y-%m-%d_%H%M%S")))
        self.f = open(self.path, "a", encoding="utf-8")

    def __call__(self, msg=""):
        line = "[%s] %s" % (datetime.datetime.now().strftime("%H:%M:%S"), msg)
        print(line, flush=True)
        if self.f:
            self.f.write(line + "\n")
            self.f.flush()

    def raw(self, line):
        """Строка вывода дочернего процесса (без метки времени)."""
        print(line, flush=True)
        if self.f:
            self.f.write(line + "\n")
            self.f.flush()


LOG = Log()


class DeployError(Exception):
    """Провал шага — сообщение для журнала, итог «ОШИБКА»."""


# ── окружение ────────────────────────────────────────────────────────────────────────────────────────────────────
def ue_dir():
    """Корень движка: ASDA_UE_DIR → ассоциация движка .uproject в реестре → D:\\Games\\UE_5.8."""
    env = os.environ.get("ASDA_UE_DIR")
    if env:
        return env
    try:
        assoc = json.load(open(UPROJECT, encoding="utf-8-sig")).get("EngineAssociation", "")
    except (OSError, ValueError):
        assoc = ""
    if assoc:
        try:
            import winreg
            if assoc.startswith("{"):
                with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Epic Games\Unreal Engine\Builds") as k:
                    path = winreg.QueryValueEx(k, assoc)[0]
            else:
                with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\EpicGames\Unreal Engine\%s" % assoc) as k:
                    path = winreg.QueryValueEx(k, "InstalledDirectory")[0]
            if path and os.path.isdir(path):
                return os.path.normpath(path)
        except OSError:
            pass
    return UE_DEFAULT


def child_env():
    """Окружение дочерних процессов: UTF-8 вывод Python, ASDA_UE_DIR этой машины."""
    env = dict(os.environ)
    env["PYTHONIOENCODING"] = "utf-8"
    env["ASDA_UE_DIR"] = ue_dir()
    return env


def running(names):
    """Какие из процессов names запущены (tasklist)."""
    out = subprocess.run(["tasklist", "/FO", "CSV", "/NH"], capture_output=True, text=True, encoding="cp866",
                         errors="replace").stdout.lower()
    return [n for n in names if '"%s"' % n.lower() in out]


def run(cmd, cwd=None, shell=False):
    """Запуск с выводом в консоль и журнал построчно. Возвращает (код, весь вывод)."""
    LOG("запуск: %s" % (cmd if isinstance(cmd, str) else " ".join('"%s"' % c if " " in c else c for c in cmd)))
    p = subprocess.Popen(cmd, cwd=cwd or PORT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=None,
                         env=child_env(), shell=shell)
    lines = []
    for raw in p.stdout:
        try:
            line = raw.decode("utf-8").rstrip("\r\n")
        except UnicodeDecodeError:
            line = raw.decode("cp866", errors="replace").rstrip("\r\n")
        lines.append(line)
        LOG.raw(line)
    return p.wait(), "\n".join(lines)


def py(script, *args):
    """Запуск Python-инструмента проекта из AsdaReborn\\Tools (cwd = проект)."""
    rc, out = run([sys.executable, os.path.join(TOOLS, script)] + list(args), cwd=PROJECT)
    return rc, out


def release(*args):
    """asda_release.py с хранилищем по умолчанию (Releases)."""
    return py("asda_release.py", *args)


def ask_version(channel):
    """Номер версии вручную (решение владельца: без подсказки). Текущая версия канала показывается для справки."""
    rc, out = release("--channel", channel, "status")
    print()
    v = ""
    while not v:
        v = input("Введите версию для канала %s (например %s): " %
                  (channel, "alpha-1.0.1-10.2026" if channel == GAME_CHANNEL else "launcher-1.0.1-10.2026")).strip()
    LOG("версия: %s" % v)
    return v


# ── сборка ───────────────────────────────────────────────────────────────────────────────────────────────────────
def cmd_build_editor(a):
    """Бинарники редактора (AsdaRebornEditor Win64 Development). Редактор должен быть закрыт — процессы не завершаем."""
    # ★ 10.11.2026: вместо отказа «запущен редактор» — общая очередь машины (ue_queue): ждём своей очереди сами.
    # ★ 10.11.2026 (ревизия потока): сборка без изменений не запускается вовсе. За 01–10.11 таких было 236 из 685
    #   (в среднем 0,6 мин каждая + ожидание очереди). Отпечаток входов (путь, размер, mtime каждого файла Source и
    #   Plugins/*/Source, .uproject, .uplugin) сверяется с отпечатком последней СОГЛАСОВАННОЙ сборки; совпал — бинарь актуален.
    #   ASDA_BUILD_FORCE=1 — собрать всё равно.
    # ★ 10.11.2026 (рефакторинг R2, три пишущие сессии в одном дереве): «запись правки + сборка» одним захватом очереди.
    #   ASDA_BUILD_PRE=<команда> выполняется ПОСЛЕ получения слота и ДО сборки (скрипт сессии кладёт свою правку в дерево);
    #   если сборка упала — выполняется ASDA_BUILD_ROLLBACK=<команда> (скрипт возвращает файлы из бэкапа) и дерево
    #   пересобирается, чтобы у соседей оно всегда компилировалось. Без этого чужой несобранный разрез ронял чужие сборки.
    pre, rollback = os.environ.get("ASDA_BUILD_PRE", ""), os.environ.get("ASDA_BUILD_ROLLBACK", "")
    if pre and not rollback:
        raise DeployError("ASDA_BUILD_PRE задан без ASDA_BUILD_ROLLBACK — откат обязателен")
    if not pre and editor_up_to_date():
        return
    with ue_slot("exclusive", "build-editor"):
        if pre:
            rc, _ = run(pre, cwd=PROJECT, shell=True)
            if rc != 0:
                run(rollback, cwd=PROJECT, shell=True)
                raise DeployError("ASDA_BUILD_PRE вернул код %d — правка отменена (выполнен ASDA_BUILD_ROLLBACK), сборка не запускалась" % rc)
            try:
                _build_editor_locked()
            except DeployError as e:
                LOG("сборка с правкой упала: %s — выполняю ASDA_BUILD_ROLLBACK и пересобираю дерево" % e)
                rc2, _ = run(rollback, cwd=PROJECT, shell=True)
                try:
                    _build_editor_locked()
                    LOG("дерево после отката собрано")
                except DeployError as e2:
                    LOG("ВНИМАНИЕ: дерево после отката НЕ собралось: %s" % e2)
                raise DeployError("правка откатана (код отката %d): %s" % (rc2, e))
            return
        if editor_up_to_date():      # пока стояли в очереди, то же состояние мог собрать другой
            return
        _build_editor_locked()


def _build_editor_locked():
    """Сборка редактора; вызывается только под exclusive-слотом очереди UE."""
    bat = os.path.join(ue_dir(), "Engine", "Build", "BatchFiles", "Build.bat")
    if not os.path.isfile(bat):
        raise DeployError("нет %s — движок UE 5.8 не найден: задайте переменную ASDA_UE_DIR (корень UE 5.8) "
                          "или установите UE 5.8 из Epic Games Launcher (EngineAssociation \"5.8\"); "
                          "см. Prerequisites\\README.md" % bat)
    # ★ 22.10.2026: правка исходников ВО ВРЕМЯ сборки (два агента в одном дереве) дала дважды несогласованный бинарь —
    #   часть .cpp собрана под старую раскладку класса (самотест T12 падал, ~1 ч на поиск). Теперь после сборки сверяются
    #   даты исходников: изменённое за время сборки — заголовки «касаются» (перекомпилируются все зависимые) и сборка
    #   повторяется; не утихло за 3 попытки — ошибка, бинарю не верить.
    touch_restored_older(editor_inputs())
    import ue_queue
    if ue_queue.memory_warning():
        LOG(ue_queue.memory_warning())
    for attempt in range(1, 4):
        before = editor_inputs()
        rc, out = run('"%s" AsdaRebornEditor Win64 Development -Project="%s" -WaitMutex' % (bat, UPROJECT), cwd=PROJECT, shell=True)
        if rc != 0 or "Result: Succeeded" not in out:
            raise DeployError("сборка редактора: код %d, «Result: Succeeded» %s" % (rc, "есть" if "Result: Succeeded" in out else "нет"))
        after = editor_inputs()
        changed = sorted(os.path.join(PROJECT, r) for r in set(before) | set(after) if before.get(r) != after.get(r))
        if not changed:
            write_editor_stamp(after)
            return
        LOG("НЕСОГЛАСОВАННО: за время сборки изменено %d файлов исходников: %s" %
            (len(changed), ", ".join(os.path.relpath(p, PROJECT) for p in changed[:12]) + (" …" if len(changed) > 12 else "")))
        for p in changed:
            if p.endswith(".h") and os.path.isfile(p):
                os.utime(p, None)     # и на последней попытке: следующая сборка обязана перекомпилировать зависимые
        if attempt == 3:
            break
        LOG("повторная сборка %d/2 (заголовки обновлены — перекомпилируются все зависимые)" % attempt)
    raise DeployError("исходники меняются во время сборки — бинарь несогласован; дождитесь конца чужих правок и соберите заново")


EDITOR_STAMP = os.path.join(PORT, "Build", "_editor_build_stamp.json")   # отпечаток входов последней согласованной сборки редактора
EDITOR_BINARIES = (os.path.join("Binaries", "Win64", "UnrealEditor-AsdaReborn.dll"), os.path.join("Binaries", "Win64", "AsdaRebornEditor.target"))


def editor_inputs():
    """{отн. путь: [размер, mtime_ns]} всех входов сборки редактора: Source/**, Plugins/**/Source/**, *.uproject, *.uplugin."""
    out = {}

    def add(p):
        try:
            st = os.stat(p)
            out[os.path.relpath(p, PROJECT).replace(os.sep, "/")] = [st.st_size, st.st_mtime_ns]
        except OSError:
            pass
    for root, _dirs, files in os.walk(os.path.join(PROJECT, "Source")):
        for f in files:
            add(os.path.join(root, f))
    for root, dirs, files in os.walk(os.path.join(PROJECT, "Plugins")):
        parts = os.path.relpath(root, PROJECT).split(os.sep)
        if "Source" in parts:
            for f in files:
                add(os.path.join(root, f))
        else:
            dirs[:] = [d for d in dirs if d not in ("Content", "Binaries", "Intermediate", "Resources", "Saved")]
            for f in files:
                if f.endswith(".uplugin"):
                    add(os.path.join(root, f))
    add(UPROJECT)
    return out


def read_editor_stamp():
    """Отпечаток последней согласованной сборки редактора или None."""
    try:
        return json.load(open(EDITOR_STAMP, "r", encoding="utf-8"))
    except (OSError, ValueError):
        return None


def write_editor_stamp(inputs):
    """Запомнить отпечаток входов согласованной сборки (атомарно)."""
    os.makedirs(os.path.dirname(EDITOR_STAMP), exist_ok=True)
    tmp = EDITOR_STAMP + ".part"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump({"built": datetime.datetime.now().isoformat(" ", "seconds"), "inputs": inputs}, f)
    os.replace(tmp, EDITOR_STAMP)


def editor_up_to_date():
    """True — входы те же, что у последней согласованной сборки, и бинарники на месте (сборка не нужна)."""
    if os.environ.get("ASDA_BUILD_FORCE") == "1":
        return False
    st = read_editor_stamp()
    if not st or st.get("inputs") != editor_inputs():
        return False
    if not all(os.path.isfile(os.path.join(PROJECT, b)) for b in EDITOR_BINARIES):
        return False
    LOG("АКТУАЛЬНО: исходники не менялись с согласованной сборки %s — сборка не запускается (ASDA_BUILD_FORCE=1 — собрать всё равно)" % st.get("built"))
    return True


def touch_restored_older(cur):
    """Страж «файл вернули из бэкапа со старым временем»: содержимое (размер/время) отличается от собранного, а mtime не
    новее — UBT такой файл не пересоберёт и бинарь останется от прежней версии (грабли parallel-build-stale-objects).
    Таким файлам время ставится «сейчас» (содержимое не меняется)."""
    st = read_editor_stamp()
    if not st:
        return
    old = st.get("inputs") or {}
    hit = [r for r, v in cur.items() if r in old and old[r] != v and v[1] <= old[r][1]]
    for r in hit:
        try:
            os.utime(os.path.join(PROJECT, r), None)
        except OSError:
            pass
    if hit:
        LOG("страж старого времени: %d файлов изменены, но не новее собранных — время обновлено, UBT их пересоберёт: %s" %
            (len(hit), ", ".join(hit[:8]) + (" …" if len(hit) > 8 else "")))


def sources_changed_since(t0):
    """Файлы исходников проекта (.h/.cpp/.cs в Source), изменённые после метки времени t0 (секунды)."""
    out = []
    for root, _dirs, files in os.walk(os.path.join(PROJECT, "Source")):
        for f in files:
            if f.endswith((".h", ".cpp", ".cs")):
                p = os.path.join(root, f)
                try:
                    if os.path.getmtime(p) > t0:
                        out.append(p)
                except OSError:
                    pass
    return sorted(out)


def ue_slot(kind, what):
    """Слот общей очереди UE (ue_queue.Slot) с журналом deploy; ошибка ожидания → DeployError."""
    sys.path.insert(0, DEPLOY_TOOLS)
    import ue_queue

    class _S(ue_queue.Slot):
        def __enter__(self):
            try:
                return super().__enter__()
            except RuntimeError as e:
                raise DeployError(str(e))
    return _S(kind, what, timeout_min=240.0, log=LOG)   # ★ 11.11: 90 мин не хватало за длинными пакетами самотестов


def cmd_queue(a):
    """Кто держит очередь UE и кто ждёт (ue_queue)."""
    sys.path.insert(0, DEPLOY_TOOLS)
    import ue_queue
    for ln in ue_queue.status_lines():
        LOG(ln)


def cmd_build_dev(a):
    """Development с EAC (eac_package --config Development)."""
    if not a.check:
        with ue_slot("exclusive", "build-dev"):
            rc, _ = py("eac_package.py", "--config", "Development")
        if rc != 0:
            raise DeployError("eac_package (Development): код %d" % rc)
        return
    rc, _ = py("eac_package.py", "--config", "Development", *(["--dry-run"] if a.check else []))
    if rc != 0:
        raise DeployError("eac_package (Development): код %d" % rc)


def cmd_build_release(a):
    """Shipping с EAC (eac_package, режим auto)."""
    if not a.check:
        with ue_slot("exclusive", "build-release"):
            rc, _ = py("eac_package.py")
        if rc != 0:
            raise DeployError("eac_package (Shipping): код %d" % rc)
        return
    rc, _ = py("eac_package.py", *(["--dry-run"] if a.check else []))
    if rc != 0:
        raise DeployError("eac_package (Shipping): код %d" % rc)


def cmd_build_launcher(a):
    """Лаунчер: Launcher\\make_dist.bat → Launcher\\dist."""
    rc, _ = run('"%s"' % os.path.join(LAUNCHER, "make_dist.bat"), cwd=LAUNCHER, shell=True)
    if rc != 0 or not os.path.isfile(os.path.join(LAUNCHER, "dist", "AsdaLauncher.exe")):
        raise DeployError("make_dist.bat: код %d (журнал сборки — Launcher\\build.log)" % rc)


# ── Inbox ────────────────────────────────────────────────────────────────────────────────────────────────────────
# Папка игры в сборке UAT = имя .uproject (AsdaReborn), а не имя папки клона — не переименовывать.
GAME_EXES = {"Shipping": os.path.join("AsdaReborn", "Binaries", "Win64", "AsdaReborn-Win64-Shipping.exe"),
             "Development": os.path.join("AsdaReborn", "Binaries", "Win64", "AsdaReborn.exe")}


def config_of(root):
    """Конфигурация корня игры по exe."""
    for cfg, rel in GAME_EXES.items():
        if os.path.isfile(os.path.join(root, rel)):
            return cfg
    return None


def find_game_root(top, depth=4):
    """Корень игры внутри top: папка, где есть AsdaReborn\\Binaries\\Win64\\<exe игры>."""
    for d, dirs, _ in os.walk(top):
        if d[len(top):].count(os.sep) >= depth:
            dirs[:] = []
            continue
        dirs[:] = [x for x in dirs if not x.endswith(".logs") and x != "_meta"]
        if config_of(d):
            return d
    return None


def is_eac_ready(root):
    """Готовая EAC-сборка: бутстрапер Astory.exe, Settings.json и каталог целостности."""
    cert = os.path.join(root, "EasyAntiCheat", "Certificates")
    return (os.path.isfile(os.path.join(root, "Astory.exe")) and
            os.path.isfile(os.path.join(root, "EasyAntiCheat", "Settings.json")) and
            os.path.isdir(cert) and bool(os.listdir(cert)))


def inbox_items():
    """Клиенты в Inbox (без служебного)."""
    if not os.path.isdir(INBOX):
        return []
    return [os.path.join(INBOX, x) for x in sorted(os.listdir(INBOX))
            if x.lower() not in INBOX_IGNORE and not x.startswith("_") and not x.endswith((".logs", ".md5.txt"))]


def exe_stamp(root):
    """Отметка exe игры (размер + время) — по ней видно, что клиент в Inbox не меняли после обработки."""
    p = os.path.join(root, GAME_EXES[config_of(root)])
    st = os.stat(p)
    return "%d:%d" % (st.st_size, int(st.st_mtime))


def read_state():
    """Итог прошлой обработки Inbox, если корень игры на месте и exe тот же."""
    try:
        s = json.load(open(INBOX_STATE, encoding="utf-8"))
    except (OSError, ValueError):
        return None
    root = s.get("game_root")
    if root and os.path.isdir(root) and config_of(root) and exe_stamp(root) == s.get("exe_stamp"):
        return s
    return None


def cmd_patch_process(a):
    """Обработать клиент из Inbox: zip → распаковка; готовая EAC-сборка → eac_package --verify-only;
    голая сборка UAT → eac_package --skip-build (Astory.exe, Settings.json, каталог целостности, проверки)."""
    items = inbox_items()
    if not items:
        LOG("Inbox пуст — обрабатывать нечего (patch-make возьмёт последнюю релизную сборку Build\\EAC\\latest.json)")
        return
    if len(items) > 1:
        raise DeployError("в Inbox больше одного клиента: %s — оставьте один" % ", ".join(os.path.basename(x) for x in items))
    item = items[0]
    if os.path.isfile(item) and item.lower().endswith(".zip"):
        dest = os.path.splitext(item)[0]
        if os.path.exists(dest):
            raise DeployError("рядом с архивом уже есть папка %s — удалите архив или папку" % os.path.basename(dest))
        LOG("распаковка %s → %s" % (os.path.basename(item), os.path.basename(dest)))
        with zipfile.ZipFile(item) as z:
            z.extractall(dest)
        os.remove(item)
        LOG("архив распакован и удалён")
        item = dest
    if not os.path.isdir(item):
        raise DeployError("в Inbox не папка и не zip: %s" % os.path.basename(item))
    root = find_game_root(item)
    if not root:
        raise DeployError("в %s не найден корень игры (папка с AsdaReborn\\Binaries\\Win64\\AsdaReborn*.exe)" % item)
    cfg = config_of(root)
    ready = is_eac_ready(root)
    LOG("клиент: %s; конфигурация %s; %s" % (root, cfg, "готовая EAC-сборка → проверка" if ready else
                                             "голая сборка → EAC-обработка"))
    if ready:
        rc, _ = py("eac_package.py", "--verify-only", "--out", root, "--config", cfg)
    else:
        rc, _ = py("eac_package.py", "--skip-build", "--no-zip", "--out", root, "--config", cfg)
    if rc != 0:
        raise DeployError("eac_package над клиентом из Inbox: код %d — клиент НЕ готов к патчу" % rc)
    state = {"game_root": root, "config": cfg, "was_eac_ready": ready, "exe_stamp": exe_stamp(root),
             "processed": datetime.datetime.now().isoformat(" ", "seconds")}
    with open(INBOX_STATE, "w", encoding="utf-8") as f:
        json.dump(state, f, ensure_ascii=False, indent=1)
    LOG("клиент готов к патчу: %s" % root)


def patch_source():
    """Корень игры для патча: обработанный клиент из Inbox, иначе (Inbox пуст) — последняя релизная сборка."""
    if inbox_items():
        s = read_state()
        if not s:
            LOG("клиент в Inbox ещё не обработан (или изменён после обработки) — обрабатываю")
            cmd_patch_process(None)
            s = read_state()
        if not s:
            raise DeployError("клиент из Inbox не готов")
        return s["game_root"], s["config"], "Inbox"
    latest = os.path.join(BUILD_EAC, "latest.json")
    try:
        root = json.load(open(latest, encoding="utf-8"))["game_root"]
    except (OSError, ValueError, KeyError):
        raise DeployError("Inbox пуст и нет %s — сначала Build\\3_Release_Build.bat или положите клиент в Inbox" % latest)
    if not os.path.isdir(root):
        # latest.json пишет абсолютный путь; при переносе папки порта ищем ту же сборку рядом с latest.json
        alt = os.path.join(BUILD_EAC, os.path.basename(os.path.dirname(root)), "Windows")
        root = alt if os.path.isdir(alt) else root
    if not os.path.isdir(root):
        raise DeployError("сборка из latest.json не найдена: %s" % root)
    return root, config_of(root), "Build\\EAC\\latest.json"


def cmd_patch_make(a):
    """Новая версия канала alpha в Releases: подпись Ed25519 и глубокая проверка — внутри asda_release build."""
    root, cfg, src = patch_source()
    LOG("источник клиента: %s (%s), конфигурация %s" % (root, src, cfg))
    if cfg != "Shipping":
        LOG("ВНИМАНИЕ: в канал %s уходит %s-сборка (консоль и дев-команды включены)" % (GAME_CHANNEL, cfg))
    version = a.version or ask_version(GAME_CHANNEL)
    rc, _ = release("--channel", GAME_CHANNEL, "build", "--version", version, "--game-root", root)
    if rc != 0:
        raise DeployError("asda_release build: код %d" % rc)
    a.version = version


def cmd_patch_verify(a):
    """Глубокая проверка хранилища выпусков (подписи, хэши, каждый кусок)."""
    rc, _ = release("--channel", GAME_CHANNEL, "verify", "--deep")
    if rc != 0:
        raise DeployError("asda_release verify --deep: код %d" % rc)


def cmd_patch_upload(a):
    """Выгрузка канала alpha на сервер обновлений (без подтверждения — решение владельца)."""
    rc, _ = release("--channel", GAME_CHANNEL, "upload", *(["--dry-run"] if getattr(a, "dry_run", False) else []))
    if rc != 0:
        raise DeployError("asda_release upload: код %d" % rc)


def cmd_patch_all(a):
    """Inbox (если есть) → новая версия → выгрузка."""
    if inbox_items() and not read_state():
        cmd_patch_process(a)
    cmd_patch_make(a)
    cmd_patch_upload(a)


def cmd_patch_resign(a):
    """Продлить срок подписи канала alpha (та же версия)."""
    rc, _ = release("--channel", GAME_CHANNEL, "resign")
    if rc != 0:
        raise DeployError("asda_release resign: код %d" % rc)


def cmd_launcher_release(a):
    """Выпуск канала launcher из Launcher\\dist и выгрузка."""
    dist = os.path.join(LAUNCHER, "dist")
    if not os.path.isfile(os.path.join(dist, "AsdaLauncher.exe")):
        raise DeployError("нет %s\\AsdaLauncher.exe — сначала Build\\4_Launcher_Build.bat" % dist)
    version = a.version or ask_version(LAUNCHER_CHANNEL)
    rc, _ = release("--channel", LAUNCHER_CHANNEL, "build", "--version", version, "--game-root", dist)
    if rc != 0:
        raise DeployError("asda_release build (launcher): код %d" % rc)
    rc, _ = release("--channel", LAUNCHER_CHANNEL, "upload")
    if rc != 0:
        raise DeployError("asda_release upload (launcher): код %d" % rc)


# ── GAME и установщик ────────────────────────────────────────────────────────────────────────────────────────────
GAME = os.path.join(DEPLOY, "GAME")                            # лаунчер + всё, что ставится вместе с ним
GAME_MANAGED = ("Launcher", "Redist", "Legal")                 # подпапки GAME, которые пересобираются целиком
INSTALLER = os.path.join(DEPLOY, "Installer")                  # сценарий Inno Setup и оформление
EAC_TOOLKIT = os.path.join(PORT, "ThirdParty", "EOS-SDK-58105819-Release-v1.19.2.1", "SDK", "Tools",
                           "EOS_AntiCheatTools-win32-x64-2.3.0.zip")
EULA = os.path.join(TOOLS, "data", "legal", "EULA_EN.txt")


def md5_of(path):
    """md5 файла."""
    import hashlib
    h = hashlib.md5()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def launcher_version():
    """Версия лаунчера: последний выпуск канала launcher из Releases\\releases.log (launcher-X.Y.Z-ММ.ГГГГ)."""
    log = os.path.join(PORT, "Releases", "releases.log")
    ver = None
    try:
        for line in open(log, encoding="utf-8"):
            parts = line.split("\t")
            if len(parts) > 1 and parts[1].startswith("launcher-"):
                ver = parts[1]
    except OSError:
        pass
    return ver


def cmd_game_collect(a):
    """DEPLOY\\GAME: актуальный лаунчер (Launcher\\dist) + рантайм VC++ из движка + установщик службы EAC из тулкита + EULA."""
    dist = os.path.join(LAUNCHER, "dist")
    if not os.path.isfile(os.path.join(dist, "AsdaLauncher.exe")):
        raise DeployError("нет %s\\AsdaLauncher.exe — сначала Build\\4_Launcher_Build.bat" % dist)
    vc = os.path.join(ue_dir(), "Engine", "Extras", "Redist", "en-us", "vc_redist.x64.exe")
    for p in (vc, EAC_TOOLKIT, EULA):
        if not os.path.isfile(p):
            raise DeployError("нет источника: %s" % p)
    for sub in GAME_MANAGED:
        p = os.path.join(GAME, sub)
        if os.path.isdir(p):
            shutil.rmtree(p)
    shutil.copytree(dist, os.path.join(GAME, "Launcher"))
    os.makedirs(os.path.join(GAME, "Redist", "EasyAntiCheat"), exist_ok=True)
    shutil.copy2(vc, os.path.join(GAME, "Redist", "vc_redist.x64.exe"))
    with zipfile.ZipFile(EAC_TOOLKIT) as z:
        for n in z.namelist():
            if n.endswith("/"):
                continue
            if n == "dist/EasyAntiCheat/EasyAntiCheat_EOS_Setup.exe" or n.startswith("dist/EasyAntiCheat/Licenses/"):
                rel = n[len("dist/EasyAntiCheat/"):].replace("/", os.sep)
                dst = os.path.join(GAME, "Redist", "EasyAntiCheat", rel)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                with z.open(n) as s, open(dst, "wb") as d:
                    shutil.copyfileobj(s, d)
    os.makedirs(os.path.join(GAME, "Legal"), exist_ok=True)
    shutil.copy2(EULA, os.path.join(GAME, "Legal", "EULA.txt"))
    files = {}
    for sub in GAME_MANAGED:
        for d, _, fns in os.walk(os.path.join(GAME, sub)):
            for fn in fns:
                p = os.path.join(d, fn)
                files[os.path.relpath(p, GAME).replace("\\", "/")] = {"size": os.path.getsize(p), "md5": md5_of(p)}
    man = {"collected": datetime.datetime.now().isoformat(" ", "seconds"), "launcher_release": launcher_version(),
           "vc_redist_source": vc, "eac_toolkit": os.path.basename(EAC_TOOLKIT), "files": files}
    with open(os.path.join(GAME, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(man, f, ensure_ascii=False, indent=1)
    for rel, info in sorted(files.items()):
        LOG("  %-48s %10d  %s" % (rel, info["size"], info["md5"]))
    LOG("GAME собран: %s (%d файлов, лаунчер %s)" % (GAME, len(files), man["launcher_release"] or "версия неизвестна"))


def iscc_path():
    """ISCC.exe Inno Setup 6: из реестра установки, иначе стандартная папка."""
    try:
        import winreg
        for root, key in ((winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1"),
                          (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1"),
                          (winreg.HKEY_CURRENT_USER, r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1")):
            try:
                with winreg.OpenKey(root, key) as k:
                    p = os.path.join(winreg.QueryValueEx(k, "InstallLocation")[0], "ISCC.exe")
                    if os.path.isfile(p):
                        return p
            except OSError:
                continue
    except ImportError:
        pass
    p = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"), "Inno Setup 6", "ISCC.exe")
    return p if os.path.isfile(p) else None


SETUP_BOOT_MAGIC = b"ASDASETUPBOOT01\0"                      # признак нагрузки в хвосте обёртки (AsdaSetupBoot.cpp, kMagic)


def build_setup_boot():
    """Обёртка установщика Installer\\Boot\\build\\AsdaSetupBoot.exe (build_boot.bat, MSVC, статический CRT)."""
    boot_dir = os.path.join(INSTALLER, "Boot")
    rc, _ = run('"%s"' % os.path.join(boot_dir, "build_boot.bat"), cwd=boot_dir, shell=True)
    exe = os.path.join(boot_dir, "build", "AsdaSetupBoot.exe")
    if rc != 0 or not os.path.isfile(exe):
        raise DeployError("build_boot.bat: код %d (журнал — Installer\\Boot\\build\\build.log)" % rc)
    return exe


def pack_setup_boot(boot_exe, inner_dir, setup_name, out_exe):
    """★ 25.10.2026 Установщик одним файлом без распаковки в %TEMP%: обёртка + файлы Inno (UseSetupLdr=no) из inner_dir.
    Формат хвоста (читает AsdaSetupBoot.cpp, ReadPayload): файлы подряд, затем записи [u16 длина имени, имя UTF-16LE,
    u64 смещение, u64 размер]×N, u32 N, u32 размер записей, SETUP_BOOT_MAGIC. setup.exe — первым."""
    import struct
    names = [setup_name] + sorted(n for n in os.listdir(inner_dir) if n != setup_name)
    if not os.path.isfile(os.path.join(inner_dir, setup_name)):
        raise DeployError("нет %s в %s" % (setup_name, inner_dir))
    tmp = out_exe + ".part"
    table = b""
    with open(tmp, "wb") as out:
        with open(boot_exe, "rb") as f:
            shutil.copyfileobj(f, out)
        for n in names:
            off = out.tell()
            with open(os.path.join(inner_dir, n), "rb") as f:
                shutil.copyfileobj(f, out, 1 << 20)
            size = out.tell() - off
            enc = n.encode("utf-16-le")
            table += struct.pack("<H", len(n)) + enc + struct.pack("<QQ", off, size)
        out.write(table)
        out.write(struct.pack("<II", len(names), len(table)) + SETUP_BOOT_MAGIC)
    os.replace(tmp, out_exe)
    return names


def cmd_installer_build(a):
    """Установщик: GAME (собирается, если его нет) → оформление → обёртка Boot → Inno Setup (без загрузчика) →
    Installer\\Output\\AStoryRemake_Setup_<версия>.exe (обёртка + файлы Inno; ★ 25.10.2026 без распаковки в %TEMP%)."""
    if not (os.path.isfile(os.path.join(GAME, "manifest.json")) and
            os.path.isfile(os.path.join(GAME, "Launcher", "AsdaLauncher.exe"))):
        LOG("папка GAME ещё не собрана (нет manifest.json или Launcher\\AsdaLauncher.exe) — собираю")
        cmd_game_collect(a)
    man = json.load(open(os.path.join(GAME, "manifest.json"), encoding="utf-8"))
    rel = a.version or man.get("launcher_release") or ""
    m = re.search(r"(\d+\.\d+\.\d+)", rel)
    if not m:
        raise DeployError("не удалось определить версию лаунчера (%r) — укажите --version 1.2.3" % rel)
    ver = m.group(1)
    rc, _ = run([sys.executable, os.path.join(INSTALLER, "Branding", "make_branding.py")], cwd=INSTALLER)
    if rc != 0:
        raise DeployError("make_branding.py: код %d" % rc)
    iscc = iscc_path()
    if not iscc:
        raise DeployError("не найден Inno Setup 6 (ISCC.exe) — установите Inno Setup 6.5+")
    boot = build_setup_boot()
    out_dir = os.path.join(INSTALLER, "Output")
    name = "AStoryRemake_Setup_%s.exe" % ver
    inner = os.path.join(out_dir, "_inner", ver)                # setup.exe + -0.bin/-1.bin (UseSetupLdr=no)
    if os.path.isdir(inner):
        shutil.rmtree(inner)
    rc, out = run([iscc, "/DAppVer=" + ver, "/DGameDir=" + GAME, "/DBootExe=" + boot, "/O" + inner,
                   os.path.join(INSTALLER, "AStoryRemake.iss")], cwd=INSTALLER)
    if rc != 0 or not os.path.isfile(os.path.join(inner, name)):
        raise DeployError("Inno Setup: код %d, setup %s" % (rc, "есть" if os.path.isfile(os.path.join(inner, name)) else "НЕ создан"))
    exe = os.path.join(out_dir, name)
    parts = pack_setup_boot(boot, inner, name, exe)
    LOG("установщик: %s (%.1f МБ, md5 %s; внутри %s)" % (exe, os.path.getsize(exe) / 2 ** 20, md5_of(exe), ", ".join(parts)))


def cmd_status(a):
    """Текущие версии каналов."""
    for ch in (GAME_CHANNEL, LAUNCHER_CHANNEL):
        release("--channel", ch, "status")


def cmd_keys_status(a):
    """Ключи выпуска."""
    release("keys", "status")


def cmd_keys_backup(a):
    """Копия закрытых ключей выпуска под пароль владельца (пароль вводится в консоли)."""
    rc, _ = release("keys", "export-backup")
    if rc != 0:
        raise DeployError("asda_release keys export-backup: код %d" % rc)


COMMANDS = {
    "build-editor": ("build", cmd_build_editor), "build-dev": ("build", cmd_build_dev),
    "build-release": ("build", cmd_build_release), "build-launcher": ("build", cmd_build_launcher),
    "game-collect": ("build", cmd_game_collect), "installer-build": ("build", cmd_installer_build),
    "patch-process": ("patch", cmd_patch_process), "patch-make": ("patch", cmd_patch_make),
    "patch-verify": ("patch", cmd_patch_verify), "patch-upload": ("patch", cmd_patch_upload),
    "patch-all": ("patch", cmd_patch_all), "patch-resign": ("patch", cmd_patch_resign),
    "launcher-release": ("patch", cmd_launcher_release), "status": ("patch", cmd_status), "queue": ("build", cmd_queue),
    "keys-status": ("patch", cmd_keys_status), "keys-backup": ("patch", cmd_keys_backup),
}


def main(argv=None):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass
    ap = argparse.ArgumentParser(description="Сборка и патч AStory Remake (обёртка AstoryClient\\DEPLOY)")
    ap.add_argument("command", choices=sorted(COMMANDS))
    ap.add_argument("--check", action="store_true", help="build-dev/build-release: только что изменилось и какой режим")
    ap.add_argument("--version", help="patch-make/patch-all/launcher-release: версия без вопроса")
    ap.add_argument("--dry-run", action="store_true", help="patch-upload: только показать, что уйдёт на сервер")
    a = ap.parse_args(argv)
    area, fn = COMMANDS[a.command]
    LOG.open(area, a.command + ("_check" if a.check else ""))
    LOG("=== %s: %s | ROOT = %s | движок = %s ===" % (a.command, datetime.datetime.now().isoformat(" ", "seconds"), PORT, ue_dir()))
    t0 = datetime.datetime.now()
    try:
        fn(a)
    except DeployError as e:
        LOG("ОШИБКА: %s" % e)
        LOG("журнал: %s" % LOG.path)
        return 1
    except KeyboardInterrupt:
        LOG("ОСТАНОВЛЕНО пользователем")
        return 1
    LOG("ГОТОВО за %.1f мин. Журнал: %s" % ((datetime.datetime.now() - t0).total_seconds() / 60, LOG.path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
