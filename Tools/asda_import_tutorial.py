# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_tutorial.py — КНИГА ОБУЧЕНИЯ: `Data\Tutorial.bin` → таблица порта `DT_Tutorial` + EN-оверлей имён.
#
# Разбор системы целиком — `Docs/Port/systems/TUTORIAL.md` (§3). Загрузчик эталона —
#   `TutorialTable_Load@0xB78B10` (`./Data/Tutorial.bin`, заголовок 20 б, записи по 4672 б = 0x1240),
#   чтение полей — `TutorialUI_RefreshPage@0xB78460`, `entry_CheckRequirements@0xB798B0`,
#   `uiSkillTutorialOnCommand@0xB79980`, `netSendSlotCmd_6184@0xB7AE80`.
#
# ══════════════════ ФОРМАТ ЗАПИСИ (4672 б, реверс загрузчика и читателей, НЕ догадка) ═══════════════════
#   +0     u16  Id            — номер раздела; уходит в 6184 (`*pSlotEntry`), он же индекс бита маски
#   +2     u16  —             — у всех 27 записей 0, читателей нет (сверяется `--verify`)
#   +4     i32  Category      — 0…4, категория меню 69300 (`Tutorial_middle_category.bin`)
#   +8     char[68] Caption   — имя раздела (меню 69300; живой путь его не показывает)
#   +76    i32[3] CondType    — вид условия 0…16 (таблица предикатов `CTutorial_Init@0xB77B90`; >16 — пропуск)
#   +88    i32[3] CondA       — первый аргумент предиката
#   +100   i32[3] CondB       — второй аргумент предиката
#   +112   char[20][64] Title — заголовок страницы → 69204
#   +1392  i32[20] DescId     — id текста страницы (`Tutorial_text.bin`) → 69205
#   +1472  i32[20] —          — читателей в срезе клиента нет (сверяется `--verify`)
#   +1552  i32[20][3] Answer  — id ответа (`Tutorial_text_answer.bin`) → 69206…69208
#   +1792  i32[20][3] Next    — страница по ответу; ≥ 20 = конец раздела (бит + 6184), −1 = ответа нет
#   +2032  char[20][64] Robot — картинка ведущего 256×460 (`<имя>.tex`; строка ≤ 1 символа = нет)
#   +3312  i32[20] RobotType  — 1 → контрол 69201 (слева), 2 → 69203 (справа)
#   +3392  char[20][64] Image — иллюстрация 380×332 → 69202
#
# ИСТОЧНИКИ. Таблица — из РАБОЧЕЙ RU-сборки (`asda_paths.DATA`, как все данные порта): имена и заголовки RU (cp1251).
#   ⚠ Условия появления разделов у рабочей сборки ОТЛИЧАЮТСЯ от эталона Interlude (раскопки — предмет 23516 вместо
#   уровня 10, бой 7 вместо 8 и т. д.; список — TUTORIAL.md §3.1). Берём рабочую — у порта весь `Data` оттуда.
#   EN-имена и EN-заголовки — из эталона Interlude (`asda_paths.CLIENT_ORIG`), оверлей `DT_Loc_Tutorial_en`
#   (ключи `<строка>.Name` и `<строка>.p<страница>`). Строка, которая в RU-сборке испорчена кодировкой (корейский
#   исходник, у недостижимых разделов «уровень 1000»), берётся из эталона как есть.
#
# ПРОВЕРКА. `--verify` собирает из разобранных строк байты ОБРАТНО поверх копии исходника и сверяет md5: поля,
#   которых нет в таблице (+2, +1472), обязаны совпасть сами — иначе смещения разобраны неверно.
#
# Запуск:  Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_import_tutorial.py
#   без UE: python asda_import_tutorial.py --json-only
#   сверка: python asda_import_tutorial.py --verify
import os as _os
import struct
import sys as _sys

_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_dt_common as _DT

SRC_BIN = _os.path.join(_P.DATA, "Tutorial.bin")              # рабочая RU-сборка
EN_BIN = _os.path.join(_P.CLIENT_ORIG, "Data", "Tutorial.bin")  # эталон Interlude (EN-имена)

OUT_DIR = _os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Data", "Tutorial")
OUT_JSON = _os.path.join(OUT_DIR, "Tutorial.json")
OUT_EN_CSV = _os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Text", "L10N", "en", "DT_Loc_Tutorial_en.csv")

PKG = "/Game/AsdaAssets/Data"
DT_NAME = "DT_Tutorial"
ROW_STRUCT = "/Script/AsdaReborn.AsdaTutorialRow"
LOC_PKG = "/Game/AsdaAssets/Text/L10N/en"
LOC_NAME = "DT_Loc_Tutorial_en"
LOC_STRUCT = "/Script/AsdaReborn.AsdaLocRow"

HDR = 20
REC = 4672
PAGES = 20


def _read(path):
    with open(path, "rb") as f:
        return f.read()


def _cstr(b):
    return b.split(b"\0", 1)[0]


def _dec_ru(b):
    """Строка рабочей сборки: cp1251. Испорченная (корейский исходник → «?») распознаётся вызывающим."""
    return _cstr(b).decode("cp1251", "replace")


def _dec_orig(b):
    """Строка эталона: ASCII либо корейская cp949 (разделы с условием «уровень 1000»)."""
    s = _cstr(b)
    try:
        return s.decode("ascii")
    except UnicodeDecodeError:
        return s.decode("cp949", "replace")


def _broken(ru, orig):
    """RU-строка, которую рабочая сборка получила из корейской подстановкой «?» (у раздела нет перевода):
    в RU есть «?», а эталонная строка не ASCII (корейская)."""
    return "?" in ru and any(ord(c) > 127 for c in orig)


def parse(raw, raw_en):
    if raw is None or len(raw) < HDR:
        return None
    n = struct.unpack_from("<i", raw, 4)[0]
    if n <= 0 or HDR + REC * n != len(raw):
        return None
    if raw_en is not None and len(raw_en) != len(raw):
        raw_en = None
    rows = []
    for i in range(n):
        r = raw[HDR + REC * i:HDR + REC * (i + 1)]
        e = raw_en[HDR + REC * i:HDR + REC * (i + 1)] if raw_en else None
        name_ru = _dec_ru(r[8:76])
        name_en = _dec_orig(e[8:76]) if e else ""
        pages = []
        for p in range(PAGES):
            t_ru = _dec_ru(r[112 + 64 * p:176 + 64 * p])
            t_en = _dec_orig(e[112 + 64 * p:176 + 64 * p]) if e else ""
            pages.append({
                "Title": t_en if (_broken(t_ru, t_en) and t_en) else t_ru,
                "TitleEn": t_en,
                "DescId": struct.unpack_from("<i", r, 1392 + 4 * p)[0],
                "AnswerId": list(struct.unpack_from("<3i", r, 1552 + 12 * p)),
                "NextPage": list(struct.unpack_from("<3i", r, 1792 + 12 * p)),
                "RobotImage": _cstr(r[2032 + 64 * p:2096 + 64 * p]).decode("latin1"),
                "RobotImageType": struct.unpack_from("<i", r, 3312 + 4 * p)[0],
                "Image": _cstr(r[3392 + 64 * p:3456 + 64 * p]).decode("latin1"),
            })
        rows.append({
            "Index": i,
            "Id": struct.unpack_from("<H", r, 0)[0],
            "Category": struct.unpack_from("<i", r, 4)[0],
            "Caption": name_en if (_broken(name_ru, name_en) and name_en) else name_ru,
            "NameEn": name_en,
            "CondType": list(struct.unpack_from("<3i", r, 76)),
            "CondA": list(struct.unpack_from("<3i", r, 88)),
            "CondB": list(struct.unpack_from("<3i", r, 100)),
            "Pages": pages,
        })
    return rows


def rebuild(raw, rows):
    """Записать разобранные поля обратно поверх копии исходника (строки — в исходных байтах рабочей сборки)."""
    out = bytearray(raw)
    for row in rows:
        o = HDR + REC * row["Index"]
        struct.pack_into("<H", out, o, row["Id"])
        struct.pack_into("<i", out, o + 4, row["Category"])
        struct.pack_into("<3i", out, o + 76, *row["CondType"])
        struct.pack_into("<3i", out, o + 88, *row["CondA"])
        struct.pack_into("<3i", out, o + 100, *row["CondB"])
        for p, pg in enumerate(row["Pages"]):
            struct.pack_into("<i", out, o + 1392 + 4 * p, pg["DescId"])
            struct.pack_into("<3i", out, o + 1552 + 12 * p, *pg["AnswerId"])
            struct.pack_into("<3i", out, o + 1792 + 12 * p, *pg["NextPage"])
            struct.pack_into("<i", out, o + 3312 + 4 * p, pg["RobotImageType"])
            for off, key in ((2032, "RobotImage"), (3392, "Image")):
                b = pg[key].encode("latin1")
                tail = raw[o + off + 64 * p + len(b):o + off + 64 * (p + 1)]
                out[o + off + 64 * p:o + off + 64 * (p + 1)] = (b + tail)[:64]
    return bytes(out)


def verify(log):
    import hashlib
    raw = _read(SRC_BIN)
    rows = parse(raw, None)
    if rows is None:
        log("!! %s не бьётся с заголовком" % SRC_BIN)
        return False
    back = rebuild(raw, rows)
    same = hashlib.md5(back).hexdigest() == hashlib.md5(raw).hexdigest()
    # Поля без читателей обязаны быть в исходнике такими, как их описывает шапка.
    zero2 = all(struct.unpack_from("<H", raw, HDR + REC * i + 2)[0] == 0 for i in range(len(rows)))
    log("строк %d, обратная сборка %s, +2 у всех 0: %s" % (len(rows), "СОВПАЛА" if same else "РАСХОДИТСЯ",
                                                          "да" if zero2 else "НЕТ"))
    return same and zero2


def en_csv(rows):
    """Оверлей EN: имя раздела и заголовок каждой непустой страницы из эталона (`FAsdaLocRow`: Name,Text)."""
    import csv
    import io
    buf = io.StringIO()
    w = csv.writer(buf, lineterminator="\n")
    w.writerow(["Name", "Text"])
    for r in rows:
        if r["NameEn"]:
            w.writerow(["%d.Name" % r["Index"], r["NameEn"]])
        for p, pg in enumerate(r["Pages"]):
            if pg["TitleEn"] and pg["TitleEn"] != "_":
                w.writerow(["%d.p%d" % (r["Index"], p), pg["TitleEn"]])
    return buf.getvalue()


def main(unreal=None):
    def log(m):
        if unreal:
            unreal.log_warning("[книга обучения] %s" % m)
        else:
            print("[книга обучения] %s" % m)

    raw_en = _read(EN_BIN) if _os.path.isfile(EN_BIN) else None
    rows = parse(_read(SRC_BIN), raw_en)
    if rows is None:
        log("!! исходник не разобран: %s" % SRC_BIN)
        return False
    log("разделов %d (EN-эталон %s)" % (len(rows), "есть" if raw_en else "НЕТ"))
    # В таблицу уходят поля строки FAsdaTutorialRow; EN-копии — только в оверлей.
    table = []
    for r in rows:
        t = {k: v for k, v in r.items() if k not in ("NameEn",)}
        t["Pages"] = [{k: v for k, v in pg.items() if k != "TitleEn"} for pg in r["Pages"]]
        table.append(t)
    text = _DT.json_rows(table, lambda r: str(r["Index"]))
    loc = en_csv(rows)
    _os.makedirs(OUT_DIR, exist_ok=True)
    with open(OUT_JSON, "w", encoding="utf-8", newline="") as f:
        f.write(text)
    with open(OUT_EN_CSV, "w", encoding="utf-8-sig", newline="") as f:
        f.write(loc)
    log("JSON: %s; EN-оверлей: %s" % (OUT_JSON, OUT_EN_CSV))
    if unreal is None:
        return True
    a = _DT.fill_dt(unreal, PKG, DT_NAME, ROW_STRUCT, text, log, fmt="json",
                    missing_hint="Public/UI/AsdaTutorialSubsystem.h, FAsdaTutorialRow")
    b = _DT.fill_dt(unreal, LOC_PKG, LOC_NAME, LOC_STRUCT, loc, log, fmt="csv",
                    missing_hint="Public/Text/AsdaLang.h, FAsdaLocRow")
    for dt in (a, b):
        if dt is not None:
            log("ASDA_WRITES_ASSET: %s" % _DT.dt_package_path(dt))
    ok = a is not None and b is not None
    log("ЗАВЕРШЕНО: %s" % ("таблицы записаны" if ok else "ОШИБКА"))
    return ok


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__":
    if "--verify" in _sys.argv:
        _sys.exit(0 if verify(lambda m: print("[книга обучения] %s" % m)) else 1)
    elif "--json-only" in _sys.argv:
        main(None)
    else:
        import unreal as _u
        main(_u)
