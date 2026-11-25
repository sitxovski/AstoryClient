# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_settings_registry.py — ★ РЕЕСТР НАСТРОЕК И КОНСОЛЬНЫХ КЛЮЧЕЙ, режим ПРОВЕРКИ (без генерации кода).
#   Волна 1 п.5 аудита дублей (Reports/dedup_audit/00_PLAN.md: E12, E13; 04_cross_layer.md: D1, D2, D7, S2), 14.09.2026.
#   Обычный python, без UE. Закон tools-no-import-side-effects: при импорте НИЧЕГО не делает, всё — из main().
#   Ничего не пишет в проект: читает C++ (Source/, Plugins/), Config/*.ini, пины сборки владельца (GameUserSettings.ini) и,
#   по флагу --engine, исходники движка. Пишет только отчёт (--report) и дамп (--json) в Reports/dedup_audit/.
#
#   python Tools/asda_settings_registry.py --check            — сверка; код 0 = нет ошибок/предупреждений, 3 = есть
#   python Tools/asda_settings_registry.py --report           — + отчёт Reports/dedup_audit/settings_registry.md
#   python Tools/asda_settings_registry.py --json             — + машинный дамп Reports/dedup_audit/settings_registry.json
#   python Tools/asda_settings_registry.py --engine ...       — дополнительно искать r.* ключи в исходниках движка (медленно, ~минута)
#   Флаги можно сочетать: --check --report --json --engine.  Пути: --report-out PATH, --json-out PATH, --engine-root PATH.
#
#   ПРОВЕРКИ (буквы — из задания волны 1):
#     (а) ключ в DefaultEngine.ini отличается от умолчания кода, а справка/комментарий называют прежнее умолчание (E12/D2);
#         + комментарии и запасные значения меню, называющие не то значение ключа, что стоит в проекте (D1)
#     (б) закон E-0b: ключ в DefaultEngine.ini задан и одновременно раздаётся уровнями Scalability (BS/DS) — глушит уровни;
#         asda.*-ключ в пер-уровневой секции DS без ECVF_Scalability — движок молча отвергнет
#     (в) дубли регистрации консольных имён (регистр не важен; у команд выживает последняя, у переменных первая)
#     (г) ключ в ini, которого нет в коде (опечатка/мёртвый); ссылки C++ на незарегистрированные asda.*; «тени» в Config/
#     (д) пины GameUserSettings.ini (сборка владельца и редактор), перекрывающие уровни качества
#     (е) межслойные пути к серверу (E13) — отчётом
#     (ж) умолчания C++ классов настроек (UDeveloperSettings) против данных ini/JSON и наличие канарейки ConfigStamp (D7)
#
#   Метод и границы (false-negatives-method): имена ищутся по строковым литералам регистраций; имя, собранное из переменных,
#   попадает в «неразобранные» (счётчик в отчёте), а не в «нет в коде». Движковые r.*/sg.* без --engine не проверяются на
#   существование. Вывод «нет» по движку — только «литерала регистрации не найдено в просмотренных каталогах».
import os
import re
import sys
import json
import math
import bisect
import argparse
import datetime
import subprocess
import collections

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))                 # Tools/ проекта
PROJ = os.path.dirname(TOOLS_DIR)                                       # AstoryClient
AREA = os.path.dirname(PROJ)                                            # @@ASTORY_DEV_ROOT_FWD@@ (<ROOT>)
CONFIG_DIR = os.path.join(PROJ, "Config")                               # проектные ini
DE_INI = os.path.join(CONFIG_DIR, "DefaultEngine.ini")                  # [SystemSettings] + RendererSettings
DS_INI = os.path.join(CONFIG_DIR, "DefaultScalability.ini")             # проектные уровни качества
DG_INI = os.path.join(CONFIG_DIR, "DefaultGame.ini")                    # секции UDeveloperSettings
DP_INI = os.path.join(CONFIG_DIR, "DefaultDeviceProfiles.ini")          # профили устройств (если появятся)
GUS_BUILD = os.path.join(AREA, "BUILD", "Windows", "AsdaReborn", "Saved", "Config", "Windows", "GameUserSettings.ini")  # пины владельца
GUS_EDITOR = os.path.join(PROJ, "Saved", "Config", "WindowsEditor", "GameUserSettings.ini")  # общий GUS редактора (агенты)
LIGHT_PRESETS_TOOL = os.path.join(TOOLS_DIR, "asda_gen_render_settings_ini.py")               # генератор D7 (зовём ТОЛЬКО с --check)
DEFAULT_ENGINE_ROOT = os.environ.get("ASDA_UE_ROOT") or os.environ.get("ASDA_UE_DIR") or r"D:\Games\UE_5.8"                     # движок UE 5.8
REPORT_DIR = os.path.join(AREA, "Reports", "dedup_audit")                             # каталог отчёта аудита
REPORT_MD = os.path.join(REPORT_DIR, "settings_registry.md")                                 # отчёт по умолчанию
REPORT_JSON = os.path.join(REPORT_DIR, "settings_registry.json")                             # дамп по умолчанию

SEV_ERR, SEV_WARN, SEV_INFO = "ОШИБКА", "ПРЕДУПРЕЖДЕНИЕ", "СВЕДЕНИЯ"                          # серьёзность находки
WHO_OWNER, WHO_TEXT, WHO_CODE = "владелец", "текст", "код"                                  # кто решает / что править
SEV_ORDER = {SEV_ERR: 0, SEV_WARN: 1, SEV_INFO: 2}                                          # порядок сортировки
SKIP_DIRS = {"Intermediate", "Binaries", "_backup", "Saved", "DerivedDataCache", "__pycache__", ".git"}  # не сканировать

# ------------------------------------------------------------------------------------------------ лексика C++

LIT_RE = re.compile(r'"((?:[^"\\\n]|\\.)*)"')                                # строковый литерал C++
REG_RE = re.compile(r'\b(TAutoConsoleVariable\s*<\s*([^<>;]+?)\s*>|FAutoConsole\w+|RegisterConsole\w+)\s*(\w+)?\s*([({])')  # регистрация
NUM_RE = re.compile(r'^[-+]?(?:\d+\.?\d*|\.\d+)(?:e[-+]?\d+)?f?$', re.I)     # число C++/ini
DEFAULT_HINT_RES = [                                                          # фразы «умолчание = N» в справке/комментарии
    re.compile(r'(?:по\s+умолчанию|дефолт\w*|default)\s*[:=(]?\s*([-+]?\d+(?:[.,]\d+)?)', re.I),
    re.compile(r'([-+]?\d+(?:[.,]\d+)?)\s*f?\s*(?:=|—|-)\s*[^;,)]{0,40}?(?:как\s+было|как\s+раньше|по\s+умолчанию|дефолт|значения\s+конфига|как\s+в\s+акторе)', re.I),
    re.compile(r'([-+]?\d+(?:[.,]\d+)?)\s*\(\s*(?:по\s+умолчанию|дефолт)', re.I),
]


def blank_comments(src):
    """Код с комментариями, заменёнными пробелами (переводы строк и литералы сохранены), и словарь строка→текст комментариев."""
    out = list(src)
    comments = collections.defaultdict(str)
    i, n, line = 0, len(src), 1
    while i < n:
        c = src[i]
        if c == "\n":
            line += 1
            i += 1
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            j = src.find("\n", i)
            j = n if j < 0 else j
            comments[line] += src[i + 2:j]
            for k in range(i, j):
                out[k] = " "
            i = j
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "*":
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            seg = src[i:j]
            for off, part in enumerate(seg.split("\n")):
                comments[line + off] += part
            for k in range(i, j):
                if src[k] != "\n":
                    out[k] = " "
            line += seg.count("\n")
            i = j
            continue
        if c == '"' or (c == "'" and not (i > 0 and src[i - 1].isalnum())):
            j = i + 1
            while j < n and src[j] != c and src[j] != "\n":
                if src[j] == "\\":
                    j += 1
                j += 1
            i = j + 1 if (j < n and src[j] == c) else j
            continue
        i += 1
    return "".join(out), comments


def line_index(text):
    """Отсортированные смещения переводов строк — для offset→номер строки."""
    return [m.start() for m in re.finditer("\n", text)]


def line_of(idx, offset):
    """Номер строки (с 1) по смещению."""
    return bisect.bisect_right(idx, offset - 1) + 1 if idx else 1


def split_args(code, open_idx):
    """Аргументы верхнего уровня вызова от открывающей скобки; (список_строк, индекс_закрывающей)."""
    depth, start, i, n = 0, open_idx + 1, open_idx, len(code)
    args = []
    while i < n:
        c = code[i]
        if c == '"':
            j = i + 1
            while j < n and code[j] != '"' and code[j] != "\n":
                if code[j] == "\\":
                    j += 1
                j += 1
            i = j
        elif c in "({[":
            depth += 1
        elif c in ")}]":
            depth -= 1
            if depth == 0:
                args.append(code[start:i])
                return args, i
        elif c == "," and depth == 1:
            args.append(code[start:i])
            start = i + 1
        i += 1
    return args, n


def literal_text(arg):
    """Склейка всех строковых литералов аргумента (TEXT("a") TEXT("b") → ab)."""
    return "".join(LIT_RE.findall(arg or "")).replace('\\"', '"').replace("\\n", " ")


def is_pure_literal(arg):
    """Аргумент — только строковые литералы (возможно в TEXT())."""
    rest = LIT_RE.sub("", arg or "")
    return bool(LIT_RE.search(arg or "")) and re.fullmatch(r'[\sTEXT()]*', rest) is not None


def to_num(v):
    """Число из значения C++/ini (1.f, True, 0.5f, «1 ; комментарий»); None, если не число."""
    if v is None:
        return None
    s = str(v).split(";")[0].strip().strip('"').strip()
    s = re.sub(r'^\(\s*\w+\s*\)\s*', "", s)
    s = re.sub(r'^static_cast<\w+>\((.*)\)$', r"\1", s)
    low = s.lower()
    if low in ("true", "false"):
        return 1.0 if low == "true" else 0.0
    if NUM_RE.match(low):
        try:
            return float(low.rstrip("f") if not low.endswith(".f") else low[:-1])
        except ValueError:
            return None
    return None


def same_val(a, b):
    """Равенство значений: числа с допуском, иначе строки без регистра и префиксов перечислений."""
    na, nb = to_num(a), to_num(b)
    if na is not None and nb is not None:
        return math.isclose(na, nb, rel_tol=1e-5, abs_tol=1e-6)
    norm = lambda x: re.sub(r'^\w+::', "", str(x).split(";")[0].strip().strip('"')).lower()
    return norm(a) == norm(b)


def fmt(v):
    """Короткая печать значения для таблиц."""
    if v is None:
        return "—"
    s = str(v).split(";")[0].strip()
    return s if len(s) <= 40 else s[:37] + "…"


def walk_files(roots, exts):
    """Файлы с расширениями exts под корнями, без служебных каталогов и .bak-копий."""
    for root in roots:
        if not os.path.isdir(root):
            continue
        for dp, dn, fn in os.walk(root):
            dn[:] = [d for d in dn if d not in SKIP_DIRS]
            for f in fn:
                if f.endswith(exts) and ".bak" not in f.lower():
                    yield os.path.join(dp, f)


def read_text(path):
    """Текст файла с учётом BOM (UTF-8/UTF-16)."""
    with open(path, "rb") as fh:
        raw = fh.read()
    if raw.startswith(b"\xff\xfe") or raw.startswith(b"\xfe\xff"):
        return raw.decode("utf-16", errors="replace")
    return raw.decode("utf-8-sig", errors="replace")


def rel(path, base=PROJ):
    """Путь относительно проекта (для отчёта)."""
    try:
        return os.path.relpath(path, base).replace("\\", "/")
    except ValueError:
        return path.replace("\\", "/")


# ------------------------------------------------------------------------------------------------ сбор: C++

def scan_registrations(path, text=None, want=None):
    """Регистрации консольных имён в файле C++. want — множество имён (нижний регистр) для быстрого фильтра движка."""
    src = text if text is not None else read_text(path)
    code, comments = blank_comments(src)
    idx = line_index(code)
    regs, unparsed = [], []
    for m in REG_RE.finditer(code):
        kw = m.group(1)
        if kw.endswith(("Sink", "Delegate")) or "Sink" in kw or "Handle" in kw:
            continue
        args, _ = split_args(code, m.start(4))
        ln = line_of(idx, m.start())
        if not args or not args[0].strip():
            continue
        if not is_pure_literal(args[0]):
            unparsed.append({"file": path, "line": ln, "kw": kw.split("<")[0], "arg0": args[0].strip()[:60]})
            continue
        name = literal_text(args[0]).strip()
        if want is not None and name.lower() not in want:
            continue
        kind = "cmd" if "Command" in kw else "var"
        reg = {"name": name, "kind": kind, "api": re.sub(r"\s+", "", kw), "file": path, "line": ln,
               "default": None, "default_raw": None, "default_line": None, "default_comment": "",
               "help": "", "flags": [], "value_type": (m.group(2) or "").strip()}
        if kind == "var":
            dflt = args[1].strip() if len(args) > 1 else ""
            reg["help"] = literal_text(args[2]) if len(args) > 2 else ""
            reg["flags"] = sorted(set(re.findall(r'ECVF_\w+', " ".join(args[3:]))))
            if kw.endswith("Ref"):
                vm = re.search(r'\b(?:static\s+|const\s+|inline\s+|constexpr\s+)*(?:int32|int|float|double|bool|uint32|uint8|float32|FString)\s+'
                               + re.escape(dflt) + r'\s*(?:=\s*([^;{]+)|\{([^}]*)\})\s*;', code)
                reg["default_raw"] = (vm.group(1) or vm.group(2) or "").strip() if vm else "&" + dflt
                if vm:
                    dl = line_of(idx, vm.start())
                    reg["default_line"] = dl
                    reg["default_comment"] = comments.get(dl, "").strip()
            else:
                reg["default_raw"] = dflt
                reg["default_line"] = ln
            reg["default"] = to_num(reg["default_raw"])
        else:
            reg["help"] = literal_text(args[1]) if len(args) > 1 else ""
            reg["flags"] = sorted(set(re.findall(r'ECVF_\w+', " ".join(args[2:]))))
        regs.append(reg)
    return regs, unparsed, src, code, comments


def collect_cpp(project_roots):
    """Все регистрации проекта + литеральные ссылки на r.*/sg.*/asda.* + ключи GConfig + запасные значения меню."""
    regs, unparsed = [], []
    refs = collections.defaultdict(list)            # имя(нижн.) → [(файл, строка, контекст)]
    gconfig_keys = set()                             # ключи, которые код читает как ДАННЫЕ GConfig (не CVar)
    fallbacks = []                                   # GetCVarInt/Float(TEXT("k"), N) и AddSlider(TEXT("k"), min, max, def)
    comment_lines = []                               # (файл, строка, текст) — для (а) «комментарий называет не то значение»
    for path in walk_files(project_roots, (".cpp", ".h")):
        r, u, src, code, comments = scan_registrations(path)
        regs += r
        unparsed += u
        reg_lines = {(x["line"]) for x in r}
        idx = line_index(src)
        lines = src.split("\n")
        for m in re.finditer(r'"((?:asda|Asda|r|sg)\.[A-Za-z0-9_.]+)(?:\s[^"\n]*)?"', src):
            ln = line_of(idx, m.start())
            ctx = lines[ln - 1].strip() if ln - 1 < len(lines) else ""
            if code[m.start()] == " ":            # литерал внутри комментария
                continue
            if "GConfig" in ctx:
                gconfig_keys.add(m.group(1).lower())
                continue
            near = " ".join(comments.get(x, "") for x in range(ln - 15, ln + 2))   # пометка «ключа пока нет» в комментарии над обращением
            documented = re.search(re.escape(m.group(1)) + r'[^\n]{0,60}?(?:не\s+существует|пока\s+нет|ещё\s+нет|не\s+зарегистр)', near, re.I) is not None
            refs[m.group(1).lower()].append((path, ln, ctx[:140], ln in reg_lines, documented))
            cm = re.search(r'сейчас\s+([-+]?\d+(?:\.\d+)?)', comments.get(ln, ""))
            if cm and ln not in reg_lines:
                comment_lines.append((path, ln, "%s сейчас %s  [комментарий строки кода: %s]" % (m.group(1), cm.group(1), comments[ln].strip())))
        for m in re.finditer(r'\b(GetCVarInt|GetCVarFloat)\s*\(\s*TEXT\s*\(\s*"([^"]+)"\s*\)\s*,\s*([-+.\w]+)\s*\)', code):
            fallbacks.append({"key": m.group(2), "api": m.group(1), "value": m.group(3), "file": path, "line": line_of(idx, m.start())})
        for m in re.finditer(r'\bAddSlider\s*\(\s*TEXT\s*\(\s*"([^"]+)"\s*\)\s*,\s*([-+.\w]+)\s*,\s*([-+.\w]+)\s*,\s*([-+.\w]+)', code):
            fallbacks.append({"key": m.group(1), "api": "AddSlider(умолч.)", "value": m.group(4), "file": path, "line": line_of(idx, m.start())})
        for ln, txt in comments.items():
            if re.search(r'(?:\br\.|\basda\.|\bsg\.)', txt, re.I):
                comment_lines.append((path, ln, txt.strip()))
    return regs, unparsed, refs, gconfig_keys, fallbacks, comment_lines


# ------------------------------------------------------------------------------------------------ сбор: ini

def parse_ini(path):
    """Секции ini → список записей {key, value, line, op}; комментарии (;) — отдельным списком (строка, текст)."""
    secs = collections.OrderedDict()
    comments = []
    if not os.path.isfile(path):
        return None, comments
    cur = None
    for ln, raw in enumerate(read_text(path).split("\n"), 1):
        s = raw.strip()
        if not s:
            continue
        if s.startswith(";") or s.startswith("#"):
            comments.append((ln, s))
            continue
        m = re.match(r'^\[(.+)\]$', s)
        if m:
            cur = m.group(1).strip()
            secs.setdefault(cur, [])
            continue
        if cur is None or "=" not in s:
            continue
        k, v = s.split("=", 1)
        op = ""
        if k[:1] in "+-.!":
            op, k = k[0], k[1:]
        secs[cur].append({"key": k.strip(), "value": v.strip(), "line": ln, "op": op})
    return secs, comments


LEVEL_SEC_RE = re.compile(r'^(\w+)@(\d+|Cine)$')     # секция уровня: <Группа>@<0..3|Cine>
LEVEL_ORDER = ["0", "1", "2", "3", "Cine"]           # порядок уровней в таблицах


def level_sections(secs):
    """{группа: {уровень: {ключ_нижн: (ключ, значение, строка)}}} из секций <Группа>@<уровень>."""
    out = collections.defaultdict(lambda: collections.defaultdict(dict))
    for name, entries in (secs or {}).items():
        m = LEVEL_SEC_RE.match(name)
        if not m:
            continue
        for e in entries:
            out[m.group(1)][m.group(2)][e["key"].lower()] = (e["key"], e["value"], e["line"])
    return out


def level_for_sg(level):
    """Уровень sg.* (0..4) → имя секции (4 и выше = Cine)."""
    try:
        lv = int(float(level))
    except (TypeError, ValueError):
        return None
    return "Cine" if lv >= 4 else str(max(lv, 0))


# ------------------------------------------------------------------------------------------------ сбор: классы настроек (ж)

def find_block(code, start):
    """Индекс закрывающей } блока, начинающегося с первой { после start."""
    b = code.find("{", start)
    if b < 0:
        return -1, -1
    depth = 0
    for i in range(b, len(code)):
        if code[i] == "{":
            depth += 1
        elif code[i] == "}":
            depth -= 1
            if depth == 0:
                return b, i
    return b, len(code)


def parse_uproperties(code, b, e, idx):
    """UPROPERTY-поля блока: [{name, type, default, config, line}]."""
    fields = []
    for m in re.finditer(r'\bUPROPERTY\s*\(', code[b:e]):
        ab = b + m.end() - 1
        args, close = split_args(code, ab)
        dm = re.match(r'\s*([\w:<>,\s\*&]+?)\s+(\w+)\s*(?:=\s*([^;]+?))?\s*;', code[close + 1:close + 400])
        if not dm:
            continue
        flags_txt = LIT_RE.sub("", ",".join(args))
        fields.append({"name": dm.group(2), "type": re.sub(r"\s+", " ", dm.group(1)).strip(),
                       "default": (dm.group(3) or "").strip() or None,
                       "config": re.search(r'\b(?:config|GlobalConfig)\b', flags_txt) is not None,
                       "line": line_of(idx, ab)})
    return fields


def collect_settings_classes(project_roots):
    """UCLASS(config=…) классы и все USTRUCT с полями — для сверки умолчаний C++ с ini (ж)."""
    classes, structs = [], {}
    for path in walk_files(project_roots, (".h",)):
        src = read_text(path)
        if "UPROPERTY" not in src:
            continue
        code, _ = blank_comments(src)
        idx = line_index(code)
        for m in re.finditer(r'\bUSTRUCT\s*\([^)]*\)\s*struct\s+(?:\w+_API\s+)?(F\w+)', code):
            b, e = find_block(code, m.end())
            if b >= 0:
                structs[m.group(1)] = {"file": path, "fields": parse_uproperties(code, b, e, idx)}
        for m in re.finditer(r'\bUCLASS\s*\(', code):
            args, close = split_args(code, m.end() - 1)
            cfg = re.search(r'\bconfig\s*=\s*(\w+)', LIT_RE.sub("", ",".join(args)), re.I)
            cm = re.match(r'\s*class\s+(?:\w+_API\s+)?U(\w+)\s*(?:final\s*)?:', code[close + 1:close + 300])
            if not cfg or not cm:
                continue
            b, e = find_block(code, close)
            parts = path.replace("\\", "/").split("/")
            module = parts[parts.index("Source") + 1] if "Source" in parts else "?"
            classes.append({"class": "U" + cm.group(1), "section": "/Script/%s.%s" % (module, cm.group(1)),
                            "config": cfg.group(1), "file": path, "line": line_of(idx, m.start()),
                            "fields": parse_uproperties(code, b, e, idx)})
    return classes, structs


def parse_ue_text(s):
    """Текст значения UE (ImportText): «(A=1,B=(C=2))» → dict, «((K,(…)),(…))» → list, прочее → строка."""
    s = s.strip()
    pos = [0]

    def skip_ws():
        while pos[0] < len(s) and s[pos[0]] in " \t":
            pos[0] += 1

    def parse_scalar():
        skip_ws()
        if pos[0] < len(s) and s[pos[0]] == '"':
            j = pos[0] + 1
            while j < len(s) and s[j] != '"':
                j += 2 if s[j] == "\\" else 1
            v = s[pos[0] + 1:j]
            pos[0] = j + 1
            return v
        j = pos[0]
        depth = 0
        while j < len(s):
            c = s[j]
            if c == "(":
                depth += 1
            elif c == ")":
                if depth == 0:
                    break
                depth -= 1
            elif c == "," and depth == 0:
                break
            j += 1
        v = s[pos[0]:j].strip()
        pos[0] = j
        return v

    def parse_value():
        skip_ws()
        if pos[0] < len(s) and s[pos[0]] == "(":
            pos[0] += 1
            items, as_dict = [], collections.OrderedDict()
            is_dict = None
            while True:
                skip_ws()
                if pos[0] >= len(s):
                    break
                if s[pos[0]] == ")":
                    pos[0] += 1
                    break
                km = re.match(r'([A-Za-z_]\w*)\s*=', s[pos[0]:])
                if km and s[pos[0]] != "(":
                    pos[0] += km.end()
                    as_dict[km.group(1)] = parse_value()
                    is_dict = True if is_dict is None else is_dict
                else:
                    items.append(parse_value())
                    is_dict = False if is_dict is None else is_dict
                skip_ws()
                if pos[0] < len(s) and s[pos[0]] == ",":
                    pos[0] += 1
            return as_dict if is_dict else items
        return parse_scalar()

    try:
        return parse_value()
    except Exception:          # разбор данных — не повод падать; вернём как строку
        return s


def compare_struct(value, struct_name, structs, path, out, depth=0):
    """Поля значения ini против умолчаний USTRUCT (рекурсивно по вложенным структурам)."""
    st = structs.get(struct_name)
    if not st or not isinstance(value, dict) or depth > 4:
        return
    for f in st["fields"]:
        if f["name"] not in value:
            continue
        v = value[f["name"]]
        inner = re.sub(r'^(?:TArray|TMap|TSet)<.*?(F\w+)>$', r"\1", f["type"])
        if isinstance(v, dict) and inner in structs:
            compare_struct(v, inner, structs, path + "." + f["name"], out, depth + 1)
            continue
        d = f["default"]
        if d is None or isinstance(v, (dict, list)) or "(" in d:
            continue
        dn = re.sub(r'^TEXT\("(.*)"\)$', r"\1", d).strip('"')
        if not same_val(dn, v):
            out.append({"path": path + "." + f["name"], "field": f["name"], "cpp": dn, "ini": v,
                        "struct": struct_name, "file": st["file"], "line": f["line"]})


# ------------------------------------------------------------------------------------------------ сбор: пути сервера (е)

PATH_LIT_RE = re.compile(r'''(?:[rRuU]?)(["'])((?:\\\\\?\\UNC\\|\\\\\\\\|\\\\|//|[A-Za-z]:[\\/])(?:(?!\1).)*)\1''')   # абсолютный путь в литерале


def norm_server_path(p):
    """Нормализация пути: \\\\?\\UNC\\ → //, обратные → прямые, нижний регистр, без хвостового /."""
    q = p.replace("\\\\", "\\").replace("\\", "/")
    q = re.sub(r'^//\?/unc/', "//", q, flags=re.I)
    q = re.sub(r'(?<!^)/{2,}', "/", q)
    return q.rstrip("/").lower()


def gameserver_dir(p):
    """Каталог GameServer, на который указывает путь (корень без GameServer → корень/gameserver)."""
    i = p.find("/gameserver")
    return p[:i + len("/gameserver")] if i >= 0 else p + "/gameserver"


def collect_server_paths():
    """Абсолютные пути к серверу в C++ (Source/Plugins) и Tools/*.py рядом с упоминанием @@ASTORY_SERVER_HOSTNAME@@ (E13)."""
    found = []
    roots = [os.path.join(PROJ, "Source"), os.path.join(PROJ, "Plugins"), TOOLS_DIR]
    for path in walk_files(roots, (".cpp", ".h", ".py")):
        if path.endswith(".py") and os.path.dirname(path) != TOOLS_DIR:
            continue
        src = read_text(path)
        if not re.search(r'@@ASTORY_SERVER_HOSTNAME@@', src, re.I):
            continue
        is_cpp = path.endswith((".cpp", ".h"))
        code = blank_comments(src)[0] if is_cpp else src
        idx = line_index(src)
        anchor_lines = [line_of(idx, m.start()) for m in re.finditer(r'@@ASTORY_SERVER_HOSTNAME@@', code, re.I) if (not is_cpp or code[m.start()] != " ")]
        if not anchor_lines:
            continue
        for m in PATH_LIT_RE.finditer(code):
            if is_cpp and code[m.start()] == " ":
                continue
            ln = line_of(idx, m.start())
            if not any(abs(ln - a) <= 15 for a in anchor_lines):
                continue
            p = norm_server_path(m.group(2))
            if not re.search(r'@@ASTORY_SERVER_HOSTNAME@@|Astory', p):
                continue
            found.append({"file": path, "line": ln, "raw": m.group(2), "norm": p, "gameserver": gameserver_dir(p)})
    paths_py = os.path.join(TOOLS_DIR, "asda_paths.py")
    root_line = None
    if os.path.isfile(paths_py):
        for ln, t in enumerate(read_text(paths_py).split("\n"), 1):
            if re.match(r'\s*(ROOT|GAMESERVER)\s*=', t):
                root_line = (root_line or []) + [(ln, t.strip())]
    return found, root_line


# ------------------------------------------------------------------------------------------------ движок (--engine)

def scan_engine(engine_root, names):
    """Регистрации нужных r.*/sg.* имён в исходниках движка: {имя_нижн: [рег]}. Быстрый фильтр по подстроке."""
    want = {n.lower() for n in names}
    if not want:
        return {}, 0
    probe = re.compile(r'"(?:r|sg|fx|foliage|grass)\.[A-Za-z0-9_.]+"')
    found = collections.defaultdict(list)
    scanned = 0
    roots = [os.path.join(engine_root, "Engine", "Source", "Runtime"), os.path.join(engine_root, "Engine", "Plugins")]
    for path in walk_files(roots, (".cpp",)):
        try:
            with open(path, "r", encoding="utf-8", errors="ignore") as fh:
                src = fh.read()
        except OSError:
            continue
        scanned += 1
        lits = {x[1:-1].lower() for x in probe.findall(src)}
        if not (lits & want):
            continue
        regs = scan_registrations(path, text=src, want=want)[0]
        for r in regs:
            found[r["name"].lower()].append(r)
    return found, scanned


# ------------------------------------------------------------------------------------------------ проверки

class Findings:
    """Накопитель находок."""

    def __init__(self):
        self.items = []           # список находок

    def add(self, check, sev, who, key, where, text, action):
        """Добавить находку: буква проверки, серьёзность, кто решает, ключ, место, суть, предложенное действие."""
        self.items.append({"check": check, "severity": sev, "who": who, "key": key, "where": where, "text": text, "action": action})

    def sorted(self):
        """Находки по серьёзности, проверке, ключу."""
        return sorted(self.items, key=lambda f: (SEV_ORDER[f["severity"]], f["check"], f["key"].lower()))


def build_registry(args):
    """Собрать все слои и выполнить проверки. Возвращает словарь состояния (он же уходит в --json)."""
    proj_roots = [os.path.join(PROJ, "Source"), os.path.join(PROJ, "Plugins")]
    F = Findings()

    regs, unparsed, refs, gconfig_keys, fallbacks, cpp_comment_lines = collect_cpp(proj_roots)
    by_name = collections.defaultdict(list)
    for r in regs:
        by_name[r["name"].lower()].append(r)

    de, de_comments = parse_ini(DE_INI)
    ds, ds_comments = parse_ini(DS_INI)
    dg, _ = parse_ini(DG_INI)
    dp, _ = parse_ini(DP_INI)
    gus_b, _ = parse_ini(GUS_BUILD)
    gus_e, _ = parse_ini(GUS_EDITOR)
    engine_root = args.engine_root or DEFAULT_ENGINE_ROOT
    bs, _ = parse_ini(os.path.join(engine_root, "Engine", "Config", "BaseScalability.ini"))

    ds_levels, bs_levels = level_sections(ds), level_sections(bs)
    engine_groups = set(bs_levels.keys())
    ds_engine = {g: v for g, v in ds_levels.items() if g in engine_groups}
    ds_custom = {g: v for g, v in ds_levels.items() if g not in engine_groups}

    def level_values(key_l):
        """Пер-уровневые значения ключа: [(источник, группа, уровень, значение, строка)] — сначала DS (перекрывает BS)."""
        out = []
        for src_name, lv in (("DS", ds_engine), ("BS", bs_levels)):
            for g, levels in lv.items():
                for L, kv in levels.items():
                    if key_l in kv:
                        out.append((src_name, g, L, kv[key_l][1], kv[key_l][2]))
        return out

    # ---- слои ключей (таблица «ключ × слой»)
    layers = collections.defaultdict(dict)       # имя_нижн → {слой: (значение, строка)}
    display = {}                                  # имя_нижн → имя для печати

    def put(layer, key, value, line):
        kl = key.lower()
        display.setdefault(kl, key)
        layers[kl][layer] = (value, line)

    de_sys = (de or {}).get("SystemSettings", [])
    de_rend = [e for e in (de or {}).get("/Script/Engine.RendererSettings", []) if "." in e["key"]]
    for e in de_sys:
        put("DE[SystemSettings]", e["key"], e["value"], e["line"])
    for e in de_rend:
        put("DE[RendererSettings]", e["key"], e["value"], e["line"])
    for g, levels in ds_engine.items():
        for L, kv in levels.items():
            for kl, (k, v, ln) in kv.items():
                put("DS %s@%s" % (g, L), k, v, ln)
    dp_entries = []
    for sec, entries in (dp or {}).items():
        for e in entries:
            if e["key"] == "CVars" and "=" in e["value"]:
                k, v = e["value"].split("=", 1)
                dp_entries.append((sec, k.strip(), v.strip(), e["line"]))
                put("DP[%s]" % sec, k.strip(), v.strip(), e["line"])
    for tag, gus in (("GUS сборки", gus_b), ("GUS редактора", gus_e)):
        for sec in ("AsdaVideo", "AsdaRender", "ScalabilityGroups"):
            for e in (gus or {}).get(sec, []):
                if "." in e["key"]:
                    put("%s[%s]" % (tag, sec), e["key"], e["value"], e["line"])
    for r in regs:
        display.setdefault(r["name"].lower(), r["name"])

    # ---- (а) DefaultEngine ≠ умолчанию кода, справка называет прежнее
    project_value = {}                             # имя_нижн → (значение, слой, строка) — действующее «проектное» значение
    for layer_name, entries in (("DE[SystemSettings]", de_sys), ("DE[RendererSettings]", de_rend)):
        for e in entries:
            project_value[e["key"].lower()] = (e["value"], layer_name, e["line"])
    diff_rows = []
    for kl, (val, layer_name, ln) in sorted(project_value.items()):
        rs = [r for r in by_name.get(kl, []) if r["kind"] == "var"]
        if not rs:
            continue
        r = rs[0]
        if r["default"] is None or to_num(val) is None or same_val(r["default"], val):
            continue
        cited = []
        for txt_src, txt in (("справка", r["help"]), ("комментарий у переменной", r["default_comment"])):
            # Текст, который сам говорит «в проекте задано Y в DefaultEngine.ini», устаревшим не считается — но Y обязан совпадать с ini
            pm = re.search(r'в\s+проекте\s+задано\s+([-+]?\d+(?:[.,]\d+)?)[^.;]{0,40}DefaultEngine', txt or "", re.I)
            if pm:
                if not same_val(pm.group(1).replace(",", "."), val):
                    cited.append("%s называет проектное значение %s: «%s»" % (txt_src, pm.group(1), pm.group(0).strip()))
                continue
            for rx in DEFAULT_HINT_RES:
                for mm in rx.finditer(txt or ""):
                    n = to_num(mm.group(1).replace(",", "."))
                    if n is not None and same_val(n, r["default"]) and not same_val(n, val):
                        cited.append("%s: «%s»" % (txt_src, mm.group(0).strip()))
        cited = list(dict.fromkeys(cited))
        where = "%s:%d (умолч. %s) ↔ DefaultEngine.ini:%d = %s" % (rel(r["file"]), r["default_line"] or r["line"], fmt(r["default_raw"]), ln, fmt(val))
        diff_rows.append({"key": r["name"], "cpp": r["default_raw"], "ini": val, "cited": cited, "file": rel(r["file"]), "line": r["default_line"] or r["line"], "ini_line": ln})
        if cited:
            F.add("а", SEV_WARN, WHO_TEXT, r["name"], where,
                  "в ini действует %s, а %s" % (fmt(val), "; ".join(cited)),
                  "привести справку к действующему значению (альтернатива — перенести значение в код: решение владельца)")
        else:
            F.add("а", SEV_INFO, WHO_TEXT, r["name"], where,
                  "проектное значение отличается от умолчания кода; справка прежнее умолчание не называет",
                  "задокументировать в справке, что проект переопределяет умолчание")

    # (а2) комментарии, называющие не то значение ключа DefaultEngine
    stale_src = [(DS_INI, ln, t) for ln, t in ds_comments] + cpp_comment_lines
    for path, ln, txt in stale_src:
        for kl, (val, layer_name, dln) in project_value.items():
            key = display.get(kl, kl)
            for mm in re.finditer(re.escape(key) + r'`?\s*(?:=\s*|\s*\(\s*сейчас\s+|[^\n]{0,30}?сейчас\s+)([-+]?\d+(?:\.\d+)?)', txt, re.I):
                before = txt[max(0, mm.start() - 25):mm.start()].lower()
                if re.search(r'было|стало|was\b|раньше|вместо|\bдо\s', before):
                    continue
                if to_num(mm.group(1)) is None or same_val(mm.group(1), val):
                    continue
                # ini-комментарий или C++-комментарий, говорящий о ДЕЙСТВУЮЩЕМ значении проекта, — предупреждение; прочие (замеры, жесты) — сведения
                claims_current = path.endswith(".ini") or re.search(r'сейчас|глобально|проект|DefaultEngine|источник', txt, re.I) is not None
                F.add("а", SEV_WARN if claims_current else SEV_INFO, WHO_TEXT, key, "%s:%d" % (rel(path), ln),
                      "комментарий называет %s=%s, а в %s:%d стоит %s: «%s»" % (key, mm.group(1), "DefaultEngine.ini", dln, fmt(val), txt.strip()[:120]),
                      "привести комментарий")
    # (а3) запасные значения меню и GetCVar-вызовов против проектного/кодового значения
    for fb in fallbacks:
        kl = fb["key"].lower()
        ref_val, ref_src = None, None
        if kl in project_value:
            ref_val, ref_src = project_value[kl][0], "DefaultEngine.ini:%d" % project_value[kl][2]
        elif by_name.get(kl) and by_name[kl][0]["kind"] == "var" and by_name[kl][0]["default"] is not None:
            ref_val, ref_src = by_name[kl][0]["default_raw"], "умолчание кода %s:%d" % (rel(by_name[kl][0]["file"]), by_name[kl][0]["line"])
        if ref_val is None or to_num(fb["value"]) is None or same_val(fb["value"], ref_val):
            continue
        F.add("а", SEV_INFO, WHO_TEXT, fb["key"], "%s:%d" % (rel(fb["file"]), fb["line"]),
              "%s запасное значение %s, а действует %s (%s)" % (fb["api"], fb["value"], fmt(ref_val), ref_src),
              "привести запасное значение или пометить, что оно только на случай «ключа нет»")

    # ---- (б) закон E-0b
    for layer_name, entries in (("DE[SystemSettings]", de_sys), ("DE[RendererSettings]", de_rend)):
        for e in entries:
            lv = level_values(e["key"].lower())
            if not lv:
                continue
            levels_txt = ", ".join("%s %s@%s=%s" % (s, g, L, fmt(v)) for s, g, L, v, _ in lv[:6])
            F.add("б", SEV_ERR, WHO_OWNER, e["key"], "DefaultEngine.ini:%d (%s)" % (e["line"], layer_name),
                  "ключ задан в DefaultEngine (приоритет выше Scalability) и раздаётся уровнями: %s — уровни его не меняют" % levels_txt,
                  "убрать из DefaultEngine (значение перенести в DefaultScalability по уровням); если это подбор владельца — решение владельца")
    for sec, k, v, ln in dp_entries:
        if level_values(k.lower()):
            F.add("б", SEV_WARN, WHO_OWNER, k, "DefaultDeviceProfiles.ini:%d [%s]" % (ln, sec),
                  "CVar профиля устройства перекрывает уровни Scalability", "решение владельца")
    for g, levels in ds_engine.items():
        for L, kv in levels.items():
            for kl, (k, v, ln) in kv.items():
                rs = by_name.get(kl)
                if rs and rs[0]["kind"] == "var" and "ECVF_Scalability" not in rs[0]["flags"]:
                    F.add("б", SEV_ERR, WHO_CODE, k, "DefaultScalability.ini:%d [%s@%s] ↔ %s:%d" % (ln, g, L, rel(rs[0]["file"]), rs[0]["line"]),
                          "ключ в пер-уровневой секции, а у регистрации нет ECVF_Scalability (флаги: %s) — движок молча отвергает строку" % (",".join(rs[0]["flags"]) or "нет"),
                          "код: добавить ECVF_Scalability к регистрации, либо убрать строку из DefaultScalability")

    # ---- (в) дубли регистрации
    for kl, rs in sorted(by_name.items()):
        if len(rs) < 2:
            continue
        files = {r["file"] for r in rs}
        kinds = {r["kind"] for r in rs}
        survivor = "у переменных выживает ПЕРВАЯ" if kinds == {"var"} else ("у команд выживает ПОСЛЕДНЯЯ (порядок линковки)" if kinds == {"cmd"} else "команда и переменная с одним именем")
        F.add("в", SEV_WARN if len(files) > 1 else SEV_INFO, WHO_CODE, rs[0]["name"],
              "; ".join("%s:%d" % (rel(r["file"]), r["line"]) for r in rs),
              "имя зарегистрировано %d раз (%s)%s" % (len(rs), survivor, "" if len(files) > 1 else "; один файл — проверить ветки #if"),
              "переименовать одну из регистраций по схеме asda.<Система>.<Действие> (ue-console-duplicate-registration)")

    # ---- движок (по флагу)
    engine_regs, engine_scanned = {}, 0
    ini_engine_names = {kl for kl in layers if not kl.startswith("asda.") and not kl.startswith("sg.")}
    cpp_engine_names = {kl for kl in refs if kl.startswith("r.")}
    if args.engine:
        engine_regs, engine_scanned = scan_engine(engine_root, ini_engine_names | cpp_engine_names)

    # ---- (г) ключ в ini, которого нет в коде
    sg_names = {"sg." + g.lower() for g in engine_groups}
    for kl, lay in sorted(layers.items()):
        if kl.startswith("asda."):
            if kl in by_name:
                continue
            where = "; ".join("%s:%s" % (l, ln) for l, (v, ln) in lay.items())
            if kl in gconfig_keys:
                F.add("г", SEV_INFO, WHO_TEXT, display[kl], where, "ключ не зарегистрирован как CVar, но код читает его как данные GConfig", "оставить; при желании переименовать, чтобы не путать с CVar")
            else:
                F.add("г", SEV_ERR, WHO_CODE, display[kl], where, "asda.*-ключ в ini, регистрации в Source/Plugins нет (опечатка или мёртвая строка)", "исправить имя или убрать строку")
        elif kl.startswith("sg."):
            if kl not in sg_names and not any(l.startswith("GUS") for l in lay):
                F.add("г", SEV_WARN, WHO_CODE, display[kl], ", ".join(lay), "неизвестная группа sg.* (нет в BaseScalability)", "исправить имя")
        elif args.engine and kl not in engine_regs:
            F.add("г", SEV_WARN, WHO_CODE, display[kl], "; ".join("%s:%s" % (l, ln) for l, (v, ln) in lay.items()),
                  "литерал регистрации не найден в Engine/Source/Runtime и Engine/Plugins (метод: поиск литерала; возможен ложный «нет»)",
                  "проверить имя по консоли движка (help %s)" % display[kl])
    for kl, lst in sorted(refs.items()):
        if not kl.startswith("asda.") or kl in by_name or kl in gconfig_keys:
            continue
        where = "; ".join("%s:%d" % (rel(x[0]), x[1]) for x in lst[:3]) + (" (+%d)" % (len(lst) - 3) if len(lst) > 3 else "")
        if all(x[4] for x in lst):
            F.add("г", SEV_INFO, WHO_CODE, kl, where, "C++ ищет незарегистрированное имя; рядом задокументировано, что ключа пока нет (код готов к его появлению)",
                  "сведения: при заведении ключа взять именно это имя")
        else:
            F.add("г", SEV_WARN, WHO_CODE, kl, where, "C++ ссылается на имя, которое нигде не зарегистрировано (литерал: «%s»)" % lst[0][2][:80],
                  "исправить имя или убрать ссылку")
    if args.engine:
        for kl, lst in sorted(refs.items()):
            if kl.startswith("r.") and kl not in engine_regs:
                F.add("г", SEV_INFO, WHO_CODE, kl, "%s:%d" % (rel(lst[0][0]), lst[0][1]),
                      "C++ проекта ссылается на r.*, литерал регистрации в просмотренных каталогах движка не найден (возможен ложный «нет»)", "проверить имя")
        for g, levels in ds_engine.items():
            for L, kv in levels.items():
                for kl, (k, v, ln) in kv.items():
                    er = engine_regs.get(kl)
                    if er and all("ECVF_Scalability" not in r["flags"] for r in er):
                        F.add("б", SEV_WARN, WHO_CODE, k, "DefaultScalability.ini:%d [%s@%s] ↔ %s:%d" % (ln, g, L, er[0]["file"], er[0]["line"]),
                              "в регистрации движка не видно ECVF_Scalability — строка может отвергаться (ConfigUtilities.cpp «Scalability.ini can only set ECVF_Scalability»)",
                              "проверить по логу старта; убрать строку или перенести")
    for p in sorted(os.listdir(CONFIG_DIR)) if os.path.isdir(CONFIG_DIR) else []:
        full = os.path.join(CONFIG_DIR, p)
        if os.path.isfile(full) and not p.lower().endswith(".ini"):
            F.add("г", SEV_WARN, WHO_OWNER, p, "Config/%s" % p, "«тень» конфига рядом с настоящим (ловушка ручной реконструкции, как 03.09)", "убрать из Config/ в _backup/ — решение владельца")

    # ---- (д) пины GameUserSettings, перекрывающие уровни
    gus_groups = {}
    for e in (gus_b or {}).get("ScalabilityGroups", []):
        gus_groups[e["key"].lower()] = e["value"]
    for tag, gus, sev in (("GUS сборки", gus_b, SEV_WARN), ("GUS редактора", gus_e, SEV_INFO)):
        if gus is None:
            continue
        path_txt = rel(GUS_BUILD if tag == "GUS сборки" else GUS_EDITOR, AREA)
        groups_here = {e["key"].lower(): e["value"] for e in gus.get("ScalabilityGroups", [])}
        for e in gus.get("AsdaVideo", []):
            if "." not in e["key"]:
                continue
            kl = e["key"].lower()
            lv = level_values(kl)
            pv = project_value.get(kl)
            if lv:
                g = lv[0][1]
                cur = level_for_sg(groups_here.get("sg." + g.lower()))
                eff = [x for x in lv if x[1] == g and x[2] == cur]
                eff_txt = ("; на текущем уровне sg.%s=%s уровень даёт %s=%s — %s" % (g, groups_here.get("sg." + g.lower()), eff[0][0], fmt(eff[0][3]),
                           "совпадает с пином, на этом уровне снятие пина картинку не изменит" if same_val(eff[0][3], e["value"]) else "пин МЕНЯЕТ картинку")) if eff else ""
                F.add("д", sev, WHO_OWNER, e["key"], "%s:%d [AsdaVideo]" % (path_txt, e["line"]),
                      "пин %s перекрывает уровни (%s)%s" % (fmt(e["value"]), ", ".join("%s %s@%s=%s" % (s, gg, L, fmt(v)) for s, gg, L, v, _ in lv[:6]), eff_txt),
                      "решение владельца: оставить как выбор или снять пин (вернуть управление уровням)")
            elif pv is not None:
                F.add("д", SEV_INFO, WHO_OWNER, e["key"], "%s:%d [AsdaVideo]" % (path_txt, e["line"]),
                      "пин %s %s проектному значению %s (DefaultEngine.ini:%d)" % (fmt(e["value"]), "равен" if same_val(e["value"], pv[0]) else "отличается от", fmt(pv[0]), pv[2]),
                      "снятие пина картинку не изменит" if same_val(e["value"], pv[0]) else "выбор владельца — оставить")
        for e in gus.get("AsdaRender", []):
            kl = e["key"].lower()
            if not kl.startswith("asda."):
                continue
            short = kl[len("asda."):]
            hits = [(g, L, kv[short]) for g, levels in ds_custom.items() for L, kv in levels.items() if short in kv]
            pv = project_value.get(kl)
            if hits:
                F.add("д", sev, WHO_OWNER, e["key"], "%s:%d [AsdaRender]" % (path_txt, e["line"]),
                      "пин %s перекрывает таблицу уровней DS: %s" % (fmt(e["value"]), ", ".join("%s@%s=%s" % (g, L, fmt(t[1])) for g, L, t in sorted(hits, key=lambda h: (h[0], LEVEL_ORDER.index(h[1]) if h[1] in LEVEL_ORDER else 9)))),
                      "решение владельца: оставить или снять пин")
            elif pv is not None:
                F.add("д", SEV_INFO, WHO_OWNER, e["key"], "%s:%d [AsdaRender]" % (path_txt, e["line"]),
                      "пин %s %s проектному значению %s (DefaultEngine.ini:%d)" % (fmt(e["value"]), "равен" if same_val(e["value"], pv[0]) else "отличается от", fmt(pv[0]), pv[2]),
                      "снятие пина картинку не изменит" if same_val(e["value"], pv[0]) else "выбор владельца — оставить")
            else:
                rs = by_name.get(kl)
                if rs and rs[0]["kind"] == "var" and rs[0]["default"] is not None and not same_val(rs[0]["default"], e["value"]):
                    F.add("д", SEV_INFO, WHO_OWNER, e["key"], "%s:%d [AsdaRender]" % (path_txt, e["line"]),
                          "пин %s отличается от умолчания кода %s (%s:%d)" % (fmt(e["value"]), fmt(rs[0]["default_raw"]), rel(rs[0]["file"]), rs[0]["line"]),
                          "выбор владельца — оставить")
        for e in gus.get("AsdaNormals", []):
            short = "normals." + e["key"].lower()
            hits = [(g, L, kv[short]) for g, levels in ds_custom.items() for L, kv in levels.items() if short in kv]
            if hits:
                F.add("д", sev, WHO_OWNER, "Normals." + e["key"], "%s:%d [AsdaNormals]" % (path_txt, e["line"]),
                      "пин %s перекрывает таблицу уровней DS: %s" % (fmt(e["value"]), ", ".join("%s@%s=%s" % (g, L, fmt(t[1])) for g, L, t in hits)),
                      "решение владельца: оставить или снять пин")
    baseline = {e["key"].lower(): (e["key"], e["value"], e["line"]) for e in (ds or {}).get("AsdaDefaultQuality", [])}
    baseline_rows = []
    for kl, (k, v, ln) in sorted(baseline.items()):
        gv = gus_groups.get(kl)
        baseline_rows.append({"key": k, "baseline": v, "line": ln, "gus_build": gv})
        if gv is not None and not same_val(gv, v):
            F.add("д", SEV_INFO, WHO_OWNER, k, "DefaultScalability.ini:%d [AsdaDefaultQuality] ↔ GUS сборки [ScalabilityGroups]" % ln,
                  "точка отсчёта группы %s, у владельца сейчас %s" % (v, gv), "сверить подбор владельца с его текущим файлом — решение владельца")

    # ---- (е) пути к серверу
    server_paths, paths_py_lines = collect_server_paths()
    per_file = collections.OrderedDict()
    for sp in server_paths:
        per_file.setdefault(sp["file"], [])
        if sp["gameserver"] not in per_file[sp["file"]]:
            per_file[sp["file"]].append(sp["gameserver"])
    cpp_sets = {f: tuple(v) for f, v in per_file.items() if f.endswith((".cpp", ".h"))}
    if len(set(cpp_sets.values())) > 1:
        F.add("е", SEV_WARN, WHO_OWNER, "путь GameServer", "; ".join(rel(f) for f in cpp_sets),
              "C++ ищет GameServer в разных наборах каталогов: " + " | ".join("%s → %s" % (os.path.basename(f), ", ".join(v)) for f, v in cpp_sets.items()),
              "единый путь (реестр путей S3) — решение владельца")
    elif cpp_sets:
        F.add("е", SEV_INFO, WHO_OWNER, "путь GameServer", "; ".join(rel(f) for f in cpp_sets),
              "наборы каталогов в C++ сейчас совпадают (%s), но записаны %d копиями" % (", ".join(next(iter(cpp_sets.values()))), len(cpp_sets)),
              "свести в одну функцию/реестр путей (S3), чтобы не разошлись снова")
    unc_spellings = sorted({re.match(r'^//[^/]+/[^/]+', sp["norm"]).group(0) for sp in server_paths if sp["norm"].startswith("//") and re.match(r'^//[^/]+/[^/]+', sp["norm"])})
    raw_spellings = sorted({"%s\\%s" % (mm.group(1), mm.group(2)) for sp in server_paths for mm in [re.search(r'(@@ASTORY_SERVER_HOSTNAME@@)[\\/]+([^\\/]+)', sp["raw"], re.I)] if mm})
    if len(server_paths):
        py_files = sorted({rel(sp["file"]) for sp in server_paths if sp["file"].endswith(".py")})
        F.add("е", SEV_INFO, WHO_OWNER, "UNC сервера", "C++: %d файлов, Tools: %d файлов" % (len(cpp_sets), len(py_files)),
              "UNC-путь живого сервера записан литералом в %d местах; написания: %s; asda_paths.py: %s" % (len(server_paths), ", ".join(raw_spellings), "; ".join("%d: %s" % x for x in (paths_py_lines or [])) or "нет"),
              "кандидат в единый реестр путей (S3)")
    local_dirs = sorted({sp["gameserver"] for sp in server_paths if not sp["gameserver"].startswith("//")})
    local_exist = {d: os.path.isdir(d) for d in local_dirs}

    # ---- (ж) классы настроек: умолчания C++ против ini, канарейка
    classes, structs = collect_settings_classes(proj_roots)
    class_rows = []
    for c in classes:
        ini = dg if c["config"].lower() == "game" else (de if c["config"].lower() == "engine" else None)
        sec = (ini or {}).get(c["section"])
        has_stamp = any(f["name"] == "ConfigStamp" for f in c["fields"])
        mism = []
        if sec is not None:
            grouped = collections.defaultdict(list)
            for e in sec:
                grouped[e["key"]].append(e)
            for f in c["fields"]:
                if not f["config"] or f["name"] not in grouped:
                    continue
                entries = grouped[f["name"]]
                inner = re.findall(r'F\w+', f["type"])
                struct_t = inner[-1] if inner and inner[-1] in structs else None
                if struct_t:
                    for e in entries:
                        val = parse_ue_text(e["value"])
                        if isinstance(val, dict):
                            compare_struct(val, struct_t, structs, "%s[%s]" % (f["name"], val.get("Id", "?")) if "Id" in val else f["name"], mism)
                        elif isinstance(val, list):
                            for item in val:
                                if isinstance(item, list) and len(item) == 2 and isinstance(item[1], dict):
                                    compare_struct(item[1], struct_t, structs, "%s[%s]" % (f["name"], item[0]), mism)
                                elif isinstance(item, dict):
                                    compare_struct(item, struct_t, structs, "%s[%s]" % (f["name"], item.get("Id", "?")), mism)
                elif f["default"] is not None and len(entries) == 1 and "(" not in f["default"] and not same_val(re.sub(r'^TEXT\("(.*)"\)$', r"\1", f["default"]), entries[0]["value"]):
                    mism.append({"path": f["name"], "field": f["name"], "cpp": f["default"], "ini": entries[0]["value"], "struct": c["class"], "file": c["file"], "line": f["line"]})
        class_rows.append({"class": c["class"], "section": c["section"], "config": c["config"], "file": rel(c["file"]), "line": c["line"],
                           "section_in_ini": sec is not None, "config_stamp": has_stamp, "mismatches": mism})
        if sec is None:
            F.add("ж", SEV_INFO, WHO_CODE, c["class"], "%s:%d" % (rel(c["file"]), c["line"]), "секции [%s] в Default%s.ini нет — работают умолчания C++" % (c["section"], c["config"]), "сведения")
            continue
        if mism and not has_stamp:
            by_field = collections.OrderedDict()
            for mm in sorted(mism, key=lambda x: (to_num(x["cpp"]) is None or to_num(x["ini"]) is None)):   # числовые — первыми в примерах
                by_field.setdefault(mm["field"], []).append(mm)
            examples = "; ".join("%s: C++ %s, ini %s" % (lst[0]["path"], fmt(lst[0]["cpp"]), fmt(lst[0]["ini"])) for lst in list(by_field.values())[:5])
            F.add("ж", SEV_WARN, WHO_OWNER, c["class"], "%s:%d ↔ Default%s.ini [%s]" % (rel(c["file"]), c["line"], c["config"], c["section"]),
                  "канарейки ConfigStamp нет; если секция не прочитается, %d значений молча вернутся к умолчаниям C++ (%d полей). Примеры: %s" % (len(mism), len(by_field), examples),
                  "добавить канарейку ConfigStamp + ValidateConfig (как UAsdaCullSettings) и/или перенести значения данных в умолчания C++ — решение владельца")
        elif mism:
            F.add("ж", SEV_INFO, WHO_TEXT, c["class"], "%s:%d" % (rel(c["file"]), c["line"]),
                  "%d значений ini отличаются от умолчаний C++; канарейка ConfigStamp есть — непрочитанная секция будет замечена" % len(mism), "сведения")
    presets_check = None
    if os.path.isfile(LIGHT_PRESETS_TOOL):
        env = dict(os.environ, PYTHONIOENCODING="utf-8")
        try:
            pr = subprocess.run([sys.executable, LIGHT_PRESETS_TOOL, "--check"], capture_output=True, text=True, encoding="utf-8", errors="replace", env=env, timeout=120)
            presets_check = {"rc": pr.returncode, "out": (pr.stdout + pr.stderr).strip()}
            if pr.returncode != 0:
                F.add("ж", SEV_WARN, WHO_OWNER, "LightPresets", "Tools/asda_light_presets.json ↔ DefaultGame.ini", "генератор пресетов --check: расхождение JSON и ini (код %d)" % pr.returncode, "перенести число в JSON или наоборот — решение владельца")
        except (OSError, subprocess.SubprocessError) as ex:
            presets_check = {"rc": None, "out": str(ex)}

    # ---- итог
    regs_out = [dict(r, file=rel(r["file"])) for r in regs]
    return {
        "generated": datetime.datetime.now().isoformat(timespec="seconds"),
        "sources": {"DefaultEngine": rel(DE_INI), "DefaultScalability": rel(DS_INI), "DefaultGame": rel(DG_INI),
                    "DefaultDeviceProfiles": rel(DP_INI) if dp is not None else None, "BaseScalability": os.path.join(engine_root, "Engine", "Config", "BaseScalability.ini") if bs is not None else None,
                    "GUS_build": GUS_BUILD if gus_b is not None else None, "GUS_editor": GUS_EDITOR if gus_e is not None else None},
        "counts": {"registrations": len(regs), "names": len(by_name), "vars": sum(1 for r in regs if r["kind"] == "var"),
                   "cmds": sum(1 for r in regs if r["kind"] == "cmd"), "unparsed": len(unparsed),
                   "asda_names": sum(1 for k in by_name if k.startswith("asda.")),
                   "cpp_engine_refs": len([k for k in refs if not k.startswith("asda.")]),
                   "ini_keys": len(layers), "engine_files_scanned": engine_scanned},
        "registrations": regs_out,
        "unparsed": [dict(u, file=rel(u["file"])) for u in unparsed],
        "layers": {display[k]: {l: {"value": v, "line": ln} for l, (v, ln) in lay.items()} for k, lay in sorted(layers.items())},
        "cpp_refs": {k: [{"file": rel(x[0]), "line": x[1]} for x in v if not x[3]][:10] for k, v in sorted(refs.items()) if not k.startswith("asda.")},
        "de_vs_code": diff_rows,
        "ds_custom_levels": {g: {L: {k: v for k, (kk, v, ln) in kv.items()} for L, kv in levels.items()} for g, levels in ds_custom.items()},
        "baseline_quality": baseline_rows,
        "server_paths": [dict(sp, file=rel(sp["file"])) for sp in server_paths],
        "server_local_dirs_exist": local_exist,
        "settings_classes": class_rows,
        "light_presets_check": presets_check,
        "engine": {"scanned": bool(args.engine), "found": {k: [{"file": r["file"], "line": r["line"], "flags": r["flags"]} for r in v] for k, v in engine_regs.items()}},
        "findings": F.sorted(),
        "_by_name": by_name, "_layers": layers, "_display": display, "_ds_levels": ds_levels, "_bs_levels": bs_levels,
        "_project_value": project_value,
    }


# ------------------------------------------------------------------------------------------------ отчёт

def md_escape(s):
    """Экранирование ячейки таблицы Markdown."""
    return str(s).replace("|", "\\|").replace("\n", " ")


def write_report(state, path, argv):
    """Отчёт Markdown: сводка, находки по «кто решает», таблица ключей по слоям, классы настроек, пути сервера."""
    c = state["counts"]
    fnd = state["findings"]
    by_sev = collections.Counter(f["severity"] for f in fnd)
    L = []
    L.append("# Реестр настроек и консольных ключей — проверка")
    L.append("")
    L.append("Сгенерировано `Tools/asda_settings_registry.py` %s, команда: `python Tools/asda_settings_registry.py %s`. "
             "Инструмент только читает; значения в `Config/*.ini` не менялись." % (state["generated"], " ".join(argv)))
    L.append("")
    L.append("## 1. Сводка")
    L.append("")
    L.append("| Показатель | Значение |")
    L.append("|---|---|")
    L.append("| Регистраций консольных имён (Source + Plugins) | %d (переменных %d, команд %d) |" % (c["registrations"], c["vars"], c["cmds"]))
    L.append("| Уникальных имён | %d, из них `asda.*` %d |" % (c["names"], c["asda_names"]))
    L.append("| Неразобранных регистраций (имя не литерал) | %d |" % c["unparsed"])
    L.append("| Движковых имён `r.*`/`sg.*`, на которые ссылается C++ | %d |" % c["cpp_engine_refs"])
    L.append("| Ключей в слоях ini (DE, DS-уровни, DP, GUS) | %d |" % c["ini_keys"])
    L.append("| Поиск по движку (`--engine`) | %s |" % ("да, файлов %d" % c["engine_files_scanned"] if state["engine"]["scanned"] else "нет (r.* на существование не проверялись)"))
    L.append("| Находок | %s |" % ", ".join("%s %d" % (s, by_sev.get(s, 0)) for s in (SEV_ERR, SEV_WARN, SEV_INFO)))
    L.append("")
    L.append("Источники: " + "; ".join("%s — `%s`" % (k, v) for k, v in state["sources"].items() if v) + ".")
    L.append("")
    L.append("Кто решает: **владелец** — меняется поведение или выбор значения; **код** — правка C++/ini без смены поведения (агентам ядра); "
             "**текст** — только справка и комментарии.")
    L.append("")
    sections = [("2. Требуют решения владельца", WHO_OWNER), ("3. Правки кода без смены поведения", WHO_CODE), ("4. Только текст: справка и комментарии", WHO_TEXT)]
    for title, who in sections:
        items = [f for f in fnd if f["who"] == who]
        L.append("## %s (%d)" % (title, len(items)))
        L.append("")
        if not items:
            L.append("Нет.")
            L.append("")
            continue
        L.append("| # | Пров. | Серьёзность | Ключ | Где | Суть | Предложенное действие |")
        L.append("|---|---|---|---|---|---|---|")
        for i, f in enumerate(items, 1):
            L.append("| %d | %s | %s | `%s` | %s | %s | %s |" % (i, f["check"], f["severity"], md_escape(f["key"]), md_escape(f["where"]), md_escape(f["text"]), md_escape(f["action"])))
        L.append("")

    L.append("## 5. Ключи по слоям")
    L.append("")
    L.append("Только ключи, которые встречаются хотя бы в одном ini-слое. Полный список регистраций — в `settings_registry.json`. "
             "Уровни DS/BS — значения 0 / 1 / 2 / 3 / Cine (DS перекрывает BS; «·» — уровень ключ не задаёт).")
    L.append("")
    L.append("| Ключ | Умолчание C++ | Где зарегистрирован | DefaultEngine | Уровни (DS > BS) | GUS сборки | GUS редактора |")
    L.append("|---|---|---|---|---|---|---|")
    by_name, layers, display = state["_by_name"], state["_layers"], state["_display"]
    ds_lv, bs_lv = state["_ds_levels"], state["_bs_levels"]
    for kl in sorted(layers, key=lambda k: (not k.startswith("asda."), k)):
        lay = layers[kl]
        rs = by_name.get(kl, [])
        reg = rs[0] if rs else None
        cpp = fmt(reg["default_raw"]) if reg and reg["kind"] == "var" else ("команда" if reg else ("—" if kl.startswith("asda.") else "движок"))
        where = ("%s:%d" % (reg["file"] if not os.path.isabs(reg["file"]) else rel(reg["file"]), reg["line"])) if reg else "—"
        de_v = " / ".join("%s (:%d)" % (fmt(v), ln) for l, (v, ln) in lay.items() if l.startswith("DE["))
        lv_txt = []
        groups = set()
        for src_lv in (ds_lv, bs_lv):
            for g, levels in src_lv.items():
                if any(kl in levels.get(L_, {}) for L_ in LEVEL_ORDER):
                    groups.add(g)
        for g in sorted(groups):
            vals = []
            for L_ in LEVEL_ORDER:
                v = ds_lv.get(g, {}).get(L_, {}).get(kl) or bs_lv.get(g, {}).get(L_, {}).get(kl)
                vals.append(fmt(v[1]) if v else "·")
            lv_txt.append("%s: %s" % (g, " / ".join(vals)))
        gb = " ".join("%s (%s)" % (fmt(v), l.split("[")[1].rstrip("]")) for l, (v, ln) in lay.items() if l.startswith("GUS сборки"))
        ge = " ".join("%s (%s)" % (fmt(v), l.split("[")[1].rstrip("]")) for l, (v, ln) in lay.items() if l.startswith("GUS редактора"))
        L.append("| `%s` | %s | %s | %s | %s | %s | %s |" % (md_escape(display[kl]), md_escape(cpp), md_escape(where), md_escape(de_v or "—"),
                                                         md_escape("; ".join(lv_txt) or "—"), md_escape(gb or "—"), md_escape(ge or "—")))
    L.append("")
    L.append("### 5.1. DefaultEngine против умолчаний кода (проверка «а»)")
    L.append("")
    L.append("| Ключ | Умолчание C++ | DefaultEngine.ini | Справка/комментарий называет прежнее |")
    L.append("|---|---|---|---|")
    for r in state["de_vs_code"]:
        L.append("| `%s` | %s (`%s:%d`) | %s (:%d) | %s |" % (r["key"], md_escape(fmt(r["cpp"])), r["file"], r["line"], md_escape(fmt(r["ini"])), r["ini_line"], md_escape("; ".join(r["cited"]) or "нет")))
    L.append("")
    L.append("### 5.2. Проектные таблицы уровней (не CVar, читает код через `ScalabilityLevelValue`)")
    L.append("")
    L.append("| Группа.ключ | 0 | 1 | 2 | 3 | Cine |")
    L.append("|---|---|---|---|---|---|")
    for g, levels in sorted(state["ds_custom_levels"].items()):
        keys = sorted({k for L_ in levels.values() for k in L_})
        for k in keys:
            L.append("| %s.%s | %s |" % (g, k, " | ".join(fmt(levels.get(L_, {}).get(k)) if levels.get(L_, {}).get(k) is not None else "·" for L_ in LEVEL_ORDER)))
    L.append("")
    if state["baseline_quality"]:
        L.append("### 5.3. Точка отсчёта групп `[AsdaDefaultQuality]` против `[ScalabilityGroups]` сборки владельца")
        L.append("")
        L.append("| Группа | DefaultScalability | GUS сборки |")
        L.append("|---|---|---|")
        for r in state["baseline_quality"]:
            L.append("| %s | %s (:%d) | %s |" % (r["key"], r["baseline"], r["line"], fmt(r["gus_build"])))
        L.append("")

    L.append("## 6. Классы настроек (проверка «ж»)")
    L.append("")
    L.append("| Класс | Секция | Секция в ini | Канарейка ConfigStamp | Значений ini ≠ умолчанию C++ |")
    L.append("|---|---|---|---|---|")
    for r in state["settings_classes"]:
        L.append("| `%s` (`%s:%d`) | `[%s]` | %s | %s | %d |" % (r["class"], r["file"], r["line"], r["section"], "да" if r["section_in_ini"] else "нет", "да" if r["config_stamp"] else "**нет**", len(r["mismatches"])))
    L.append("")
    for r in state["settings_classes"]:
        if not r["mismatches"]:
            continue
        L.append("<details><summary>%s — %d расхождений умолчаний C++ с ini</summary>" % (r["class"], len(r["mismatches"])))
        L.append("")
        L.append("| Путь | C++ | ini | Объявление |")
        L.append("|---|---|---|---|")
        for mm in r["mismatches"]:
            L.append("| %s | %s | %s | `%s:%d` |" % (md_escape(mm["path"]), md_escape(fmt(mm["cpp"])), md_escape(fmt(mm["ini"])), rel(mm["file"]), mm["line"]))
        L.append("")
        L.append("</details>")
        L.append("")
    pc = state["light_presets_check"]
    if pc:
        L.append("Генератор пресетов `Tools/asda_gen_render_settings_ini.py --check` (JSON ↔ DefaultGame.ini ↔ `[AsdaShadow@N]`): код %s — %s" % (pc["rc"], md_escape(pc["out"].replace("\n", "; "))))
        L.append("")

    L.append("## 7. Пути к серверу (проверка «е»)")
    L.append("")
    L.append("| Файл:строка | Литерал | Каталог GameServer |")
    L.append("|---|---|---|")
    for sp in state["server_paths"]:
        L.append("| `%s:%d` | `%s` | `%s` |" % (sp["file"], sp["line"], md_escape(sp["raw"]), sp["gameserver"]))
    L.append("")
    if state["server_local_dirs_exist"]:
        L.append("Локальные каталоги: " + "; ".join("`%s` — %s" % (d, "есть" if ok else "НЕТ") for d, ok in state["server_local_dirs_exist"].items()) + ". UNC не опрашивался.")
        L.append("")
    L.append("## 8. Метод и границы")
    L.append("")
    L.append("Имена берутся из строковых литералов регистраций (`TAutoConsoleVariable`, `FAutoConsole*`, `RegisterConsole*`); "
             "«нет в коде» по движку без `--engine` не утверждается. Находки уровня %s включены в разделы 2–4. "
             "Неразобранных регистраций %d%s." % (SEV_INFO, c["unparsed"], (": " + "; ".join("`%s:%d` %s(%s)" % (u["file"], u["line"], u["kw"], md_escape(u["arg0"])) for u in state["unparsed"][:15])) if state["unparsed"] else ""))
    L.append("")
    L.append("Сверка счёта: сканер аудита `scratchpad/dedup/cvar_scan.py` на 14.09 дал 1001 имя / 1001 регистрацию; этот инструмент — %d / %d." % (c["names"], c["registrations"]))
    L.append("")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(L))


def public_state(state):
    """Состояние без служебных (_*) полей — для JSON."""
    return {k: v for k, v in state.items() if not k.startswith("_")}


def main(argv):
    """Точка входа: разбор флагов, сбор, проверки, отчёт/дамп, код возврата."""
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description="Реестр настроек и консольных ключей AsdaReborn (только проверка).")
    ap.add_argument("--check", action="store_true", help="код возврата 3 при ошибках/предупреждениях")
    ap.add_argument("--report", action="store_true", help="записать отчёт Markdown")
    ap.add_argument("--report-out", default=REPORT_MD, help="путь отчёта")
    ap.add_argument("--json", action="store_true", help="записать машинный дамп")
    ap.add_argument("--json-out", default=REPORT_JSON, help="путь дампа")
    ap.add_argument("--engine", action="store_true", help="искать r.* в исходниках движка")
    ap.add_argument("--engine-root", default=None, help="корень UE (по умолчанию ASDA_UE_ROOT или D:\\Games\\UE_5.8)")
    args = ap.parse_args(argv)
    if not (args.check or args.report or args.json):
        ap.print_help()
        return 0
    state = build_registry(args)
    c = state["counts"]
    fnd = state["findings"]
    by_sev = collections.Counter(f["severity"] for f in fnd)
    print("регистраций %d (имён %d, asda.* %d, неразобрано %d); ключей в ini %d" % (c["registrations"], c["names"], c["asda_names"], c["unparsed"], c["ini_keys"]))
    print("находки: " + ", ".join("%s %d" % (s, by_sev.get(s, 0)) for s in (SEV_ERR, SEV_WARN, SEV_INFO)))
    for f in fnd:
        if f["severity"] != SEV_INFO:
            print("  [%s] %s (%s) %s — %s" % (f["check"], f["severity"], f["who"], f["key"], f["text"][:160]))
    if args.report:
        write_report(state, args.report_out, argv)
        print("отчёт: " + args.report_out)
    if args.json:
        os.makedirs(os.path.dirname(args.json_out), exist_ok=True)
        with open(args.json_out, "w", encoding="utf-8", newline="\n") as fh:
            json.dump(public_state(state), fh, ensure_ascii=False, indent=1, default=str)
        print("дамп: " + args.json_out)
    if args.check:
        return 3 if (by_sev.get(SEV_ERR) or by_sev.get(SEV_WARN)) else 0
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
