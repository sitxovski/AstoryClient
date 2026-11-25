# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_soulmate_roulette_dt.py — DT_Soulmate_Roulette ← Data/Soulmate_Roulette.bin.
#
# ЧТО ЭТО. КЛИЕНТСКАЯ таблица призов рулетки соулмейта (окно 67000 `GOLD_roullete_WIN`, опкоды
#   6575 «крутить» / 6576 «результат» / 6579 «забрать» / 6580 «выдача»). Каждая строка описывает
#   ОДНО положение колеса: четыре сектора (ВВЕРХ · ВПРАВО · ВНИЗ · ВЛЕВО) с типом приза, значением,
#   количеством, признаком включённости и звуком.
#
# ★★★ ТАБЛИЦА ОТВЕЧАЕТ ТОЛЬКО ЗА ОТОБРАЖЕНИЕ. Розыгрыш и НАЧИСЛЕНИЕ приза делает СЕРВЕР по своей
#   таблице (`dword_5F5F60`, 40 б/строка). Веса здесь — лишь зеркало серверных, клиент их НЕ
#   разыгрывает; держим их ради редактора данных и сверки с сервером (закон «авторитет = сервер»).
#   ⚠ ДЕФЕКТ ОРИГИНАЛА D6: текст приза эталон строит ИЗ ЭТОЙ таблицы по ЛОКАЛЬНОМУ углу колеса, а
#   выдаёт сервер ⇒ при расхождении двух таблиц игрок видит одно, а получает другое. Порт
#   воспроизводит это 1:1; ключ `asda.Soul.Roulette.TextFromServer 1` строит текст по пакету 6580.
#   ⇒ ПРАВИТЬ ЗНАЧЕНИЯ ЗДЕСЬ НЕЛЬЗЯ: расхождение с сервером сразу проявится как D6.
#
# ИСТОЧНИК (вещественное доказательство, скопирован из дистрибутива Donors\AsdaChroniclesInterlude\Data):
#   RawAssets/Data/Soulmate_Roulette.bin — 3420 б, сигнатура «SRL» по +0x0C, хедер 20 б, запись 340 б.
#
# ═══════ РАСКЛАДКА, ДОКАЗАННАЯ ПО САМОМУ ФАЙЛУ (не по отчёту) ═══════
#   Хедер 20 б:  +0x00 i32 0 (версия) · +0x04 i32 count = 10 · +0x08 i32 25 (кодом не читается) ·
#                +0x0C "SRL\0" · +0x10 i32 0
#   Запись 340 б (s = сектор 0..3; 4 + 5×16 + 4×64 = 340 — БЕЗ ОСТАТКА, неизвестных байт НЕТ):
#     +0x00        i32      id строки (0..9; он же ключ строки DT и значение «следующая строка» 6576)
#     +0x04 + 4·s  i32      ВЕС сектора        ← сумма четвёрки = 100000 (строки 0..4), 0 у пустых 5..9
#     +0x14 + 4·s  i32      тип приза          0 пусто · 1 опыт · 2 предмет · 3 ОЗ/ОМ · −1 нет сектора
#     +0x24 + 4·s  i32      значение           опыт (в тексте /10) · itemId · процент ОЗ/ОМ
#     +0x34 + 4·s  i32      количество предметов
#     +0x44 + 4·s  i32      включённость       ≤ 0 ⇒ плашка сектора СКРЫТА
#     +0x54 + 64·s char[64] базовое имя wav    (эталон дописывает «.wav»)
#   ДОКАЗАТЕЛЬСТВА (печатаются при каждом прогоне, см. `prove()`):
#     • 20 + 10×340 = 3420 = размер файла — шаг записи сходится ТОЧНО;
#     • строковые поля лежат на 104, 444, 784, … — шаг 340, первое = 20 + 84;
#     • сумма четвёрки по +0x04 равна РОВНО 100000 у строк 0…4;
#     • после нуля-терминатора в 40 строковых полях нет НИ ОДНОГО ненулевого байта.
#   ★ ПОПРАВКА К ОТЧЁТУ `Reports/soulmate_skills_recall_roulette.md` §C.1: он считал +4..+19
#     частью «id/заголовка». На данных это ЧЕТЫРЕ ВЕСА (35000/10000/20000/35000 у строки 0).
#   ★ Строки 5…9 в живом файле ПУСТЫЕ (веса 0, всё −1, звук — ЛИТЕРАЛЬНЫЙ текст «-1»). Это данные
#     оригинала, не мусор разбора: переносим 1:1, резать нельзя (закон `no-scope-cutting`).
#
# RowStruct = FAsdaRouletteRow (Source/AsdaReborn/Public/UI/AsdaSoulRouletteSubsystem.h)
#   — ТРЕБУЕТ сборки C++ до запуска скрипта. Колонки берутся РОВНО из неё, своих не выдумываем.
# Выход: RawAssets/Data/SoulmateRoulette.csv (ВНЕ Content!) + /Game/AsdaAssets/Data/DT_Soulmate_Roulette
#   (путь ассета задан в коде: `UAsdaSoulRouletteSubsystem::kPrizeTablePath`).
# Запуск (в редакторе):  python Tools/run_headless_safe.py Tools/asda_build_soulmate_roulette_dt.py --idle 200
# Запуск (только CSV):   python Tools/asda_build_soulmate_roulette_dt.py
# ★ ПРАВИЛО tools-no-import-side-effects: НИКАКИХ действий при импорте модуля — только через main().
import os
import struct

# Скрипт обязан импортироваться и БЕЗ редактора: тогда он делает только CSV (шаг «CSV → DataTable»
#   пропускается). Так CSV можно перегенерировать в любой момент, не открывая UE.
try:
    import unreal
except ImportError:      # запуск обычным python — это штатный режим «только CSV»
    unreal = None

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "RawAssets", "Data", "Soulmate_Roulette.bin")
CSV = os.path.join(ROOT, "RawAssets", "Data", "SoulmateRoulette.csv")
OUT_PKG = "/Game/AsdaAssets/Data"
OUT_NAME = "DT_Soulmate_Roulette"                       # = kPrizeTablePath в C++
ROW_STRUCT = "/Script/AsdaReborn.AsdaRouletteRow"       # = FAsdaRouletteRow
HDR = 20          # шапка 20 б
REC = 340         # запись 340 б: 4 + 5×16 + 4×64
SIG = b"SRL"      # магия по смещению +0x0C
SECTORS = 4       # ВВЕРХ · ВПРАВО · ВНИЗ · ВЛЕВО (порядок из `CGoldRoulette_InitUI@0xE615E0`)
STR_LEN = 64      # длина строкового поля звука
WEIGHT_SUM = 100000   # сумма весов четвёрки у НЕпустой строки


def log(m):
    if unreal:
        unreal.log_warning("[roulette-dt] %s" % m)
        return
    # Консоль Windows бывает в cp1251 и роняется на «★» — печатаем с заменой непечатного.
    line = "[roulette-dt] %s" % m
    enc = (getattr(__import__("sys").stdout, "encoding", None) or "utf-8")
    print(line.encode(enc, errors="replace").decode(enc, errors="replace"))


def parse(path):
    """Разбор `Soulmate_Roulette.bin` → список словарей в ПОРЯДКЕ ФАЙЛА (порядок = id 0..9)."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < HDR:
        log("файл короче шапки (%d б) — разбирать нечего" % len(data))
        return [], data
    sig = data[12:15]
    if sig != SIG:
        log("СИГНАТУРА НЕ %r, а %r — файл не тот (или зашифрован: см. ENCRYPTED_BUILDS_CIPHER.md)"
            % (SIG, sig))
        return [], data
    count_hdr = struct.unpack_from("<I", data, 4)[0]
    count_file = (len(data) - HDR) // REC
    if count_hdr != count_file:
        log("хедер обещает %d записей, по размеру файла их %d — беру меньшее" % (count_hdr, count_file))
    count = min(count_hdr, count_file)
    tail = (len(data) - HDR) % REC
    if tail:
        log("!! хвост %d б не кратен записи %d б — раскладка под подозрением" % (tail, REC))

    rows = []
    for i in range(count):
        o = HDR + REC * i
        # 21 i32 = id + 5 четвёрок полей; дальше 4 строки по 64 б — запись покрыта ЦЕЛИКОМ.
        v = struct.unpack_from("<21i", data, o)
        sounds = []
        for s in range(SECTORS):
            raw = data[o + 84 + STR_LEN * s: o + 84 + STR_LEN * (s + 1)]
            sounds.append(raw.split(b"\x00")[0].decode("cp1251", errors="replace"))
        rows.append({
            "id": v[0],
            "weights": list(v[1:5]),
            "types": list(v[5:9]),
            "values": list(v[9:13]),
            "counts": list(v[13:17]),
            "enabled": list(v[17:21]),
            "sounds": sounds,
        })
    return rows, data


def prove(rows, data):
    """Доказательства раскладки — печатаются КАЖДЫЙ прогон, чтобы порча файла всплывала сразу."""
    ok = True
    log("размер файла %d б; %d + %d×%d = %d %s"
        % (len(data), HDR, len(rows), REC, HDR + len(rows) * REC,
           "СХОДИТСЯ" if HDR + len(rows) * REC == len(data) else "!! НЕ СХОДИТСЯ"))
    if HDR + len(rows) * REC != len(data):
        ok = False

    # (1) id идут подряд 0..N-1 — значит +0 действительно id, а не первый вес.
    ids = [r["id"] for r in rows]
    if ids != list(range(len(rows))):
        log("!! id не образуют 0..%d: %s" % (len(rows) - 1, ids))
        ok = False
    else:
        log("id записей: 0..%d подряд ⇒ поле +0x00 = id (а не вес)" % (len(rows) - 1))

    # (2) Сумма весов НЕпустой строки = 100000 — главный признак верного смещения весов.
    for r in rows:
        tot = sum(r["weights"])
        empty = (tot == 0 and set(r["types"]) == {-1})
        mark = "ПУСТАЯ (все поля −1)" if empty else ("сумма=%d %s" % (
            tot, "OK" if tot == WEIGHT_SUM else "!! НЕ 100000"))
        if not empty and tot != WEIGHT_SUM:
            ok = False
        log("  id %d: веса %s — %s" % (r["id"], r["weights"], mark))

    # (3) В строковых полях после нуля-терминатора не должно быть НИЧЕГО (иначе там прячется поле).
    dirty = 0
    for i in range(len(rows)):
        o = HDR + REC * i
        for s in range(SECTORS):
            f = data[o + 84 + STR_LEN * s: o + 84 + STR_LEN * (s + 1)]
            z = f.find(b"\x00")
            if z >= 0 and f[z:].strip(b"\x00"):
                dirty += 1
                log("!! id %d сектор %d: мусор после терминатора %s" % (i, s, f[z:].hex()))
    if dirty:
        ok = False
    else:
        log("строковые поля (%d шт.): после терминатора ни одного ненулевого байта ⇒ "
            "неизвестных байт в записи НЕТ, сырой hex-остаток не нужен" % (len(rows) * SECTORS))
    return ok


def _arr_i(vals):
    """TArray<int32> в CSV DataTable: «(1,2,3,4)» — поле целиком в кавычках."""
    return '"(%s)"' % ",".join(str(v) for v in vals)


def _arr_s(vals):
    """TArray<FString> в CSV DataTable: «("a","b")»; внутренние кавычки удваиваются по RFC 4180."""
    return '"(%s)"' % ",".join('""%s""' % v.replace('"', "'") for v in vals)


def build_csv(rows):
    """CSV для `UDataTable`. RowStruct = FAsdaRouletteRow; ключ строки = id записи (так в C++)."""
    lines = ["_row,RowId,Weights,Types,Values,Counts,Enabled,Sounds"]
    for r in rows:
        lines.append(",".join([
            str(r["id"]), str(r["id"]),
            _arr_i(r["weights"]), _arr_i(r["types"]), _arr_i(r["values"]),
            _arr_i(r["counts"]), _arr_i(r["enabled"]), _arr_s(r["sounds"]),
        ]))
    return "\n".join(lines) + "\n"


def build_json(rows):
    """Тот же материал в JSON — запасной путь заливки, если CSV не разберёт массивы."""
    import json
    out = []
    for r in rows:
        out.append({
            "Name": str(r["id"]),
            "RowId": r["id"],
            "Weights": r["weights"], "Types": r["types"], "Values": r["values"],
            "Counts": r["counts"], "Enabled": r["enabled"], "Sounds": r["sounds"],
        })
    return json.dumps(out, ensure_ascii=False, indent=0)


def verify_dt(dt, rows):
    """Читаем DataTable ОБРАТНО и сверяем с .bin — заливка обязана быть проверена, а не обещана.

    ⚠ Строку DataTable из python в UE 5.8 «в лоб» не достать: у `DataTableFunctionLibrary`
    метода `get_data_table_row_from_name` НЕТ (снято `dir()` на живом редакторе 31.08.2026).
    Штатный обратный путь — `export_to_json_string()`: он отдаёт ВСЕ строки со всеми полями.
    """
    import json
    try:
        dump = json.loads(dt.export_to_json_string())
    except Exception as e:                           # noqa: BLE001 — печатаем и считаем провалом
        log("!! обратное чтение (export_to_json_string) не удалось: %s" % e)
        return [r["id"] for r in rows]

    # Ключ строки UE кладёт в «Name»; регистр имён полей у экспорта может отличаться — сверяем без него.
    by_key = {}
    for d in dump:
        low = {str(k).lower(): v for k, v in d.items()}
        by_key[str(low.get("name", ""))] = low

    bad = []
    for r in rows:
        d = by_key.get(str(r["id"]))
        if d is None:
            log("!! строки с ключом %d в таблице НЕТ" % r["id"])
            bad.append(r["id"])
            continue
        got = (d.get("rowid"), [int(x) for x in d.get("weights", [])],
               [int(x) for x in d.get("types", [])], [int(x) for x in d.get("values", [])],
               [int(x) for x in d.get("counts", [])], [int(x) for x in d.get("enabled", [])],
               [str(x) for x in d.get("sounds", [])])
        want = (r["id"], r["weights"], r["types"], r["values"], r["counts"], r["enabled"], r["sounds"])
        if got != want:
            log("!! строка %d разошлась:\n    в DT   %s\n    в .bin %s" % (r["id"], got, want))
            bad.append(r["id"])
    return bad


def main():
    rows, data = parse(BIN)
    if not rows:
        log("записей нет — CSV и DT не тронуты")
        print("ЗАВЕРШЕНО")
        return
    prove(rows, data)

    # ── Сводка по данным (глазами сверяется с шапкой AsdaSoulRouletteSubsystem.h) ──
    kind = {0: "пусто", 1: "опыт", 2: "предмет", 3: "ОЗ/ОМ", -1: "нет"}
    for r in rows:
        log("  id %d: %s" % (r["id"], " | ".join(
            "%s %s=%d n=%d вкл=%d %s" % (["ВВЕРХ", "ВПРАВО", "ВНИЗ", "ВЛЕВО"][s],
                                         kind.get(r["types"][s], "?%d" % r["types"][s]),
                                         r["values"][s], r["counts"][s], r["enabled"][s],
                                         r["sounds"][s])
            for s in range(SECTORS))))
    live = [r["id"] for r in rows if sum(r["weights"])]
    log("★ рабочих строк (веса ненулевые): %d из %d — id %s; остальные ПУСТЫЕ в самом оригинале"
        % (len(live), len(rows), live))
    log("★ веса — ЗЕРКАЛО серверной таблицы; клиент их не разыгрывает (дефект D6: показ ≠ выдача)")

    text = build_csv(rows)
    with open(CSV, "w", encoding="utf-8-sig", newline="\n") as f:
        f.write(text)
    log("CSV записан: %s (%d строк)" % (CSV, len(rows)))

    if not unreal:
        log("редактора нет — шаг «CSV → DataTable» пропущен (это штатный режим «только CSV»)")
        print("ЗАВЕРШЕНО")
        return

    # Общий шаг «текст → DataTable» уже есть в проекте — используем его (закон №0), не дублируем.
    # Путь к Tools/ добавляем ЗДЕСЬ, а не при импорте модуля (правило tools-no-import-side-effects):
    # headless-редактор запускает скрипт по абсолютному пути и своей папки в sys.path не держит.
    import sys
    _tools = os.path.dirname(os.path.abspath(__file__))
    if _tools not in sys.path:
        sys.path.insert(0, _tools)
    import asda_dt_common
    dt = asda_dt_common.fill_dt(
        unreal, OUT_PKG, OUT_NAME, ROW_STRUCT, text, log, fmt="csv",
        missing_hint="Public/UI/AsdaSoulRouletteSubsystem.h ещё не скомпилирован")
    if not dt:
        print("ЗАВЕРШЕНО")
        return

    bad = verify_dt(dt, rows)
    if bad:
        # CSV-разбор массивов подвёл — тот же материал заливаем JSON'ом (вложенность у него родная,
        # см. заметку в Tools/asda_import_nsr_dialog.py) и проверяем ЗАНОВО.
        log("★ CSV не сошёлся по строкам %s — перезаливаю JSON'ом (массивы у него родные)" % bad)
        # ★ ИСПРАВЛЕНИЕ дедуп №2 В2 Т4: перезаливка JSON — тоже через общий шаг (провал → не сохранять, маркер).
        if not asda_dt_common.fill_and_save(unreal, dt, build_json(rows), log, fmt="json"):
            log("!! перезаливка JSON не прошла — таблица НЕ сохранена")
        bad = verify_dt(dt, rows)

    log("%s: строк=%d (в .bin %d), расхождений=%d"
        % (OUT_NAME, len(dt.get_row_names()), len(rows), len(bad)))
    if bad or len(dt.get_row_names()) != len(rows):
        log("!! ТАБЛИЦА НЕ СОШЛАСЬ С ФАЙЛОМ — разбираться, не принимать")
    else:
        log("★ сверено ОБРАТНЫМ чтением: все %d строк совпали с .bin побайтово-по-полям" % len(rows))
    print("ЗАВЕРШЕНО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    main()
