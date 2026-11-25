# Copyright (c) 2026 @sitxovski (Discord)
# asda_asset_paths.py — РЕЕСТР ПУТЕЙ И ПРАВИЛ ИМЁН АССЕТОВ: единый источник `Tools/data/asda_asset_paths.json` →
#   генерация C++-заголовка и Python-блока, сверка с диском, проверка литералов после волн.
#
# ЗАЧЕМ (аудит дублей 14.09.2026, E11/E15, волна 2 п. 7; разбор `Reports/dedup_audit/wave2_asset_paths_design.md`).
#   Одни и те же правила пути («BP_Fx_» + jeb с «-»→«_», «<корень>/<имя>/SkeletalMeshes/<имя>anim_NN», JUI-атлас,
#   каталог текстур карты…) жили копиями в ~15 файлах C++ и ~50 инструментах Python. Копии расходились (anim_%d
#   против anim_%02d, сырое имя карты против приведённого), а загрузка идёт с LOAD_NoWarn — промах молчит.
#
# ЧТО ГЕНЕРИРУЕТСЯ из JSON (руками не править; правка = JSON + запуск этого скрипта):
#   1. Source/AsdaReborn/Public/Resources/AsdaAssetPaths.h — C++: корни (constexpr), функции правил, обратная таблица D4
#   2. Tools/asda_ue_paths.py — блок между маркерами «★ GENERATED asda_asset_paths»: константы G_*/M_* (прежние
#      имена и значения) + функции правил (snake_case от имён C++); ручная часть файла не трогается.
#   Нормализатор имени карты/звука НЕ копируется: C++ зовёт AsdaMapRegistry::ToAssetName, Python —
#   asda_map_registry.cpp_asset_name (реестр карт, `Tools/asda_map_registry.py`).
#
# ЗАПУСК (обычный python, unreal НЕ нужен; `python -B`):
#   python -B Tools/asda_asset_paths.py                 — перегенерировать; пишет только изменившиеся файлы, с бэкапом
#   python -B Tools/asda_asset_paths.py --diff          — показать разницу, ничего не писать
#   python -B Tools/asda_asset_paths.py --check         — целостность JSON, актуальность генерации, корни на диске,
#                                                         контрольные векторы, таблица D4, потребители; код 1 при ОШИБКАХ
#   python -B Tools/asda_asset_paths.py --scan          — ★ ПРОВЕРКА ПОСЛЕ ВОЛН: литералы /Game/ в Source/Plugins/Tools;
#                                                         новые шаблоны вне реестра (сверх базы) → ОШИБКА (пробы _*.py —
#                                                         предупреждение), несуществующие одиночные ассеты → предупреждение
#   python -B Tools/asda_asset_paths.py --scan --update-baseline — принять текущие шаблоны как базу (после разбора!)
#   python -B Tools/asda_asset_paths.py --verify-data   — правила на реальных данных (CSV, реестр карт) против Content
#   python -B Tools/asda_asset_paths.py --refresh-fx-table — пересобрать в JSON таблицу D4 (jeb с «-», у которых есть BP)
#
# КАК ДОБАВИТЬ ПРАВИЛО:
#   1. JSON → `rules`: {id, cpp, py, doc, args:[{name, py, type: str|tchar|int|bool, default?}], lets?, root?, pattern, form}.
#      pattern — текст с метками {Арг}, {Арг:02d}, {Арг?m:f}; root — id из `paths` (или аргумент {Root} в pattern);
#      form: name (имя), package/dir (пакет/папка), object («.<лист>»), class («.<лист>_C»). Лист = последний сегмент pattern.
#      lets — локальные значения до форматирования: {name, py, rule, args} (вызов правила) или {name, py, norm, arg}
#      (нормализатор: asset_name = ToAssetName реестра карт, dash_to_us = «-»→«_»).
#   2. Контрольный вектор в `vectors` — ОЖИДАЕМАЯ СТРОКА ПО СТАРОЙ ФОРМУЛЕ, выписанная вручную (не прогоном генератора).
#   3. `python -B Tools/asda_asset_paths.py` → `--check` (0 ошибок) → полная сборка C++ при закрытом редакторе.
#
# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает (всё — в main()).
# ★ Ассеты и уровни не пишет никогда (только чтение Content). Login.umap и инструменты логина не трогает.

import argparse
import datetime
import difflib
import fnmatch
import json
import os
import re
import shutil
import sys

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(TOOLS_DIR)
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)
import asda_paths as _P   # noqa: E402  (корни диска; модуль без побочных эффектов)
import asda_registry_io as _RIO   # noqa: E402  ★ дедуп №2 В1 Т16: общий ввод-вывод генераторов реестров

# ── Пути ──────────────────────────────────────────────────────────────────────────────────────────────────────
REGISTRY_JSON = os.path.join(TOOLS_DIR, "data", "asda_asset_paths.json")                  # ★ ЕДИНЫЙ ИСТОЧНИК
BASELINE_JSON = os.path.join(TOOLS_DIR, "data", "asda_asset_paths_scan_baseline.json")    # база --scan
OUT_CPP = os.path.join(PROJECT_DIR, "Source", "AsdaReborn", "Public", "Resources", "AsdaAssetPaths.h")
OUT_PY = os.path.join(TOOLS_DIR, "asda_ue_paths.py")
CONTENT_DIR = os.path.join(PROJECT_DIR, "Content")
# ★ Т3 (11.10.2026, решение владельца): единый корень резервных копий @@ASTORY_DEV_ROOT@@\_backup (был AstoryClient\_backup;
#   старые наборы там не трогаются). Путь внутри набора — от @@ASTORY_DEV_ROOT@@ (asda_headless_tool.backup_rel_path).
BACKUP_ROOT = _P.ROOT + os.sep + "_backup"
# Имена .jeb оригинала (для таблицы D4). Переопределяется переменной окружения.
FX_JEB_DIR = os.environ.get("ASDA_FX_JEB_DIR", os.path.join(_P.ROOT, "Donors", "TRANSLATE_unpacked", "models_orig", "Data", "Effect", "Model"))
FX_CONTENT_DIR = os.path.join(CONTENT_DIR, "AsdaAssets", "Effects")

PY_BEGIN = ("# ── ★ GENERATED asda_asset_paths BEGIN: генерирует Tools/asda_asset_paths.py из "
            "Tools/data/asda_asset_paths.json — НЕ ПРАВИТЬ РУКАМИ ──")
PY_END = "# ── ★ GENERATED asda_asset_paths END ──"

ARG_TYPES = ("str", "tchar", "int", "bool")
FORMS = ("name", "package", "dir", "object", "class")
TOKEN = re.compile(r"\{([A-Za-z_][A-Za-z0-9_]*)(?::(0?[0-9]*d))?(?:\?([^:}]*):([^}]*))?\}")
IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

# Нормализаторы: C++-выражение, Python-выражение (исходник для блока) — {x} = аргумент.
NORM_CPP = {
    "asset_name": "AsdaMapRegistry::ToAssetName({x})",
    "dash_to_us": "{x}.Replace(TEXT(\"-\"), TEXT(\"_\"))",
}
NORM_PY = {
    "asset_name": "_MAPREG.cpp_asset_name({x})",
    "dash_to_us": "{x}.replace(\"-\", \"_\")",
}


# ══ Загрузка / сохранение источника ══════════════════════════════════════════════════════════════════════════
def load_registry(path=REGISTRY_JSON):
    """Прочитать JSON реестра (общий `asda_registry_io.load_json`)."""
    return _RIO.load_json(path)


def dump_registry(reg):
    """Каноническая запись JSON (UTF-8 без BOM, LF, отступ 1) — одна форма для бутстрапа и --refresh-fx-table."""
    return (json.dumps(reg, ensure_ascii=False, indent=1) + "\n").encode("utf-8")


def _read_bytes(path):
    """Содержимое файла или None (общий `asda_registry_io.read_bytes`)."""
    return _RIO.read_bytes(path)


def _backup_path(path):
    """Путь бэкапа: @@ASTORY_DEV_ROOT_ESC@@\\_backup\\<дата>_asset_paths\\<путь относительно @@ASTORY_DEV_ROOT_ESC@@> (★ Т3, 11.10.2026: был
    AstoryClient\\_backup\\…\\<путь от проекта>). Правило «копия дня + промежуточная со временем» — в _write_if_changed."""
    import asda_headless_tool as HT   # лениво: модулю генератора не нужен каркас при импорте
    day = datetime.date.today().isoformat()
    return os.path.join(BACKUP_ROOT, day + "_asset_paths", HT.backup_rel_path(path, _P.ROOT))


def _write_if_changed(path, data):
    """Записать, только если содержимое другое; прежнюю версию — в бэкап (раз в день, сверка размера); атомарно.
    Тело — общий `asda_registry_io.write_if_changed` (набор бэкапа `_asset_paths`, временный файл `.tmp_asset_paths`)."""
    return _RIO.write_if_changed(path, data, _backup_path, ".tmp_asset_paths")


# ══ Модель: корни и правила ══════════════════════════════════════════════════════════════════════════════════
def resolve_paths(reg):
    """id → запись `paths` с полем `path` (полный /Game-путь), в порядке JSON."""
    out = {}
    for e in reg["paths"]:
        if e.get("parent"):
            full = out[e["parent"]]["path"] + "/" + e["rel"]
        else:
            full = e["path"]
        out[e["id"]] = dict(e, path=full)
    return out


def parse_pattern(pattern):
    """Шаблон → список частей: ('lit', текст) | ('tok', имя, формат_int|None, (да, нет)|None)."""
    parts, pos = [], 0
    for m in TOKEN.finditer(pattern):
        if m.start() > pos:
            parts.append(("lit", pattern[pos:m.start()]))
        cond = (m.group(3), m.group(4)) if m.group(3) is not None else None
        parts.append(("tok", m.group(1), m.group(2), cond))
        pos = m.end()
    if pos < len(pattern):
        parts.append(("lit", pattern[pos:]))
    return parts


def leaf_pattern(pattern):
    """Последний сегмент шаблона после «/» (метки «/» не содержат)."""
    return pattern.rsplit("/", 1)[-1]


def full_pattern(rule, paths):
    """Полный шаблон: корень + «/» + pattern + суффикс формы (.<лист> / .<лист>_C)."""
    pat = rule["pattern"]
    if rule.get("root"):
        pat = paths[rule["root"]]["path"] + "/" + pat
    form = rule.get("form", "name")
    if form == "object":
        pat += "." + leaf_pattern(rule["pattern"])
    elif form == "class":
        pat += "." + leaf_pattern(rule["pattern"]) + "_C"
    return pat


def var_types(rule, rules_by_id):
    """Имя переменной (аргумент или let) → тип."""
    t = {a["name"]: a["type"] for a in rule.get("args", [])}
    for let in rule.get("lets", []):
        t[let["name"]] = "str"
    return t


# ── Встроенные правила (не шаблон): ОДИН исходник Python (для блока и для интерпретатора) + тело C++ ─────────
BUILTIN_PY_SRC = {
    "ObjectPathFromPackage": '''
def object_path_from_package(key):
    """Пакетный путь → объектный: «/A/B/N» → «/A/B/N.N»; если у листа уже есть «.» или это не путь — как есть.
    Двойник C++ AsdaAssetPaths::ObjectPathFromPackage (кэши текстур и моделей)."""
    if key.startswith("/") and "/" in key:
        leaf = key[key.rfind("/") + 1:]
        if "." not in leaf:
            return key + "." + leaf
    return key
''',
    "PackageObjectPath": '''
def package_object_path(pkg):
    """Пакет → «<пакет>.<короткое имя>» без условий (короткое имя — после последнего «/», как FPackageName::GetShortName)."""
    return pkg + "." + pkg[pkg.rfind("/") + 1:]
''',
    "WidgetClassPath": '''
def widget_class_path(path):
    """Путь WBP → путь класса: «/A/WBP_X» → «/A/WBP_X.WBP_X_C», «/A/WBP_X.WBP_X» → «…_C»; с «_C» — как есть.
    Двойник C++ AsdaAssetPaths::WidgetClassPath (UAsdaWindowSubsystem::OpenWindowByPath)."""
    if not path.lower().endswith("_c"):
        if "." not in path and "/" in path:
            path = path + "." + path[path.rfind("/") + 1:]
        path += "_C"
    return path
''',
    "JebFromFxClassName": '''
_FX_JEB_DASH = None


def _fx_jeb_dash_table():
    """Обратная таблица D4 (лениво, из Tools/data/asda_asset_paths.json): имя без дефиса (нижний регистр) → имя .jeb."""
    global _FX_JEB_DASH
    if _FX_JEB_DASH is None:
        p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "asda_asset_paths.json")
        with open(p, encoding="utf-8") as f:
            names = _json.load(f).get("fx_jeb_dash_names", [])
        _FX_JEB_DASH = {n.replace("-", "_").lower(): n for n in names}
    return _FX_JEB_DASH


def jeb_from_fx_class_name(class_name):
    """Имя класса/ассета «BP_Fx_<jeb с «_»>[_C]» → имя .jeb. ★ ИСПРАВЛЕНИЕ D4: дефис восстанавливается по таблице
    реальных имён (правилом нельзя: 71 имя .jeb без дефиса тоже кончается на «_a_NN»). Регистр префикса/суффикса
    не важен — как FString::RemoveFromStart/RemoveFromEnd (ESearchCase::IgnoreCase)."""
    jeb = class_name
    if jeb.lower().endswith("_c"):
        jeb = jeb[:-2]
    if jeb.lower().startswith("bp_fx_"):
        jeb = jeb[6:]
    return _fx_jeb_dash_table().get(jeb.lower(), jeb)
''',
}

BUILTIN_CPP = {
    "ObjectPathFromPackage": '''	// Пакетный путь → объектный: «/A/B/N» → «/A/B/N.N»; если у листа уже есть «.» или ключ не путь — как есть.
	//   Одна точка вместо копий в UAsdaTextureSubsystem::ResolveAndCache и UAsdaModelAnimSubsystem::LoadOrCache.
	inline FString ObjectPathFromPackage(const FString& Key)
	{
		int32 SlashIdx = INDEX_NONE;
		if (Key.StartsWith(TEXT("/")) && Key.FindLastChar(TEXT('/'), SlashIdx))
		{
			const FString Leaf = Key.Mid(SlashIdx + 1);
			if (!Leaf.Contains(TEXT(".")))
			{
				return Key + TEXT(".") + Leaf;
			}
		}
		return Key;
	}
''',
    "PackageObjectPath": '''	// Пакет → «<пакет>.<короткое имя>» без условий; короткое имя — после последнего «/» (как FPackageName::GetShortName).
	inline FString PackageObjectPath(const FString& Pkg)
	{
		int32 SlashIdx = INDEX_NONE;
		Pkg.FindLastChar(TEXT('/'), SlashIdx);
		return Pkg + TEXT(".") + Pkg.Mid(SlashIdx + 1);
	}
''',
    "WidgetClassPath": '''	// Путь WBP → путь класса Blueprint: «/A/WBP_X» → «/A/WBP_X.WBP_X_C», «/A/WBP_X.WBP_X» → «…_C»; с «_C» — как есть.
	inline FString WidgetClassPath(const FString& WidgetClassPathIn)
	{
		FString Path = WidgetClassPathIn;
		if (!Path.EndsWith(TEXT("_C")))
		{
			if (!Path.Contains(TEXT(".")))
			{
				int32 Slash;
				if (Path.FindLastChar(TEXT('/'), Slash))
				{
					Path = Path + TEXT(".") + Path.Mid(Slash + 1);
				}
			}
			Path += TEXT("_C");
		}
		return Path;
	}
''',
    "JebFromFxClassName": None,   # рендерится отдельно (нужна таблица из JSON)
}


def make_runtime(reg):
    """Интерпретатор правил на Python: dict py-имя → функция. Шаблоны — по JSON, встроенные — из BUILTIN_PY_SRC."""
    paths = resolve_paths(reg)
    by_id = {r["id"]: r for r in reg["rules"]}
    import asda_map_registry as _MAPREG
    norm_fn = {"asset_name": _MAPREG.cpp_asset_name, "dash_to_us": lambda s: s.replace("-", "_")}
    ns = {"os": os, "_json": json, "__file__": OUT_PY}
    for rid, src in BUILTIN_PY_SRC.items():
        exec(src, ns)   # noqa: S102 — исходник свой, тот же текст уходит в блок asda_ue_paths.py
    funcs = {}

    def make(rule):
        if rule.get("impl") == "builtin":
            return ns[rule["py"]]
        parts = parse_pattern(full_pattern(rule, paths))

        def fn(*args, **kw):
            env = {}
            for i, a in enumerate(rule.get("args", [])):
                if i < len(args):
                    env[a["name"]] = args[i]
                elif a["name"] in kw:
                    env[a["name"]] = kw[a["name"]]
                elif a["py"] in kw:
                    env[a["name"]] = kw[a["py"]]
                elif "default" in a:
                    env[a["name"]] = a["default"]
                else:
                    raise TypeError("%s: нет аргумента %s" % (rule["id"], a["name"]))
            for let in rule.get("lets", []):
                if "rule" in let:
                    env[let["name"]] = funcs[by_id[let["rule"]]["py"]](*[env[x] for x in let["args"]])
                else:
                    env[let["name"]] = norm_fn[let["norm"]](env[let["arg"]])
            out = []
            for p in parts:
                if p[0] == "lit":
                    out.append(p[1])
                elif p[3] is not None:
                    out.append(p[3][0] if env[p[1]] else p[3][1])
                elif p[2]:
                    out.append(("%" + p[2]) % env[p[1]])
                else:
                    out.append(env[p[1]])
            return "".join(out)
        fn.__name__ = rule["py"]
        return fn

    for r in reg["rules"]:
        funcs[r["py"]] = make(r)
    return funcs


# ══ Рендер C++ ═══════════════════════════════════════════════════════════════════════════════════════════════
def _cpp_str(s):
    """Экранирование для TEXT("…")."""
    return s.replace("\\", "\\\\").replace("\"", "\\\"")


def _cpp_fmt(parts, types):
    """Части шаблона → (формат Printf, выражения аргументов)."""
    fmt, args = [], []
    for p in parts:
        if p[0] == "lit":
            fmt.append(_cpp_str(p[1]).replace("%", "%%"))
            continue
        name, ifmt, cond = p[1], p[2], p[3]
        t = types[name]
        if cond is not None:
            fmt.append("%s")
            args.append("(%s ? TEXT(\"%s\") : TEXT(\"%s\"))" % (name, _cpp_str(cond[0]), _cpp_str(cond[1])))
        elif t == "int":
            fmt.append("%" + (ifmt or "d"))
            args.append(name)
        elif t == "tchar":
            fmt.append("%s")
            args.append(name)
        else:
            fmt.append("%s")
            args.append("*" + name)
    return "".join(fmt), args


def _cpp_param(a):
    """Параметр функции C++."""
    t = a["type"]
    if t == "str":
        s = "const FString& %s" % a["name"]
        if "default" in a:
            s += " = FString()" if a["default"] == "" else " = TEXT(\"%s\")" % _cpp_str(a["default"])
        return s
    if t == "tchar":
        return "const TCHAR* %s" % a["name"]
    if t == "int":
        return "int32 %s" % a["name"]
    return "bool %s" % a["name"]


def _cpp_doc(text, indent="\t"):
    """Многострочный комментарий `// …`."""
    return "".join("%s// %s\n" % (indent, ln) if ln else "%s//\n" % indent for ln in text.split("\n"))


def render_cpp_rule(rule, paths, by_id):
    """Функция C++ шаблонного правила."""
    types = var_types(rule, by_id)
    parts = parse_pattern(full_pattern(rule, paths))
    fmt, args = _cpp_fmt(parts, types)
    s = _cpp_doc(rule["doc"])
    s += "\tinline FString %s(%s)\n\t{\n" % (rule["cpp"], ", ".join(_cpp_param(a) for a in rule.get("args", [])))
    for let in rule.get("lets", []):
        if "rule" in let:
            expr = "%s(%s)" % (by_id[let["rule"]]["cpp"], ", ".join(let["args"]))
        else:
            expr = NORM_CPP[let["norm"]].format(x=let["arg"])
        s += "\t\tconst FString %s = %s;\n" % (let["name"], expr)
    if args:
        s += "\t\treturn FString::Printf(TEXT(\"%s\"), %s);\n" % (fmt, ", ".join(args))
    else:
        s += "\t\treturn FString(TEXT(\"%s\"));\n" % fmt.replace("%%", "%")
    s += "\t}\n"
    return s


def render_cpp_d4(reg):
    """Обратная таблица D4 и JebFromFxClassName."""
    names = reg.get("fx_jeb_dash_names", [])
    s = ("\t// ★ ИСПРАВЛЕНИЕ D4 (волна 2 п. 7, 14.09.2026). Прежде (UAsdaFxLightSubsystem::ResolveJebName) имя .jeb из имени\n"
         "\t//   класса бралось как «BP_Fx_<x>_C» → «<x>» — с «_» вместо «-». Правилом дефис не вернуть: у 71 имени .jeb без\n"
         "\t//   дефиса тоже есть хвост «_a_NN». Поэтому — таблица РЕАЛЬНЫХ имён .jeb с дефисом, у которых в Content есть\n"
         "\t//   BP_Fx_* (%d шт.; пересборка: `python -B Tools/asda_asset_paths.py --refresh-fx-table`). Имена вне таблицы —\n"
         "\t//   как раньше. Регулярки семейств DefaultGame.ini на обоих написаниях дают то же семейство (стенд волны).\n"
         % len(names))
    s += "\tinline constexpr const TCHAR* kFxJebDashNames[] = {\n"
    for n in names:
        s += "\t\tTEXT(\"%s\"),\n" % _cpp_str(n)
    if not names:
        s += "\t\tTEXT(\"\"),\n"
    s += "\t};\n\n"
    s += ("\t// Ключ — имя с «_» вместо «-» (TMap<FString> сравнивает без учёта регистра), значение — имя .jeb.\n"
          "\tinline const TMap<FString, FString>& FxJebDashTable()\n\t{\n"
          "\t\tstatic const TMap<FString, FString> Table = []()\n\t\t{\n"
          "\t\t\tTMap<FString, FString> M;\n"
          "\t\t\tM.Reserve(UE_ARRAY_COUNT(kFxJebDashNames));\n"
          "\t\t\tfor (const TCHAR* Jeb : kFxJebDashNames)\n\t\t\t{\n"
          "\t\t\t\tconst FString S(Jeb);\n"
          "\t\t\t\tif (!S.IsEmpty()) { M.Add(S.Replace(TEXT(\"-\"), TEXT(\"_\")), S); }\n"
          "\t\t\t}\n\t\t\treturn M;\n\t\t}();\n\t\treturn Table;\n\t}\n\n")
    s += ("\t// Имя класса эффект-BP «BP_Fx_<jeb с «_»>_C» → имя .jeb (с дефисом, если оно есть в таблице выше).\n"
          "\tinline FString JebFromFxClassName(const FString& ClassName)\n\t{\n"
          "\t\tFString Jeb = ClassName;\n"
          "\t\tJeb.RemoveFromEnd(TEXT(\"_C\"));\n"
          "\t\tJeb.RemoveFromStart(TEXT(\"BP_Fx_\"));\n"
          "\t\tif (const FString* Dashed = FxJebDashTable().Find(Jeb)) { return *Dashed; }   // ★ ИСПРАВЛЕНИЕ D4\n"
          "\t\treturn Jeb;\n\t}\n")
    return s


def render_cpp_header(reg):
    """Полный текст AsdaAssetPaths.h (UTF-8 с BOM, CRLF — как AsdaMapRegistry.h)."""
    paths = resolve_paths(reg)
    by_id = {r["id"]: r for r in reg["rules"]}
    s = ("﻿// AsdaAssetPaths.h — РЕЕСТР ПУТЕЙ И ПРАВИЛ ИМЁН АССЕТОВ для C++: корни /Game, построение пути по имени из данных\n"
         "//   (часть экипировки, скелетал и клип, класс эффекта, звук, JUI-атлас, каталоги текстур карты…), формы ссылки.\n"
         "//\n"
         "// ★ ГЕНЕРИРУЕТСЯ Tools/asda_asset_paths.py из Tools/data/asda_asset_paths.json — НЕ ПРАВИТЬ РУКАМИ.\n"
         "//   Правка: источник (JSON) → `python -B Tools/asda_asset_paths.py`; сверка — `--check`; литералы после волны — `--scan`.\n"
         "//   Python-двойник — генерируемый блок Tools/asda_ue_paths.py (те же правила, имена в snake_case).\n"
         "//   Имя карты/звука приводится ОДНОЙ точкой — AsdaMapRegistry::ToAssetName (реестр карт), здесь не копируется.\n"
         "#pragma once\n\n"
         "#include \"CoreMinimal.h\"\n"
         "#include \"World/AsdaMapRegistry.h\"\n\n"
         "namespace AsdaAssetPaths\n{\n")
    s += "\t// ══ Корни и одиночные ассеты (пакетные пути) ══\n"
    for e in reg["paths"]:
        p = paths[e["id"]]
        if not (e.get("cpp") or e.get("cpp_prefix") or e.get("cpp_folder")):
            continue
        cmt = e.get("comment", "")
        if e.get("cpp"):
            s += "\tinline constexpr const TCHAR* %s = TEXT(\"%s\");%s\n" % (
                e["cpp"], _cpp_str(p["path"]), ("   // " + cmt) if cmt else "")
        if e.get("cpp_prefix"):
            s += "\tinline constexpr const TCHAR* %s = TEXT(\"%s/\");   // то же с «/» на конце (префикс для «+ имя»)\n" % (
                e["cpp_prefix"], _cpp_str(p["path"]))
        if e.get("cpp_folder"):
            rel = p["path"][len(paths["assets"]["path"]) + 1:]
            s += "\tinline constexpr const TCHAR* %s = TEXT(\"%s\");   // папка относительно %s (аргумент Folder)\n" % (
                e["cpp_folder"], _cpp_str(rel), paths["assets"]["path"])
    s += "\n\t// ══ Правила ══\n"
    for r in reg["rules"]:
        s += "\n"
        if r.get("impl") == "builtin":
            s += render_cpp_d4(reg) if r["id"] == "JebFromFxClassName" else BUILTIN_CPP[r["id"]]
        else:
            s += render_cpp_rule(r, paths, by_id)
    s += "}\n"
    return s.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8")


# ══ Рендер Python-блока ══════════════════════════════════════════════════════════════════════════════════════
def render_py_rule(rule, paths, by_id):
    """Функция Python шаблонного правила."""
    types = var_types(rule, by_id)
    py_of = {a["name"]: a["py"] for a in rule.get("args", [])}
    for let in rule.get("lets", []):
        py_of[let["name"]] = let["py"]
    parts = parse_pattern(full_pattern(rule, paths))
    fmt, args = [], []
    for p in parts:
        if p[0] == "lit":
            fmt.append(p[1].replace("\\", "\\\\").replace("\"", "\\\"").replace("%", "%%"))
        elif p[3] is not None:
            fmt.append("%s")
            args.append("(\"%s\" if %s else \"%s\")" % (p[3][0], py_of[p[1]], p[3][1]))
        elif types[p[1]] == "int":
            fmt.append("%" + (p[2] or "d"))
            args.append(py_of[p[1]])
        else:
            fmt.append("%s")
            args.append(py_of[p[1]])
    params = []
    for a in rule.get("args", []):
        params.append(a["py"] + ("=%s" % json.dumps(a["default"]) if "default" in a else ""))
    s = "\n\ndef %s(%s):\n" % (rule["py"], ", ".join(params))
    s += "    \"\"\"%s\n    Двойник C++ AsdaAssetPaths::%s.\"\"\"\n" % (rule["doc"].replace("\n", "\n    "), rule["cpp"])
    for let in rule.get("lets", []):
        if "rule" in let:
            expr = "%s(%s)" % (by_id[let["rule"]]["py"], ", ".join(py_of[x] for x in let["args"]))
        else:
            expr = NORM_PY[let["norm"]].format(x=py_of[let["arg"]])
        s += "    %s = %s\n" % (let["py"], expr)
    if args:
        s += "    return \"%s\" %% (%s,)\n" % ("".join(fmt), ", ".join(args))
    else:
        s += "    return \"%s\"\n" % "".join(fmt).replace("%%", "%")
    return s


def render_py_block(reg):
    """Текст генерируемого блока asda_ue_paths.py (между маркерами, маркеры включены; LF)."""
    paths = resolve_paths(reg)
    by_id = {r["id"]: r for r in reg["rules"]}
    s = PY_BEGIN + "\n"
    s += ("# Корни /Game и одиночные ассеты (имена и значения — прежние) + функции правил пути (двойники C++\n"
          "#   Source/AsdaReborn/Public/Resources/AsdaAssetPaths.h). Правка — Tools/data/asda_asset_paths.json.\n"
          "import json as _json\n"
          "import asda_map_registry as _MAPREG   # ★ приведение имени карты/звука — реестр карт (cpp_asset_name), не копия\n\n")
    for e in reg["paths"]:
        if not e.get("py"):
            continue
        p = paths[e["id"]]
        if e.get("obsolete"):
            s += "# ★ УСТАРЕЛО (D5, волна 2 п. 7): %s\n" % e["obsolete"]
        if e.get("parent") and paths[e["parent"]].get("py"):
            val = "%s + \"/%s\"" % (paths[e["parent"]]["py"], e["rel"])
        else:
            val = "\"%s\"" % p["path"]
        line = "%s = %s" % (e["py"], val)
        if e.get("comment"):
            line = "%-40s  # %s" % (line, e["comment"])
        s += line + "\n"
    for r in reg["rules"]:
        if r.get("impl") == "builtin":
            s += "\n" + BUILTIN_PY_SRC[r["id"]].rstrip("\n") + "\n"
        else:
            s += render_py_rule(r, paths, by_id)
    s += PY_END + "\n"
    return s


def render_py_file(reg, current):
    """asda_ue_paths.py с заменённым блоком. Нет маркеров — ошибка (блок ставится один раз вручную)."""
    text = current.decode("utf-8")
    eol = "\r\n" if "\r\n" in text else "\n"
    t = text.replace("\r\n", "\n")
    b, e = t.find(PY_BEGIN), t.find(PY_END)
    if b < 0 or e < 0 or e < b:
        raise SystemExit("в %s нет маркеров генерируемого блока (%s … %s)" % (OUT_PY, PY_BEGIN[:40], PY_END))
    e = t.index("\n", e) + 1
    t = t[:b] + render_py_block(reg) + t[e:]
    return t.replace("\n", eol).encode("utf-8")


def rendered_outputs(reg):
    """[(путь, байты)] всех генерируемых файлов."""
    return [(OUT_CPP, render_cpp_header(reg)), (OUT_PY, render_py_file(reg, _read_bytes(OUT_PY) or b""))]


def generate(write=True, show_diff=False, reg=None):
    """Сгенерировать файлы. Возвращает список изменившихся (или тех, что изменились бы).
    Тело — общий `asda_registry_io.generate` (сверка реестра → отрисовка → запись с бэкапом)."""
    reg = reg or load_registry()
    return _RIO.generate(reg, check_registry, rendered_outputs, _write_if_changed, write=write, show_diff=show_diff)


# ══ Сверка ═══════════════════════════════════════════════════════════════════════════════════════════════════
def check_registry(reg):
    """Целостность JSON. → (ошибки, предупреждения, сведения)."""
    errs, warns, info = [], [], []
    ids, pys, cpps = set(), set(), set()
    for e in reg.get("paths", []):
        if e["id"] in ids:
            errs.append("paths: повтор id %s" % e["id"])
        ids.add(e["id"])
        if e.get("parent") and e["parent"] not in ids:
            errs.append("paths %s: родитель %s не объявлен выше" % (e["id"], e["parent"]))
        if not e.get("parent") and not e.get("path", "").startswith("/Game"):
            errs.append("paths %s: корень без parent должен начинаться с /Game" % e["id"])
        if e.get("kind") not in ("dir", "asset"):
            errs.append("paths %s: kind должен быть dir|asset" % e["id"])
        for k in ("py", "cpp", "cpp_prefix", "cpp_folder"):
            v = e.get(k)
            if not v:
                continue
            bag = pys if k == "py" else cpps
            if v in bag:
                errs.append("paths %s: повтор имени %s" % (e["id"], v))
            bag.add(v)
    if "assets" not in ids:
        errs.append("paths: нет корня assets (/Game/AsdaAssets)")
    rids = set()
    for r in reg.get("rules", []):
        rid = r.get("id", "?")
        if rid in rids:
            errs.append("rules: повтор id %s" % rid)
        for k in ("cpp", "py", "doc"):
            if not r.get(k):
                errs.append("rules %s: нет поля %s" % (rid, k))
        if r.get("cpp") in cpps or r.get("py") in pys:
            errs.append("rules %s: имя совпадает с константой" % rid)
        cpps.add(r.get("cpp")); pys.add(r.get("py"))
        if r.get("impl") == "builtin":
            if rid not in BUILTIN_PY_SRC:
                errs.append("rules %s: встроенного правила с таким id нет в генераторе" % rid)
            rids.add(rid)
            continue
        if r.get("form", "name") not in FORMS:
            errs.append("rules %s: form %s" % (rid, r.get("form")))
        if r.get("root") and r["root"] not in ids:
            errs.append("rules %s: корень %s не объявлен" % (rid, r["root"]))
        names = set()
        for a in r.get("args", []):
            if a.get("type") not in ARG_TYPES or not IDENT.match(a.get("name", "")) or not IDENT.match(a.get("py", "")):
                errs.append("rules %s: аргумент %s" % (rid, a))
            names.add(a.get("name"))
        for let in r.get("lets", []):
            if "rule" in let:
                if let["rule"] not in rids:
                    errs.append("rules %s: let %s ссылается на правило %s, не объявленное выше" % (rid, let["name"], let["rule"]))
                for x in let.get("args", []):
                    if x not in names:
                        errs.append("rules %s: let %s — нет переменной %s" % (rid, let["name"], x))
            elif let.get("norm") not in NORM_CPP or let.get("arg") not in names:
                errs.append("rules %s: let %s — нормализатор/аргумент" % (rid, let.get("name")))
            names.add(let["name"])
        for p in parse_pattern(r.get("pattern", "")):
            if p[0] == "tok" and p[1] not in names:
                errs.append("rules %s: метка {%s} не аргумент и не let" % (rid, p[1]))
        rids.add(rid)
    for v in reg.get("vectors", []):
        if v.get("rule") not in rids:
            errs.append("vectors: правило %s не объявлено" % v.get("rule"))
    return errs, warns, info


def check_generated(reg):
    """Генерируемые файлы актуальны."""
    errs = []
    for path, data in rendered_outputs(reg):
        if _read_bytes(path) != data:
            errs.append("генерируемый файл устарел: %s (запустите python -B Tools/asda_asset_paths.py)" % path)
    return errs


_CONTENT_CACHE = {}


def content_index():
    """(пакеты, папки) Content в нижнем регистре: «/game/…» (обход диска, только чтение)."""
    if "idx" not in _CONTENT_CACHE:
        pk, dirs = set(), set()
        for dp, dn, fn in os.walk(CONTENT_DIR):
            rel = os.path.relpath(dp, CONTENT_DIR).replace("\\", "/")
            base = "/game" if rel == "." else "/game/" + rel.lower()
            dirs.add(base)
            for f in fn:
                if f.endswith((".uasset", ".umap")):
                    pk.add(base + "/" + os.path.splitext(f)[0].lower())
        _CONTENT_CACHE["idx"] = (pk, dirs)
    return _CONTENT_CACHE["idx"]


def check_disk(reg):
    """Корни и одиночные ассеты реестра на диске. Устаревшие (obsolete) обязаны отсутствовать."""
    errs, warns, info = [], [], []
    pk, dirs = content_index()
    for e in resolve_paths(reg).values():
        low = e["path"].lower()
        exists = (low in dirs) if e["kind"] == "dir" else (low in pk)
        if e["kind"] == "dir" and exists and not any(p.startswith(low + "/") for p in pk):
            info.append("папка %s есть, но пакетов в ней нет" % e["path"])
            if e.get("obsolete"):
                exists = False   # у устаревшей папки важны ПАКЕТЫ: пустая папка = пометка верна
        if e.get("obsolete"):
            (warns if exists else info).append("%s %s: помечен устаревшим (%s) — %s" % (
                e["kind"], e["path"], e["obsolete"], "ЕСТЬ на диске, снять пометку?" if exists else "на диске нет, как и ожидалось"))
        elif not exists:
            errs.append("%s %s (%s) нет в Content" % (e["kind"], e["path"], e["id"]))
    return errs, warns, info


def check_vectors(reg):
    """Контрольные векторы: интерпретатор JSON и сгенерированный блок Python дают ожидаемую строку."""
    errs = []
    rt = make_runtime(reg)
    by_id = {r["id"]: r for r in reg["rules"]}
    blk = {"os": os, "__file__": OUT_PY}
    exec(render_py_block(reg), blk)   # noqa: S102 — ровно тот текст, что пишется в asda_ue_paths.py
    for v in reg.get("vectors", []):
        r = by_id[v["rule"]]
        args = [v["in"][a["name"]] if a["name"] in v["in"] else a.get("default") for a in r.get("args", [])]
        for label, fn in (("JSON", rt[r["py"]]), ("блок", blk[r["py"]])):
            got = fn(*args)
            if got != v["out"]:
                errs.append("вектор %s(%s) [%s]: %r, ожидалось %r" % (v["rule"], v["in"], label, got, v["out"]))
    return errs, len(reg.get("vectors", []))


FX_FAMILIES_INI = os.path.join(PROJECT_DIR, "Config", "DefaultGame.ini")


def read_fx_families(path=FX_FAMILIES_INI):
    """Семейства света эффектов из DefaultGame.ini: [(Id, регулярка Jeb)] в порядке файла (первое совпадение — победитель,
    без учёта регистра — как UAsdaFxLightSettings::FindRule)."""
    if not os.path.isfile(path):
        return []
    with open(path, encoding="utf-8-sig", errors="replace") as f:
        return re.findall(r'^\+Families=\(Id="([^"]+)",Jeb="((?:[^"\\]|\\.)*)"', f.read(), re.M)


def _fx_family(families, jeb):
    """Id первого семейства, чья регулярка находит имя (None — ни одного)."""
    for fid, rx in families:
        try:
            if re.search(rx, jeb, re.I):
                return fid
        except re.error:
            continue
    return None


def scan_fx_dash_names():
    """(имена, исключённые) — реальные имена .jeb с «-», у которых в Content есть BP_Fx_<jeb с «_»> (точный регистр).
    ★ Исключаются имена, у которых семейство света DefaultGame.ini на «-» и на «_» разное: D4 не должен ломать прежнее
    совпадение (14.09: одно такое — test-000, регулярка ui_markers «test_»)."""
    if not os.path.isdir(FX_JEB_DIR):
        return None, None
    jebs = sorted({os.path.splitext(f)[0] for f in os.listdir(FX_JEB_DIR) if f.lower().endswith(".jeb")})
    bps = {os.path.splitext(f)[0][len("BP_Fx_"):] for f in os.listdir(FX_CONTENT_DIR)
           if f.startswith("BP_Fx_") and f.endswith(".uasset")}
    fams = read_fx_families()
    names, excluded = [], []
    for j in jebs:
        if "-" not in j or j.replace("-", "_") not in bps:
            continue
        a, b = _fx_family(fams, j.replace("-", "_")), _fx_family(fams, j)
        if a != b:
            excluded.append({"jeb": j, "why": "семейство света на «_» = %s, на «-» = %s — прежнее совпадение сохранено" % (a, b)})
        else:
            names.append(j)
    return names, excluded


def check_fx_table(reg):
    """Таблица D4 в JSON совпадает с диском (.jeb оригинала × BP_Fx_* в Content × семейства DefaultGame.ini)."""
    warns, info = [], []
    cur, exc = scan_fx_dash_names()
    have = reg.get("fx_jeb_dash_names", [])
    if cur is None:
        info.append("таблица D4: каталога .jeb нет (%s) — сверка пропущена" % FX_JEB_DIR)
    elif cur != have or exc != reg.get("fx_jeb_dash_excluded", []):
        warns.append("таблица D4 разошлась с диском/ini: в JSON %d, на диске %d (+%d/−%d), исключений %d/%d — `--refresh-fx-table`" % (
            len(have), len(cur), len(set(cur) - set(have)), len(set(have) - set(cur)),
            len(reg.get("fx_jeb_dash_excluded", [])), len(exc)))
    else:
        info.append("таблица D4: %d имён (+%d исключено по семействам), совпадает с диском" % (len(have), len(exc)))
    return warns, info


def _code_lines(path):
    """Строки файла (utf-8, с заменой ошибок)."""
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read().splitlines()


def _is_comment_or_log(line, py):
    """Строка-комментарий или лог — литералы в ней не считаем."""
    s = line.strip()
    if py:
        return s.startswith("#")
    return s.startswith("//") or s.startswith("*") or s.startswith("/*") or "UE_LOG" in s


def _cpp_skip_mask(lines):
    """Номера строк C++ (с 1), которые не код пути: комментарии и ВСЁ тело UE_LOG(…), в т. ч. строки-продолжения
    многострочного лога (без маски текст лога «…/Maps/%s/» на второй строке считался бы шаблоном)."""
    mask, depth = set(), 0
    for i, line in enumerate(lines, 1):
        s = line.strip()
        if depth > 0 or "UE_LOG" in s:
            mask.add(i)
            if "UE_LOG" in s and depth == 0:
                depth = 0
                s = s[s.index("UE_LOG"):]
            depth += s.count("(") - s.count(")")
            if depth <= 0:
                depth = 0
            continue
        if s.startswith("//") or s.startswith("*") or s.startswith("/*"):
            mask.add(i)
    return mask


def check_consumers(reg):
    """Объявленные потребители действительно зовут правило; старых шаблонов C++ не осталось вне заголовка."""
    errs, warns = [], []
    for r in reg["rules"]:
        for c in r.get("cpp_consumers", []):
            path = os.path.join(PROJECT_DIR, c.split("::")[0])
            if not os.path.isfile(path):
                errs.append("правило %s: файла потребителя нет: %s" % (r["id"], c))
            elif ("AsdaAssetPaths::" + r["cpp"]) not in open(path, encoding="utf-8", errors="replace").read():
                errs.append("правило %s: %s не зовёт AsdaAssetPaths::%s" % (r["id"], c, r["cpp"]))
        for c in r.get("py_consumers", []):
            path = os.path.join(PROJECT_DIR, c)
            if not os.path.isfile(path):
                errs.append("правило %s: файла потребителя нет: %s" % (r["id"], c))
            elif not re.search(r"\.%s\(" % re.escape(r["py"]), open(path, encoding="utf-8", errors="replace").read()):
                errs.append("правило %s: %s не зовёт .%s(" % (r["id"], c, r["py"]))
    forb = reg.get("cpp_forbidden_templates", [])
    if forb:
        for root in ("Source", "Plugins"):
            for dp, dn, fn in os.walk(os.path.join(PROJECT_DIR, root)):
                dn[:] = [d for d in dn if d not in ("Intermediate", "Binaries")]
                for f in fn:
                    if not f.endswith((".h", ".cpp")):
                        continue
                    path = os.path.join(dp, f)
                    if os.path.abspath(path) == os.path.abspath(OUT_CPP):
                        continue
                    lines = _code_lines(path)
                    skip = _cpp_skip_mask(lines)
                    for i, line in enumerate(lines, 1):
                        if i in skip:
                            continue
                        for pat in forb:
                            if pat in line:
                                errs.append("старый шаблон «%s» вне реестра: %s:%d" % (pat, os.path.relpath(path, PROJECT_DIR), i))
    return errs, warns


def check_itemdb_manifest(reg):
    """AsdaItemDbManifest.h (генерирует decode_item_db.py) лежит под корнем data реестра."""
    warns = []
    p = os.path.join(PROJECT_DIR, "Source", "AsdaReborn", "Public", "Items", "AsdaItemDbManifest.h")
    if not os.path.isfile(p):
        return ["нет %s" % p]
    root = resolve_paths(reg)["data"]["path"] + "/"
    bad = [m for m in re.findall(r'TEXT\("(/Game/[^"]*)"\)', open(p, encoding="utf-8", errors="replace").read())
           if not m.startswith(root)]
    if bad:
        warns.append("AsdaItemDbManifest.h: %d путей не под %s (пример %s)" % (len(bad), root, bad[0]))
    return warns


def run_check():
    """--check целиком. Код 1 при ошибках."""
    reg = load_registry()
    errs, warns, info = check_registry(reg)
    if not errs:
        errs += check_generated(reg)
        e, w, i = check_disk(reg)
        errs += e; warns += w; info += i
        ve, nvec = check_vectors(reg)
        errs += ve
        info.append("векторов проверено: %d (интерпретатор JSON + сгенерированный блок Python)" % nvec)
        w, i = check_fx_table(reg)
        warns += w; info += i
        e, w = check_consumers(reg)
        errs += e; warns += w
        warns += check_itemdb_manifest(reg)
    for x in info:
        print("  сведения: " + x)
    for x in warns:
        print("  ПРЕДУПРЕЖДЕНИЕ: " + x)
    for x in errs:
        print("  ОШИБКА: " + x)
    print("--check: ошибок %d, предупреждений %d" % (len(errs), len(warns)))
    return 1 if errs else 0


# ══ --scan: литералы /Game/ после волн ═══════════════════════════════════════════════════════════════════════
LIT_RE = re.compile(r"(\"|')((?:(?!\1)[^\\\n]|\\.)*?/Game(?:/(?:(?!\1)[^\\\n]|\\.)*?)?)\1")


def _scan_files(reg):
    """(относительный путь, язык, проба?) всех файлов для --scan."""
    cfg = reg.get("scan", {})
    excl = set(cfg.get("exclude_dirs", []))
    skip = {os.path.normcase(os.path.join(PROJECT_DIR, x)) for x in cfg.get("skip_files", [])}
    for root, exts, py in (("Source", (".h", ".cpp"), False), ("Plugins", (".h", ".cpp"), False), ("Tools", (".py",), True)):
        for dp, dn, fn in os.walk(os.path.join(PROJECT_DIR, root)):
            dn[:] = [d for d in dn if d not in excl and not d.startswith("_backup")]
            for f in fn:
                if not f.endswith(exts):
                    continue
                path = os.path.join(dp, f)
                if os.path.normcase(path) in skip:
                    continue
                probe = py and any(fnmatch.fnmatch(f, g) for g in cfg.get("probe_globs", []))
                yield os.path.relpath(path, PROJECT_DIR).replace("\\", "/"), py, probe


def scan_literals(reg):
    """→ список (rel, line, literal, kind, probe); kind: template | registry_copy | singleton_ok | singleton_missing."""
    pk, dirs = content_index()
    reg_paths = {e["path"].lower() for e in resolve_paths(reg).values()}
    out = []
    for rel, py, probe in _scan_files(reg):
        lines = _code_lines(os.path.join(PROJECT_DIR, rel))
        skip = set() if py else _cpp_skip_mask(lines)
        in_block = False
        for i, line in enumerate(lines, 1):
            if py and line.startswith(PY_BEGIN[:30]):
                in_block = True
            if in_block:
                if line.startswith(PY_END):
                    in_block = False
                continue
            if "/Game" not in line or i in skip or _is_comment_or_log(line, py):
                continue
            for m in LIT_RE.finditer(line):
                lit = m.group(2)
                if "/Game" not in lit or not (lit.startswith("/Game") or "%" in lit or "{" in lit):
                    continue
                after, before = line[m.end():], line[:m.start()]
                concat = bool(re.match(r"^\)*\s*\+", after)) or bool(re.search(r"\+\s*(TEXT\(|FString\(TEXT\()?$", before)) \
                    or bool(re.match(r"^\)*\s*%", after))
                low = lit.rstrip("/").lower()
                if "%" in lit or "{" in lit or lit.endswith("/") or concat:
                    kind = "registry_copy" if (low in reg_paths and not ("%" in lit or "{" in lit)) else "template"
                elif low in reg_paths:
                    kind = "registry_copy"
                else:
                    obj = low.split(".", 1)[0]
                    kind = "singleton_ok" if (obj in pk or obj in dirs) else "singleton_missing"
                out.append((rel, i, lit, kind, probe))
    return out


def run_scan(update_baseline=False):
    """--scan. Код 1, если в Source/Plugins/непробных Tools появились шаблоны или копии корней сверх базы."""
    reg = load_registry()
    hits = scan_literals(reg)
    cur = {}
    for rel, i, lit, kind, probe in hits:
        if kind in ("template", "registry_copy"):
            cur.setdefault(rel, {}).setdefault(lit, 0)
            cur[rel][lit] += 1
    if update_baseline:
        data = (json.dumps({"_comment": "База --scan (Tools/asda_asset_paths.py): шаблоны и копии корней /Game, существовавшие "
                                        "на момент принятия. Новые сверх базы — ошибка. Обновлять только после разбора.",
                            "date": datetime.date.today().isoformat(), "files": cur},
                           ensure_ascii=False, indent=1, sort_keys=True) + "\n").encode("utf-8")
        _write_if_changed(BASELINE_JSON, data)
        print("  база --scan записана: %s (файлов %d, литералов %d)" % (BASELINE_JSON, len(cur), sum(sum(v.values()) for v in cur.values())))
    base = {}
    if os.path.isfile(BASELINE_JSON):
        with open(BASELINE_JSON, encoding="utf-8") as f:
            base = json.load(f).get("files", {})
    errs, warns = [], []
    seen = {}
    stat = {}
    for rel, i, lit, kind, probe in hits:
        stat[(kind, probe)] = stat.get((kind, probe), 0) + 1
        if kind in ("template", "registry_copy"):
            seen.setdefault((rel, lit), 0)
            seen[(rel, lit)] += 1
            if seen[(rel, lit)] > base.get(rel, {}).get(lit, 0):
                msg = "новый %s вне реестра: %s:%d  %s" % ("шаблон" if kind == "template" else "дубль корня", rel, i, lit)
                (warns if probe else errs).append(msg)
        elif kind == "singleton_missing" and not probe:
            warns.append("ассета нет в Content: %s:%d  %s" % (rel, i, lit))
    gone = sum(max(0, n - seen.get((rel, lit), 0)) for rel, d in base.items() for lit, n in d.items())
    print("--scan: литералов /Game %d" % len(hits))
    for (kind, probe), n in sorted(stat.items()):
        print("  %-18s %s: %d" % (kind, "пробы _*.py" if probe else "код/инструменты", n))
    if gone:
        print("  сведения: из базы исчезло %d литералов (переведены на реестр) — можно `--scan --update-baseline`" % gone)
    for x in warns:
        print("  ПРЕДУПРЕЖДЕНИЕ: " + x)
    for x in errs:
        print("  ОШИБКА: " + x)
    print("--scan: ошибок %d, предупреждений %d" % (len(errs), len(warns)))
    return 1 if errs else 0


# ══ --verify-data: правила на реальных данных ════════════════════════════════════════════════════════════════
def _csv_rows(path, enc="utf-8-sig"):
    """Строки CSV как dict."""
    import csv
    with open(path, encoding=enc, newline="") as f:
        return list(csv.DictReader(f))


def run_verify_data():
    """Каждое правило на реальных входах → есть ли пакет/папка в Content. Ошибка — только нарушение раскладки
    (папка под корнем без пакета по правилу) сверх `verify_known_gaps`; пробелы контента — сведения."""
    reg = load_registry()
    R = make_runtime(reg)
    P = resolve_paths(reg)
    pk, dirs = content_index()
    raw = os.path.join(PROJECT_DIR, "RawAssets", "Data")
    known = set(reg.get("verify_known_gaps", []))
    errs, rows = [], []

    def has(p):
        return p.split(".", 1)[0].lower() in pk

    def has_dir(p):
        return p.lower() in dirs

    def put(name, total, miss, note=""):
        rows.append((name, total, len(miss), sorted(miss)[:8], note))

    def subdirs(root):
        low = root.lower() + "/"
        return sorted({d[len(low):].split("/")[0] for d in dirs if d.startswith(low)})

    # A. Раскладки: каждая папка под корнем обязана содержать пакет по правилу.
    layout = [
        ("EquipPart", "equip_parts", lambda m: R["equip_part_mesh"](m)),
        ("CharStaticMesh", "models_char", lambda m: R["char_static_mesh_object"](m)),
        ("LootMesh", "models_items", lambda m: R["loot_mesh_object"](m)),
        ("MobStaticMesh", "mob", lambda m: R["mob_static_mesh_object"](m)),
    ] + [("SkelMesh " + rid, rid, (lambda rr: (lambda m: R["skel_mesh"](P[rr]["path"], m)))(rid))
         for rid in ("mobskel", "models_npc", "pet", "vehicle", "soulguard", "clanflag", "characters", "gachya")]
    for name, rid, fn in layout:
        real_root = [d for d in dirs if d == P[rid]["path"].lower()]
        ds = subdirs(real_root[0]) if real_root else []
        miss = [d for d in ds if not has(fn(d)) and ("%s:%s" % (rid, d)) not in known]
        put("A раскладка %s (%s)" % (name, P[rid]["path"]), len(ds), miss)
        errs += ["раскладка %s: папка %s без пакета по правилу" % (name, d) for d in miss]

    # B. Эффекты: имена jeb из CSV → класс BP.
    jebs = set()
    for r in _csv_rows(os.path.join(raw, "SkillFx.csv")):
        if r.get("HitFx"):
            jebs.add(r["HitFx"])
    for r in _csv_rows(os.path.join(raw, "BuffFx.csv")):
        for col in ("PlayerFx", "MobFx"):
            for part in (r.get(col) or "").split(";"):
                j = part.split("|")[0].strip()
                if j:
                    jebs.add(j)
    for r in _csv_rows(os.path.join(raw, "RideEffect.csv")):
        if r.get("EffectName"):
            jebs.add(os.path.splitext(r["EffectName"])[0])
    sg = _csv_rows(os.path.join(raw, "SoulGuarder.csv"))
    for r in sg:
        for c in ("EffSkill1", "EffSkill2", "EffSkill3"):
            if r.get(c):
                jebs.add(os.path.splitext(r[c])[0])
    put("B FxClassPathFromJeb (CSV)", len(jebs), [j for j in jebs if not has(R["fx_class_path_from_jeb"](j))],
        "пробелы контента (BP не собран) — не дефект правила")
    bpn = [os.path.splitext(f)[0] for f in os.listdir(FX_CONTENT_DIR) if f.startswith("BP_Fx_") and f.endswith(".uasset")]
    back = [b for b in bpn if R["fx_bp_name"](R["jeb_from_fx_class_name"](b + "_C")) != b]
    put("B' обратимость JebFromFxClassName → FxBpName (все BP_Fx_*)", len(bpn), back)
    errs += ["обратимость D4: %s" % b for b in back]

    # C. Звуки.
    snd = set()
    for r in _csv_rows(os.path.join(raw, "BuffFx.csv")):
        if (r.get("Wav") or "").strip():
            snd.add(os.path.splitext(r["Wav"].strip())[0])
    for r in _csv_rows(os.path.join(raw, "RideEffect.csv")):
        for c in ("SoundName", "SoundName2", "SoundName3"):
            if (r.get(c) or "").strip():
                snd.add(os.path.splitext(r[c].strip())[0])
    folder = P["sound"]["path"][len(P["assets"]["path"]) + 1:]
    put("C SoundObject (Sound, CSV)", len(snd), [s for s in snd if not has(R["sound_object"](folder, s))], "пробелы контента")
    mreg = json.load(open(os.path.join(TOOLS_DIR, "data", "asda_map_registry.json"), encoding="utf-8"))
    mfolder = P["sounds_music"]["path"][len(P["assets"]["path"]) + 1:]
    bgm = {m[k] for m in mreg["maps"] for k in ("bgm_town", "bgm_field") if m.get(k)}
    put("C' SoundObject (Sounds/Music, реестр карт)", len(bgm), [b for b in bgm if not has(R["sound_object"](mfolder, b))], "пробелы контента")

    # D. Предметы.
    img = _csv_rows(os.path.join(raw, "ItemDB", "item_img_table.csv"))
    tex, atl = set(), set()
    for r in img:
        for c in ("TexMale", "TexFemale"):
            if r.get(c):
                tex.add(r[c])
        for c in ("IconAtlasMale", "IconAtlasFemale"):
            if r.get(c):
                atl.add(r[c])
    put("D CharTextureObject | LootTextureObject (item_img_table)", len(tex),
        [t for t in tex if not has(R["char_texture_object"](t)) and not has(R["loot_texture_object"](t))], "пробелы контента")
    put("D' JuiTextureObject (атласы иконок)", len(atl), [a for a in atl if not has(R["jui_texture_object"](a))], "пробелы контента")
    faces = [R["face_texture_name"](g, i) for g in (True, False) for i in range(12)]
    put("D'' FaceTextureName → CharTextureObject (0..11)", len(faces), [f for f in faces if not has(R["char_texture_object"](f))],
        "лица 11 нет в клиенте")

    # E. NPC.
    npc = {r["ModelName"] for r in _csv_rows(os.path.join(raw, "NPC", "NpcQuestNpc.csv")) if r.get("ModelName")}
    put("E SkelMesh NPC", len(npc), [m for m in npc if not has(R["skel_mesh"](P["models_npc"]["path"], m))], "пробелы контента")
    put("E' NpcPortraitObject", len(npc), [m for m in npc if not has(R["npc_portrait_object"](m))], "портретов нет и в оригинале")

    # F. Карты (реестр карт).
    load_miss, tex_miss = [], []
    for m in mreg["maps"]:
        d = m["dir"]
        if not any(p.startswith(R["map_load_dir"](d).lower() + "/") for p in pk):
            load_miss.append(d)
        if not has_dir(R["map_tex_dir"](d)):
            tex_miss.append(d)
    put("F MapLoadDir (картинки загрузки; D1)", len(mreg["maps"]), load_miss, "пусто → start_logo, как в эталоне")
    put("F' MapTexDir (текстуры карты; D2)", len(mreg["maps"]), tex_miss)

    # G. Питомцы, хранители, клипы навыков.
    pets = [r["ModelName"] for r in _csv_rows(os.path.join(raw, "PetModels.csv")) if r.get("ModelName")]
    put("G SkelAnim Pet 0..2", len(pets) * 3,
        ["%s:%d" % (m, k) for m in pets for k in range(3) if not has(R["skel_anim"](P["pet"]["path"], m, k))])
    skc = _csv_rows(os.path.join(raw, "SkillClips.csv"))
    miss, lt10, tot = [], 0, 0
    for r in skc:
        try:
            idx = int(r["AnimIdx"])
        except (KeyError, ValueError):
            continue
        tot += 1
        lt10 += idx < 10
        male = r["Gender"].strip().lower() != "f"
        if not has(R["skel_anim"](P["characters"]["path"], R["char_preview_name"](male), idx)):
            miss.append("%s:%d" % (r.get("SkillId"), idx))
    put("G' SkelAnim клипы навыков (D3)", tot, miss, "AnimIdx<10: %d строк" % lt10)

    for name, total, nmiss, ex, note in rows:
        print("  %-66s всего=%-5d нет=%-4d %s" % (name, total, nmiss, note))
        for x in ex:
            print("        - %s" % x)
    for x in errs:
        print("  ОШИБКА: " + x)
    print("--verify-data: ошибок %d" % len(errs))
    return 1 if errs else 0


def refresh_fx_table():
    """--refresh-fx-table: пересобрать `fx_jeb_dash_names` в JSON (с бэкапом)."""
    reg = load_registry()
    cur, exc = scan_fx_dash_names()
    if cur is None:
        raise SystemExit("нет каталога .jeb: %s" % FX_JEB_DIR)
    reg["fx_jeb_dash_names"] = cur
    reg["fx_jeb_dash_excluded"] = exc
    changed = _write_if_changed(REGISTRY_JSON, dump_registry(reg))
    print("  таблица D4: %d имён, исключено по семействам %d %s — %s" % (
        len(cur), len(exc), [x["jeb"] for x in exc], "JSON ЗАПИСАН" if changed else "без изменений"))


def main(argv=None):
    """Точка входа командной строки."""
    ap = argparse.ArgumentParser(description="Реестр путей и правил имён ассетов: генерация и сверка")
    ap.add_argument("--diff", action="store_true", help="показать разницу, ничего не писать")
    ap.add_argument("--check", action="store_true", help="сверка, ничего не пишет")
    ap.add_argument("--scan", action="store_true", help="литералы /Game/ в коде и инструментах (проверка после волн)")
    ap.add_argument("--update-baseline", action="store_true", help="с --scan: принять текущие шаблоны как базу")
    ap.add_argument("--verify-data", action="store_true", help="правила на реальных данных против Content")
    ap.add_argument("--refresh-fx-table", action="store_true", help="пересобрать таблицу D4 в JSON")
    a = ap.parse_args(argv)
    if a.check:
        return run_check()
    if a.scan:
        return run_scan(a.update_baseline)
    if a.verify_data:
        return run_verify_data()
    if a.refresh_fx_table:
        refresh_fx_table()
        return 0
    generate(write=not a.diff, show_diff=a.diff)
    return 0


if __name__ == "__main__":
    sys.exit(main())
