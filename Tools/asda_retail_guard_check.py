# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_retail_guard_check.py — проверки ритейл-защиты клиента (16.10.2026, agent-retail-branding; разбор —
#   Reports/anticheat/eac/BRANDING_CONSOLE_REPORT.md, код — Source/AsdaCore/Public/AsdaRetailGuard.h).
#
# Три проверки (функции вызывает Tools/eac_package.py на КАЖДОЙ упаковке; CLI — для ручного прогона):
#   1. dev_names_in_exe(exe)       — в Shipping-exe нет имён наших дев-команд и ASDA_DEV_CVAR (собираются из Source/ по
#                                    RegisterConsoleCommand / FAutoConsoleCommand* / ASDA_DEV_CVAR*), поиск ASCII и UTF-16LE с
#                                    границей имени (asda.Foo не находится внутри asda.Footstep).
#                                    ★ 19.10.2026: вхождение имени внутри строки exe, равной формату UE_LOG/UE_CLOG/UE_LOG_REF
#                                    из Source/ (запечатанный журнал Shipping), — не нарушение (см. «1а»); отдельная строка-
#                                    имя (регистрация) и любое другое вхождение — нарушение, как раньше.
#   2. shipping_selfcheck(exe)     — запуск Shipping-exe СКРЫТО с -AsdaRetailGuardSelfCheck=<файл> и запрещёнными ключами
#                                    (-ExecCmds с дев-пробой, -IniBootstrap=, -textconfig, -DP=, -UseFixedTimeStep, -FPS=,
#                                    -BinaryConfigScalabilityOverride=, «-xDP=» без границы слова) + -AsdaNoNet. Exe очищает
#                                    командную строку ДО WinMain, пишет итог и выходит до движка: ни окна, ни сети, ни
#                                    сообщения «запуск без EAC». Проверяется: запрещённых ключей в итоговой строке нет,
#                                    -AsdaNoNet остался, сработали ожидаемые правила, процесс завершился сам с кодом 0.
#   3. exe_icon_check(exe, ico)    — в ресурсах exe (RT_GROUP_ICON/RT_ICON) лежат ровно картинки Build/Windows/Application.ico.
# Плюс --dev: два прогона Development-игры (редактор движка в режиме -game, -AsdaNoNet -RenderOffscreen, свой
#   -saveddirsuffix с подложенным пользовательским Engine.ini) — «защита» (-AsdaRetailGuard) и «контроль»; самотест
#   AsdaRetailGuardSelfTest.cpp печатает наблюдения, вердикт — здесь (в контроле вход обязан сработать, под защитой — нет).
# Запуск:
#   python Tools/asda_retail_guard_check.py --shipping <exe>   — проверки 1–3 над готовым Shipping-exe
#   python Tools/asda_retail_guard_check.py --dev [--out <папка>] — прогон Development (редактор закрыт не обязателен: -game)
# При импорте ничего не делает (закон tools-no-import-side-effects).
import argparse
import ctypes
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))                         # …\AsdaReborn\Tools
PROJECT_DIR = os.path.dirname(HERE)                                        # …\AsdaReborn
SOURCE_DIR = os.path.join(PROJECT_DIR, "Source")                           # исходники модулей
UPROJECT = os.path.join(PROJECT_DIR, "AsdaReborn.uproject")                # проект
UE_GAME = os.path.join(os.environ.get("ASDA_UE_DIR") or r"D:\Games\UE_5.8",
                       "Engine", "Binaries", "Win64", "UnrealEditor.exe")   # редактор в режиме -game (ASDA_UE_DIR, как asda_selftest_batch)
LOGIN_MAP = "/Game/Map/Login"                                              # карта прогона Development (только чтение)
TAG = "AsdaRetailGuard/v1"                                                 # метка защиты (ASDA_RETAIL_GUARD_TAG)
# Файлы с примерами в комментариях (имена оттуда — не настоящие регистрации).
DOC_ONLY = ("AsdaDevConsole.h",)

# Запрещённые ключи для самопроверки Shipping: (аргумент, правило, которое обязано сработать).
SHIP_BANNED = (
    ("-ExecCmds=asda.RetailGuard.Probe shipping", "ExecCmds="),
    ("-IniBootstrap=C:\\Windows\\win.ini", "IniBootstrap="),
    ("-textconfig", "textconfig"),
    ("-DP=Windows", "DP="),
    ("-xDP=Windows", "DP="),                       # FParse::Value ищет подстроку — без границы слова тоже срабатывает
    ("-UseFixedTimeStep", "UseFixedTimeStep"),
    ("-FPS=5", "FPS="),
    ("-BinaryConfigScalabilityOverride=..\\..\\x", "BinaryConfigScalabilityOverride="),
    ("-dpcvars=r.Fog=0", "DPCVars="),
)
SHIP_KEPT = ("-AsdaNoNet", "-AsdaLang=ru")        # безобидные ключи — обязаны остаться


# ── 1. имена дев-команд ─────────────────────────────────────────────────────────────────────────────────────────
CMD_RE = re.compile(r'(?:RegisterConsoleCommand|FAutoConsoleCommand\w*)\s*\(\s*TEXT\("([^"]+)"\)', re.S)
DEV_CVAR_RE = re.compile(r'ASDA_DEV_CVAR(?:_REF)?\s*\([^;]*?TEXT\("([^"]+)"\)', re.S)


def dev_command_names(source_dir=SOURCE_DIR):
    """Имена дев-команд (все консольные команды порта — дев, закон А2) и ASDA_DEV_CVAR из Source/. → set."""
    names = set()
    for d, _, files in os.walk(source_dir):
        for fn in files:
            if not fn.endswith((".cpp", ".h")) or fn in DOC_ONLY:
                continue
            t = io.open(os.path.join(d, fn), encoding="utf-8", errors="replace").read()
            names.update(CMD_RE.findall(t))
            names.update(DEV_CVAR_RE.findall(t))
    return names


# ── 1а. строки журнала (★ 19.10.2026, журналы релиза, часть A) ─────────────────────────────────────────────────
# С 19.10 Shipping пишет запечатанный журнал: форматы UE_LOG/UE_CLOG/UE_LOG_REF наших модулей лежат в exe литералами, и в
#   некоторых из них есть ИМЕНА дев-ключей (например «asda.Fog.DensityScale=%.2f …»). Это раскрытие названия, а не доступ:
#   в Shipping дев-ключи — константы TAsdaConstCVar (не регистрируются, ini их не задаёт), дев-команды вырезаны. Регистрация
#   требует ОТДЕЛЬНОЙ строки, равной имени целиком (TEXT("asda.Foo") → L"asda.Foo\0"), — её проверка ниже не ослаблена.
#   Вхождение имени прощается ТОЛЬКО если вся окружающая его строка exe (от нуля до нуля) байт в байт равна строковому
#   литералу из Source/ и длиннее самого имени, причём литерал — (а) формат или литерал-аргумент вызова UE_LOG/UE_CLOG/
#   UE_LOG_REF, либо (б) прочий литерал кода (текст, который уходит в журнал через переменную/«?:»), но НЕ литерал вызова
#   регистрации (RegisterConsole*/FAutoConsole*/TAutoConsoleVariable/ASDA_DEV_CVAR: имя и справка). Всё прочее — отдельная
#   строка-имя, справка дев-команды, строка не из Source, комментарий — по-прежнему нарушение.
LOG_CALL_RE = re.compile(r"\b(UE_LOG|UE_CLOG|UE_LOG_REF)\s*\(")
LOG_FORMAT_ARG = {"UE_LOG": 2, "UE_CLOG": 3, "UE_LOG_REF": 2}   # номер аргумента-формата (с нуля)
_ESC = {"n": "\n", "r": "\r", "t": "\t", "0": "\0", "\\": "\\", '"': '"', "'": "'", "a": "\a", "b": "\b", "f": "\f",
        "v": "\v", "?": "?"}


def _unescape_c(body):
    """Тело строкового литерала C++ (без кавычек) → текст."""
    out, i = [], 0
    while i < len(body):
        c = body[i]
        if c != "\\" or i + 1 >= len(body):
            out.append(c)
            i += 1
            continue
        e = body[i + 1]
        if e == "x":
            j = i + 2
            while j < len(body) and body[j] in "0123456789abcdefABCDEF":
                j += 1
            out.append(chr(int(body[i + 2:j] or "0", 16)))
            i = j
        elif e in "uU":
            n = 4 if e == "u" else 8
            out.append(chr(int(body[i + 2:i + 2 + n], 16)))
            i += 2 + n
        else:
            out.append(_ESC.get(e, e))
            i += 2
    return "".join(out)


def _split_call_args(text, start):
    """Аргументы вызова от открывающей скобки text[start] до парной: [(текст аргумента)]; учёт строк, символов, комментариев."""
    args, depth, cur, i = [], 0, [], start
    n = len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            cur.append(text[i:j + 1])
            i = j + 1
            continue
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
            continue
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
            continue
        if c == "(":
            depth += 1
            if depth > 1:
                cur.append(c)
        elif c == ")":
            depth -= 1
            if depth == 0:
                args.append("".join(cur))
                return args
            cur.append(c)
        elif c == "," and depth == 1:
            args.append("".join(cur))
            cur = []
        else:
            cur.append(c)
        i += 1
    return None


_QUOTED = r'"(?:[^"\\]|\\.)*"'                                # один строковый литерал "…"
_QSEQ = _QUOTED + r'(?:\s*' + _QUOTED + r')*'                   # склейка соседних "…" "…" (в т. ч. внутри TEXT( ))
# Цепочка литералов (TEXT("…" "…") / "…") внутри выражения-аргумента.
_LIT1 = r'(?:TEXT\s*\(\s*' + _QSEQ + r'\s*\)|' + _QUOTED + r')'
LIT_RUN_RE = re.compile(_LIT1 + r'(?:\s*' + _LIT1 + r')*', re.S)
# Один кусок: TEXT( склейка ) или голый литерал.
LIT_PIECE_RE = re.compile(r'\s*(?:TEXT\s*\(\s*(' + _QSEQ + r')\s*\)|(' + _QUOTED + r'))\s*', re.S)
_QUOTED_RE = re.compile(r'"((?:[^"\\]|\\.)*)"', re.S)            # содержимое каждого литерала склейки


def _literal_only(arg):
    """Аргумент из одних строковых литералов (TEXT("…") / "…", склейка соседних) → текст; иначе None."""
    pos, parts = 0, []
    while pos < len(arg):
        m = LIT_PIECE_RE.match(arg, pos)
        if not m or m.end() == pos:
            return None
        parts += _QUOTED_RE.findall(m.group(1) if m.group(1) is not None else m.group(2))
        pos = m.end()
    return _unescape_c("".join(parts)) if parts else None


# Вызовы регистрации консольных команд/переменных: их литералы (имя, справка) упоминанием НЕ считаются никогда.
REG_CALL_RE = re.compile(r"\b(?:RegisterConsoleCommand|RegisterConsoleVariable\w*|FAutoConsoleCommand\w*|FAutoConsoleVariable\w*"
                         r"|TAutoConsoleVariable\s*<[^;{}()]*>|ASDA_DEV_CVAR(?:_REF)?)\s*(?:\w+\s*)?\(")
KIND_LOG = "журнал"     # литерал вызова журнала (формат или аргумент)
KIND_TEXT = "строка"    # прочий литерал Source вне регистраций (текст для журнала через переменную, «?:» вне вызова и т. п.)


_COMMENT_OR_LIT_RE = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:[^"\\\n]|\\.)*"|\'(?:[^\'\\\n]|\\.)*\'', re.S)


def _strip_comments(t):
    """Комментарии C++ → пробелы (строковые и символьные литералы не трогаются, переводы строк сохраняются)."""
    def repl(m):
        s = m.group(0)
        if s[0] in "\"'":
            return s
        return re.sub(r"[^\n]", " ", s)
    return _COMMENT_OR_LIT_RE.sub(repl, t)


def mention_literals(source_dir=SOURCE_DIR):
    """Строки Source/, внутри которых имя дев-ключа — лишь упоминание. → {текст: вид}.
    KIND_LOG — формат вызова журнала (UE_LOG/UE_CLOG/UE_LOG_REF; если он из одних литералов) и литералы-аргументы после
    формата; KIND_TEXT — любой другой строковый литерал кода (не комментарий), КРОМЕ литералов вызовов регистрации."""
    out, reg = {}, set()
    for d, _, files in os.walk(source_dir):
        for fn in files:
            if not fn.endswith((".cpp", ".h")):
                continue
            t = _strip_comments(io.open(os.path.join(d, fn), encoding="utf-8", errors="replace").read())
            for m in REG_CALL_RE.finditer(t):
                args = _split_call_args(t, m.end() - 1) or []
                for a in args:
                    for r in LIT_RUN_RE.finditer(a):
                        lit = _literal_only(r.group(0))
                        if lit:
                            reg.add(lit)
            for m in LOG_CALL_RE.finditer(t):
                args = _split_call_args(t, m.end() - 1)
                k = LOG_FORMAT_ARG[m.group(1)]
                if args and len(args) > k:
                    lit = _literal_only(args[k])
                    if lit:
                        out[lit] = KIND_LOG
                    # Литералы-аргументы строки журнала (в т. ч. ветки «?:»): склейки соседних литералов целиком.
                    for a in args[k + 1:]:
                        for r in LIT_RUN_RE.finditer(a):
                            lit = _literal_only(r.group(0))
                            if lit:
                                out[lit] = KIND_LOG
            for r in LIT_RUN_RE.finditer(t):
                lit = _literal_only(r.group(0))
                if lit and lit not in out:
                    out[lit] = KIND_TEXT
    for lit in reg:
        if out.get(lit) == KIND_TEXT:
            del out[lit]
    return out


def log_format_literals(source_dir=SOURCE_DIR):
    """Совместимость: все строки-упоминания Source (см. mention_literals). → dict {текст: вид}."""
    return mention_literals(source_dir)


def _enclosing_string(data, i, n, width):
    """Строка exe (от нулевого символа до нулевого), в которой лежит вхождение data[i:i+n]. → текст или None."""
    enc = "utf-16-le" if width == 2 else "latin-1"
    zero = b"\0" * width
    s = i
    while s - width >= 0 and data[s - width:s] != zero and i - s < 65536:
        s -= width
    e = i + n
    while e + width <= len(data) and data[e:e + width] != zero and e - i < 65536:
        e += width
    try:
        txt = data[s:e].decode(enc)
    except UnicodeDecodeError:
        return None
    if width == 1:
        try:
            txt = data[s:e].decode("utf-8")
        except UnicodeDecodeError:
            pass
    return txt


def _literal_at(data, i, name, width, log_formats):
    """Строка Source с именем, лежащая в exe целиком так, что имя в ней стоит ровно на data[i]. → текст или None."""
    enc = "utf-16-le" if width == 2 else "utf-8"
    for lit in log_formats:
        if name not in lit or lit == name:
            continue
        raw = lit.encode(enc, "ignore")
        nb = name.encode(enc)
        off = raw.find(nb)
        while off >= 0:
            if i - off >= 0 and data[i - off:i - off + len(raw)] == raw:
                return lit
            off = raw.find(nb, off + 1)
    return None


def classify_name_hits(data, name, log_formats, bounded=True, kinds=None):
    """Все вхождения имени в exe (bounded — только с границей имени): → (есть ли вхождение вне строк-упоминаний, число
    упоминаний). log_formats — set или dict {текст: вид}; kinds (dict) — сюда счёт упоминаний по видам."""
    bad, logged = False, 0
    for width, needle in ((1, name.encode("ascii", "ignore")), (2, name.encode("utf-16-le"))):
        if not needle:
            continue
        start = 0
        while True:
            i = data.find(needle, start)
            if i < 0:
                break
            start = i + 1
            nxt = data[i + len(needle): i + len(needle) + width]
            ch = nxt.decode("utf-16-le" if width == 2 else "latin-1", errors="replace") if len(nxt) == width else ""
            if bounded and ch and (ch.isalnum() or ch in "_."):
                continue                                     # не граница имени (asda.Foo внутри asda.Footstep)
            s = _enclosing_string(data, i, len(needle), width)
            if s is None or s == name or s not in log_formats:
                # ★ 16.11.2026 (выпуск 8.0.0): компоновщик кладёт строку вплотную за чужими данными без нулевого байта —
                #   `_enclosing_string` захватывает мусор слева, и строка журнала не узнаётся (ложное нарушение
                #   asda.Skill.HoldMobDeath / asda.SysOpt.LegacyNoRebind66). Тогда ищем строку Source, которая ЦЕЛИКОМ
                #   лежит в exe ровно на этом месте (байт в байт, с именем на своей позиции).
                s = _literal_at(data, i, name, width, log_formats)
            if s is not None and s != name and s in log_formats:
                logged += 1                                  # строка-упоминание из Source — раскрытие имени, не регистрация
                if kinds is not None:
                    k = log_formats.get(s, KIND_LOG) if isinstance(log_formats, dict) else KIND_LOG
                    kinds[k] = kinds.get(k, 0) + 1
            else:
                bad = True
    return bad, logged


def dev_names_in_exe(exe, names=None, log_formats=None, mentions=None, kinds=None):
    """Имена дев-команд/ключей с вхождением в exe вне строк-упоминаний (ASCII или UTF-16LE, с границей имени).
    mentions (dict) — сюда «имя → число упоминаний»; kinds (dict) — счёт упоминаний по видам. → (нарушения, всего имён)."""
    names = names if names is not None else dev_command_names()
    log_formats = log_formats if log_formats is not None else mention_literals()
    data = open(exe, "rb").read()
    hits = []
    for n in sorted(names):
        bad, logged = classify_name_hits(data, n, log_formats, kinds=kinds)
        if bad:
            hits.append(n)
        if logged and mentions is not None:
            mentions[n] = logged
    return hits, len(names)


def tag_in_exe(exe):
    """Метка ритейл-защиты в exe (UTF-16LE)."""
    return TAG.encode("utf-16-le") in open(exe, "rb").read()


# ── 2. самопроверка Shipping ───────────────────────────────────────────────────────────────────────────────────
def shipping_selfcheck(exe, timeout=30.0):
    """Скрытый запуск Shipping-exe с запрещёнными ключами и -AsdaRetailGuardSelfCheck. → (ok, строки отчёта)."""
    if not tag_in_exe(exe):
        # Exe без защиты не выйдет до движка — запустилась бы игра (окно «запуск без EAC»). Не запускаем.
        return False, ["в exe нет метки %s — запуск не выполнялся (exe без защиты поднял бы игру)" % TAG]
    tmp = tempfile.mkdtemp(prefix="asda_rg_selfcheck_")
    out = os.path.join(tmp, "selfcheck.txt")
    lines = []
    try:
        args = [exe] + [a for a, _ in SHIP_BANNED] + list(SHIP_KEPT) + ["-AsdaRetailGuardSelfCheck=" + out]
        si = subprocess.STARTUPINFO()
        si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        si.wShowWindow = 0
        t0 = time.time()
        p = subprocess.Popen(args, cwd=os.path.dirname(exe), startupinfo=si, creationflags=subprocess.CREATE_NO_WINDOW,
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            rc = p.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            subprocess.call(["taskkill", "/PID", str(p.pid), "/T", "/F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            return False, ["процесс не вышел сам за %.0f с — добит (самопроверка до WinMain не сработала)" % timeout]
        dt = time.time() - t0
        lines.append("процесс вышел сам: код %d за %.1f с" % (rc, dt))
        if not os.path.isfile(out):
            return False, lines + ["файла итога нет — защиты в exe нет или ключ самопроверки не распознан"]
        txt = io.open(out, encoding="utf-8", errors="replace").read()
        kv = {}
        for ln in txt.splitlines()[1:]:
            k, _, v = ln.partition("=")
            kv[k] = v
        ok = rc == 0 and txt.startswith(TAG)
        lines.append("метка: %s" % txt.splitlines()[0] if txt else "пусто")
        removed = [r for r in kv.get("removed", "").split(",") if r]
        lines.append("убрано правилами: %s" % ", ".join(removed))
        lines.append("fallback: %s" % kv.get("fallback"))
        cmd = kv.get("cmdline", "")
        lines.append("итоговая строка: %s" % cmd)
        if kv.get("fallback") != "0":
            ok = False
            lines.append("ОШИБКА: пересборка строки не сошлась (оставлен только путь exe)")
        for arg, rule in SHIP_BANNED:
            key = arg.split("=")[0]
            if key.lower() in cmd.lower():
                ok = False
                lines.append("ОШИБКА: в строке остался %s" % key)
            if rule.lower() not in [r.lower() for r in removed]:
                ok = False
                lines.append("ОШИБКА: правило %s не сработало (%s)" % (rule, arg))
        for arg in SHIP_KEPT:
            if arg not in cmd:
                ok = False
                lines.append("ОШИБКА: безобидный ключ %s пропал" % arg)
        if len(removed) != len(SHIP_BANNED):
            ok = False
            lines.append("ОШИБКА: убрано %d аргументов, ожидалось %d" % (len(removed), len(SHIP_BANNED)))
        return ok, lines
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# ── 3. иконка в ресурсах exe ───────────────────────────────────────────────────────────────────────────────────
def exe_icons(exe):
    """Картинки RT_ICON из exe по группам RT_GROUP_ICON. → [(имя группы, [байты картинок])] в порядке ресурсов."""
    from ctypes import wintypes
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.LoadLibraryExW.restype = wintypes.HMODULE
    k32.LoadLibraryExW.argtypes = (wintypes.LPCWSTR, wintypes.HANDLE, wintypes.DWORD)
    k32.FindResourceW.restype = wintypes.HRSRC
    k32.FindResourceW.argtypes = (wintypes.HMODULE, ctypes.c_void_p, ctypes.c_void_p)
    k32.LoadResource.restype = wintypes.HGLOBAL
    k32.LoadResource.argtypes = (wintypes.HMODULE, wintypes.HRSRC)
    k32.LockResource.restype = ctypes.c_void_p
    k32.LockResource.argtypes = (wintypes.HGLOBAL,)
    k32.SizeofResource.restype = wintypes.DWORD
    k32.SizeofResource.argtypes = (wintypes.HMODULE, wintypes.HRSRC)
    k32.FreeLibrary.argtypes = (wintypes.HMODULE,)
    EnumProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HMODULE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p)
    k32.EnumResourceNamesW.argtypes = (wintypes.HMODULE, ctypes.c_void_p, EnumProc, ctypes.c_void_p)
    RT_ICON, RT_GROUP_ICON = 3, 14
    h = k32.LoadLibraryExW(exe, None, 0x00000002 | 0x00000020)   # LOAD_LIBRARY_AS_DATAFILE | AS_IMAGE_RESOURCE
    if not h:
        raise OSError("LoadLibraryEx %s: %d" % (exe, ctypes.get_last_error()))

    def read(name, typ):
        r = k32.FindResourceW(h, name, typ)
        if not r:
            return None
        g = k32.LoadResource(h, r)
        return ctypes.string_at(k32.LockResource(g), k32.SizeofResource(h, r))

    groups = []
    try:
        names = []

        def cb(hm, typ, name, lp):
            names.append(name if name >> 16 else int(name))       # MAKEINTRESOURCE — число; строку берём адресом
            return True
        k32.EnumResourceNamesW(h, RT_GROUP_ICON, EnumProc(cb), None)
        for n in names:
            data = read(n, RT_GROUP_ICON)
            cnt = int.from_bytes(data[4:6], "little")
            imgs = []
            for i in range(cnt):
                ent = data[6 + 14 * i: 6 + 14 * (i + 1)]
                icon_id = int.from_bytes(ent[12:14], "little")
                imgs.append(read(icon_id, RT_ICON))
            groups.append((n if isinstance(n, int) else "str", imgs))
    finally:
        k32.FreeLibrary(h)
    return groups


def exe_icon_check(exe, ico):
    """Первая группа иконок exe (её показывает Проводник) = все картинки ico. → (ok, описание)."""
    sys.path.insert(0, HERE)
    import make_app_icon as MI
    want = [blob for _, _, blob in MI.parse_ico(open(ico, "rb").read())]
    groups = exe_icons(exe)
    if not groups:
        return False, "в exe нет RT_GROUP_ICON"
    gname, imgs = groups[0]
    if sorted(imgs) == sorted(want):
        return True, "группа %s (первая из %d): %d картинок = Application.ico байт в байт" % (gname, len(groups), len(imgs))
    match = sum(1 for b in want if b in imgs)
    return False, "группа %s: совпало %d из %d картинок Application.ico" % (gname, match, len(want))


# ── --dev: два прогона Development ─────────────────────────────────────────────────────────────────────────────
USER_ENGINE_INI = """[SystemSettings]
r.OneFrameThreadLag=0

[ConsoleVariables]
r.SceneRenderTargetResizeMethod=2

[AsdaRetailGuardTest]
Marker=1
"""


def _dev_run(out_dir, name, guard, timeout=420.0):
    """Один прогон -game. → словарь наблюдений RETAILGUARD-TEST."""
    import asda_pie_harness as PH
    suffix = "rg_%s_%d" % (name, os.getpid())
    saved = os.path.join(PROJECT_DIR, "Saved_" + suffix)
    shutil.rmtree(saved, ignore_errors=True)
    for plat in ("Windows", "WindowsEditor"):
        d = os.path.join(saved, "Config", plat)
        os.makedirs(d, exist_ok=True)
        io.open(os.path.join(d, "Engine.ini"), "w", encoding="utf-8", newline="\r\n").write(USER_ENGINE_INI)
    log = os.path.join(out_dir, "%s.log" % name)
    if os.path.exists(log):
        os.remove(log)
    cmd = [UE_GAME, UPROJECT, LOGIN_MAP, "-game", "-AsdaNoNet", "-RenderOffscreen", "-unattended", "-nopause", "-nosplash",
           "-nosound", "-AsdaLang=ru", "-saveddirsuffix=%s" % suffix, "-abslog=%s" % log, "-AsdaRetailGuardTest",
           "-ExecCmds=asda.RetailGuard.Probe execcmds", "-DP=Windows", "-UseFixedTimeStep", "-FPS=60"]
    if guard:
        cmd.append("-AsdaRetailGuard")
    print("[%s] %s" % (name, subprocess.list2cmdline(cmd)), flush=True)
    p = PH.popen_hidden(cmd)
    t0 = time.time()
    try:
        while p.poll() is None and time.time() - t0 < timeout:
            time.sleep(2)
            if "RETAILGUARD-TEST ИТОГ" in _read(log):
                deadline = time.time() + 30
                while p.poll() is None and time.time() < deadline:
                    time.sleep(1)
                break
    finally:
        if p.poll() is None:
            PH.kill_tree(p.pid)
        time.sleep(2)
        shutil.rmtree(saved, ignore_errors=True)
    facts = {}
    for m in re.finditer(r"RETAILGUARD-TEST (\w+)=(.*)", _read(log)):
        facts.setdefault(m.group(1), []).append(m.group(2).strip())
    facts["_probe_lines"] = re.findall(r"RETAILGUARD-PROBE-RAN.*", _read(log))
    facts["_done"] = "RETAILGUARD-TEST ИТОГ" in _read(log)
    print("[%s] прогон %.0f с, итог %s" % (name, time.time() - t0, "есть" if facts["_done"] else "НЕТ"), flush=True)
    return facts


def _read(path):
    """Текст лога (или пусто)."""
    try:
        return io.open(path, encoding="utf-8", errors="replace").read()
    except OSError:
        return ""


def dev_check(out_dir):
    """Вердикт по паре прогонов. → код возврата (0 — всё верно)."""
    os.makedirs(out_dir, exist_ok=True)
    g = _dev_run(out_dir, "guard", True)
    c = _dev_run(out_dir, "control", False)
    f = lambda d, k: (d.get(k) or ["?"])[0]
    checks = [
        ("оба прогона дошли до итога", g["_done"] and c["_done"]),
        ("контроль: -ExecCmds выполнил пробу", f(c, "probe_runs_execcmds") != "0"),
        ("защита: -ExecCmds НЕ выполнил пробу", f(g, "probe_runs_execcmds") == "0"),
        ("контроль: Exec пробы прошёл", f(c, "probe_runs_exec_path") == "1"),
        ("защита: Exec пробы отклонён", f(g, "probe_runs_exec_path") == "0" and f(g, "exec_blocked_delta") == "1"),
        ("фильтр: quit и cvar — да; open/show/«cvar|stat»/проба — нет",
         (f(g, "allow_quit"), f(g, "allow_cvar"), f(g, "allow_open"), f(g, "allow_show"), f(g, "allow_pipe"),
          f(g, "allow_probe")) == ("1", "1", "0", "0", "0", "0")),
        ("контроль: ключи -ExecCmds/-DP=/-UseFixedTimeStep/-FPS= в FCommandLine есть",
         (f(c, "cmd_execcmds"), f(c, "cmd_dp"), f(c, "cmd_fixedstep"), f(c, "cmd_fps")) == ("1", "1", "1", "1")),
        ("защита: этих ключей в FCommandLine нет, -AsdaNoNet остался",
         (f(g, "cmd_execcmds"), f(g, "cmd_dp"), f(g, "cmd_fixedstep"), f(g, "cmd_fps"), f(g, "cmd_keep_nonet"))
         == ("0", "0", "0", "0", "1")),
        ("контроль: фиксированный шаг включён; защита: выключен", f(c, "fixed_timestep") == "1" and f(g, "fixed_timestep") == "0"),
        ("контроль: пользовательский [SystemSettings] применён (r.OneFrameThreadLag=0)", f(c, "cvar_systemsettings") == "0"),
        ("защита: [SystemSettings] не применён (r.OneFrameThreadLag=1)", f(g, "cvar_systemsettings") == "1"),
        ("контроль: пользовательский [ConsoleVariables] применён (=2)", f(c, "cvar_consolevariables") == "2"),
        ("защита: [ConsoleVariables] не применён (=0)", f(g, "cvar_consolevariables") == "0"),
        ("контроль: своя секция ini видна; защита: нет",
         f(c, "ini_marker_key") == "1" and f(g, "ini_marker_key") == "нет" and f(g, "ini_systemsettings_key") == "нет"),
        ("контроль: консоль вьюпорта есть; защита: снята", f(c, "viewport_console") == "1" and f(g, "viewport_console") == "0"),
    ]
    bad = 0
    print("\nпроверка\tитог")
    for name, ok in checks:
        bad += 0 if ok else 1
        print("%s\t%s" % (name, "ОК" if ok else "ПРОВАЛ"))
    print("\nзащита, журнал старта:")
    for s in g.get("startup", []):
        print("  " + s)
    print("строки пробы: защита %d, контроль %d" % (len(g["_probe_lines"]), len(c["_probe_lines"])))
    print("\nDEV-ПРОВЕРКА: %s (%d из %d)" % ("ВСЁ ВЕРНО" if not bad else "ЕСТЬ ПРОВАЛЫ", len(checks) - bad, len(checks)))
    return 0 if not bad else 1


def shipping_all(exe, ico=None, log=print):
    """Проверки 1–3 над Shipping-exe. → число нарушений."""
    errs = 0
    mentions, kinds = {}, {}
    log_formats = mention_literals()
    hits, total = dev_names_in_exe(exe, log_formats=log_formats, mentions=mentions, kinds=kinds)
    for h in hits:
        log("  ДЕВ-ИМЯ в exe: %s" % h)
    log("  имена дев-команд/ключей из Source: %d, найдено в exe вне строк-упоминаний: %d" % (total, len(hits)))
    # ★ 19.10.2026: упоминания внутри строк журнала/текста (запечатанный журнал Shipping) — не нарушение, но видны в отчёте.
    log("  упоминаний имён: %d имён, %d вхождений (в строках журнала %d, в прочих строках кода %d)%s" % (
        len(mentions), sum(mentions.values()), kinds.get(KIND_LOG, 0), kinds.get(KIND_TEXT, 0),
        (" — " + ", ".join(sorted(mentions))) if mentions else ""))
    errs += len(hits)
    has_tag = tag_in_exe(exe)
    log("  метка %s в exe: %s" % (TAG, "есть" if has_tag else "НЕТ"))
    errs += 0 if has_tag else 1
    ok, lines = shipping_selfcheck(exe)
    for l in lines:
        log("  самопроверка: " + l)
    log("  самопроверка очистки командной строки: %s" % ("ОК" if ok else "ПРОВАЛ"))
    errs += 0 if ok else 1
    if ico:
        ok, why = exe_icon_check(exe, ico)
        log("  иконка в ресурсах exe: %s — %s" % ("ОК" if ok else "ПРОВАЛ", why))
        errs += 0 if ok else 1
    return errs


def main(argv=None):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass
    ap = argparse.ArgumentParser(description="Проверки ритейл-защиты клиента")
    ap.add_argument("--shipping", help="Shipping-exe: дев-имена, метка, самопроверка очистки, иконка")
    ap.add_argument("--ico", default=os.path.join(PROJECT_DIR, "Build", "Windows", "Application.ico"), help="ico для сверки")
    ap.add_argument("--dev", action="store_true", help="два прогона Development (-game): защита и контроль")
    ap.add_argument("--out", default=os.path.join(PROJECT_DIR, "Saved", "RetailGuardCheck"), help="папка логов --dev")
    a = ap.parse_args(argv)
    sys.path.insert(0, HERE)
    rc = 0
    if a.shipping:
        e = shipping_all(a.shipping, a.ico)
        print("SHIPPING-ПРОВЕРКА: %s (%d нарушений)" % ("ВСЁ ВЕРНО" if not e else "ЕСТЬ НАРУШЕНИЯ", e))
        rc |= 1 if e else 0
    if a.dev:
        rc |= dev_check(a.out)
    if not (a.shipping or a.dev):
        ap.error("нужен --shipping <exe> и/или --dev")
    return rc


if __name__ == "__main__":
    sys.exit(main())
