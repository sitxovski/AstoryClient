# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_constellation_match_dt.py — DT_ConstellationMatch ← Data/Constellation_Match.BIN.
#
# ЧТО ЭТО. Таблица «ВЛЕЧЕНИЕ (attraction) между знаками зодиака»: по МОЕМУ знаку она даёт знак
#   «максимального» и знак «сильного» влечения; все прочие знаки — «обычное». Из неё считается
#   индекс 0/1/2, который окно соулмейта и окно чужого персонажа превращают в нотис 110632/110633/
#   110634 и РОВНО ОДНУ из трёх картинок.
#
# ИСТОЧНИК (вещественное доказательство, скопирован из дистрибутива Donors\AsdaChroniclesInterlude\Data):
#   RawAssets/Data/Constellation_Match.BIN — 320 б, сигнатура "CTM", хедер 20 б, запись 5 б.
#
# РАСКЛАДКА (снята с бинаря, отчёт `Reports/constellation_full.md` §3.3; загрузчик
#   `ConstellationMatchTable_Load@0xF1A3D0`, `AsdaStory_game.c:538111`):
#     +0x00 i32 0
#     +0x04 i32 entryCount = 60          ← читается кодом (`pMgr[172]`)
#     +0x08 i32 5                        ← размер записи; кодом НЕ читается
#     +0x0C "CTM\0"
#     +0x10 i32 0
#     +0x14 60 записей по 5 байт:
#           [0] u8 МОЙ знак (1…12)
#           [1] u8 знак «Максимальное влечение» → индекс 0, нотис 110632
#           [2] u8 знак «Сильное влечение»      → индекс 1, нотис 110633
#           [3] u8 МЕСЯЦ
#           [4] u8 ДЕНЬ
#   60 записей = 5 недельных блоков по 12 знаков: 10/07, 10/14, 10/21, 10/28, 11/04 (шаг ровно
#   7 дней) — таблица задумана как еженедельный «гороскоп», обновляемый патчем.
#
# ★★★ ДЕФЕКТ ОРИГИНАЛА D5. Поиск `findRecord5ByFirstByte@0xF1A770` (`:538189`) берёт ПЕРВУЮ запись
#   с нужным байтом[0] и поля месяца/дня НЕ читает ни здесь, ни где-либо ещё. Значит игра всегда
#   считает влечение по блоку 10/07, а 48 записей остальных недель — мёртвый груз. В порту это
#   поведение оставлено ПО УМОЛЧАНИЮ, а учёт даты включается CVar `asda.Constellation.UseDate 1`
#   (см. `UAsdaConstellationSubsystem::MatchIndex`). Поэтому в таблицу кладутся ВСЕ 60 записей —
#   резать данные нельзя (закон `no-scope-cutting`).
#
# ★ ЗАПИСИ НЕ ПРАВИМ. В блоках 10/14…11/04 есть самосовпадения (Телец→Телец, Рак→Рак,
#   Скорпион→Скорпион, Рыбы→Рыбы, Весы→Весы) — это дефект ДАННЫХ оригинала (D9), но данные здесь
#   вещественное доказательство: переносим 1:1 и лишь помечаем колонкой `SelfMatch`.
#
# RowStruct = FAsdaConstellationMatchRow (Source/AsdaReborn/Public/UI/AsdaConstellationSubsystem.h)
#   — ТРЕБУЕТ сборки C++ до запуска скрипта.
# Выход: RawAssets/Data/ConstellationMatch.csv (ВНЕ Content!) + /Game/AsdaAssets/Data/DT_ConstellationMatch
# Запуск (в редакторе):  python Tools/run_headless_safe.py Tools/asda_build_constellation_match_dt.py --idle 200
# Запуск (только CSV):   python Tools/asda_build_constellation_match_dt.py
# ★ ПРАВИЛО tools-no-import-side-effects: НИКАКИХ действий при импорте модуля — только через main().
import os
import os as _os_dtc  # noqa: E402  ★ дедуп №2 В2 Т4: общий шаг «заливка → сохранение» DataTable
import sys as _sys_dtc  # noqa: E402
_sys_dtc.path.insert(0, _os_dtc.path.dirname(_os_dtc.path.abspath(__file__)))
import asda_dt_common as _DTC  # noqa: E402
import struct

# Скрипт обязан импортироваться и БЕЗ редактора: тогда он делает только CSV (шаг «CSV → DataTable»
#   пропускается). Так CSV можно перегенерировать в любой момент, не открывая UE.
try:
    import unreal
except ImportError:      # запуск обычным python — это штатный режим «только CSV»
    unreal = None

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "RawAssets", "Data", "Constellation_Match.BIN")
# Имена знаков — ИЗ ТАБЛИЦЫ (Constellation.BIN → RawAssets/Text/World/Constellation.csv),
#   в CSV они идут только как комментарий-колонка для чтения глазами (закон «данные из таблиц»).
SIGNS_CSV = os.path.join(ROOT, "RawAssets", "Text", "World", "Constellation.csv")
CSV = os.path.join(ROOT, "RawAssets", "Data", "ConstellationMatch.csv")
OUT_PKG = "/Game/AsdaAssets/Data"
OUT_NAME = "DT_ConstellationMatch"
ROW_STRUCT = "/Script/AsdaReborn.AsdaConstellationMatchRow"
HDR = 20          # шапка 20 б (`fread(header, 0x14u, 1u, Stream)`)
REC = 5           # запись 5 б (`fread(array, 5u, count, Stream)`)
SIG = b"CTM"      # магия по смещению +0x0C


def log(m):
    if unreal:
        unreal.log_warning("[constellation-match-dt] %s" % m)
        return
    # Консоль Windows бывает в cp1251 и роняется на «★» — печатаем с заменой непечатного.
    line = "[constellation-match-dt] %s" % m
    enc = (getattr(__import__("sys").stdout, "encoding", None) or "utf-8")
    print(line.encode(enc, errors="replace").decode(enc, errors="replace"))


def sign_names():
    """Имена знаков 1…12 из RawAssets/Text/World/Constellation.csv (ключ = колонка `Name`)."""
    import csv
    out = {}
    try:
        with open(SIGNS_CSV, "r", encoding="utf-8-sig", newline="") as f:
            for row in csv.reader(f):
                if len(row) >= 3 and row[0].strip().isdigit():
                    out[int(row[0])] = row[2]
    except OSError as e:
        log("не прочитать %s: %s — колонки с именами останутся пустыми" % (SIGNS_CSV, e))
    return out


def parse(path):
    """Разбор `Constellation_Match.BIN` → список словарей в ПОРЯДКЕ ФАЙЛА (порядок значим: D5)."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < HDR:
        log("файл короче шапки (%d б) — разбирать нечего" % len(data))
        return []
    sig = data[12:15]
    if sig != SIG:
        log("СИГНАТУРА НЕ %r, а %r — файл не тот (или зашифрован: см. ENCRYPTED_BUILDS_CIPHER.md)"
            % (SIG, sig))
        return []
    count_hdr = struct.unpack_from("<I", data, 4)[0]
    count_file = (len(data) - HDR) // REC
    if count_hdr != count_file:
        log("хедер обещает %d записей, по размеру файла их %d — беру меньшее" % (count_hdr, count_file))
    count = min(count_hdr, count_file)
    if (len(data) - HDR) % REC:
        log("хвост %d б не кратен записи %d б" % ((len(data) - HDR) % REC, REC))

    rows = []
    for i in range(count):
        o = HDR + REC * i
        rows.append({
            "order": i,                 # ПОРЯДОК В ФАЙЛЕ — по нему работает 1:1-поиск (первая запись)
            "my": data[o + 0],
            "best": data[o + 1],
            "strong": data[o + 2],
            "month": data[o + 3],
            "day": data[o + 4],
        })
    return rows


def build_csv(rows, names):
    """CSV для `UDataTable` (RowStruct = FAsdaConstellationMatchRow). Ключ строки = порядок в файле."""
    lines = ["_row,Order,MySign,BestSign,StrongSign,Month,Day,SelfMatch,MyName,BestName,StrongName"]
    for r in rows:
        # ★ `SelfMatch` — НАША служебная пометка (в файле её нет): запись, где знак совпадает сам
        #   с собой. Дефект ДАННЫХ оригинала D9, НЕ чиним — только помечаем.
        self_match = (r["my"] == r["best"]) or (r["my"] == r["strong"])
        lines.append('%d,%d,%d,%d,%d,%d,%d,%s,"%s","%s","%s"' % (
            r["order"], r["order"], r["my"], r["best"], r["strong"], r["month"], r["day"],
            "True" if self_match else "False",
            names.get(r["my"], ""), names.get(r["best"], ""), names.get(r["strong"], "")))
    return "\n".join(lines) + "\n"


def main():
    rows = parse(BIN)
    if not rows:
        log("записей нет — CSV и DT не тронуты")
        print("ЗАВЕРШЕНО")
        return
    names = sign_names()

    # ── Самопроверка данных: печатаем блоки по датам (глазами сверяется с §3.3 отчёта) ──
    blocks = []
    for r in rows:
        key = (r["month"], r["day"])
        if key not in blocks:
            blocks.append(key)
    log("записей %d, недельных блоков %d: %s" % (
        len(rows), len(blocks), ", ".join("%02d/%02d" % b for b in blocks)))
    selfm = [r for r in rows if r["my"] in (r["best"], r["strong"])]
    log("★ самосовпадений (дефект данных D9, НЕ чиним): %d — %s" % (
        len(selfm), ", ".join("%02d/%02d %s" % (r["month"], r["day"], names.get(r["my"], r["my"]))
                              for r in selfm)))
    for b in blocks:
        part = [r for r in rows if (r["month"], r["day"]) == b]
        log("  блок %02d/%02d: %s" % (b[0], b[1], "; ".join(
            "%s→%s/%s" % (names.get(r["my"], r["my"]), names.get(r["best"], r["best"]),
                          names.get(r["strong"], r["strong"])) for r in part)))
        # ★ Блок 10/07 — единственный, который читает ОРИГИНАЛ (дефект D5).
        if b == blocks[0]:
            log("  ↑ ЭТОТ блок и только он работает в оригинале (дефект D5)")

    text = build_csv(rows, names)
    with open(CSV, "w", encoding="utf-8-sig", newline="\n") as f:
        f.write(text)
    log("CSV записан: %s (%d строк)" % (CSV, len(rows)))

    if not unreal:
        log("редактора нет — шаг «CSV → DataTable» пропущен (это штатный режим «только CSV»)")
        print("ЗАВЕРШЕНО")
        return

    rs = unreal.load_object(None, ROW_STRUCT)
    if not rs:
        log("!! RowStruct %s НЕ НАЙДЕН — нужна СБОРКА C++ "
            "(Public/UI/AsdaConstellationSubsystem.h ещё не скомпилирован). DT не создан." % ROW_STRUCT)
        print("ЗАВЕРШЕНО")
        return

    full = "%s/%s" % (OUT_PKG, OUT_NAME)
    dt = _DTC.open_or_create_dt(unreal, OUT_PKG, OUT_NAME, rs)  # дедуп №2 В2 Т4: общий «открыть-или-создать»
    if not dt:
        log("!! DataTable %s не создан" % OUT_NAME)
        print("ЗАВЕРШЕНО")
        return
    # ★ CompileAndSave НЕ сохраняет — сохраняем явно.
    problems = _DTC.fill_and_save(unreal, dt, text, log, fmt="csv")  # ★ ИСПРАВЛЕНИЕ дедуп №2 В2 Т4: провал заливки → DT не сохраняется; маркер ASDA_WRITES_ASSET
    log("%s: строк=%d (в .BIN %d), заполнение=%s" % (OUT_NAME, len(dt.get_row_names()), len(rows), problems))
    print("ЗАВЕРШЕНО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    main()
