# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_charexp_sync.py — ★★★ СИММЕТРИЯ КЛИЕНТСКОЙ И СЕРВЕРНОЙ ТАБЛИЦ ОПЫТА ПЕРСОНАЖА
#
# ═══ ЗАЧЕМ ═══════════════════════════════════════════════════════════════════════════════════
# Порог опыта хранится ДВАЖДЫ:
#   • КЛИЕНТ — `Data/Character/charexp.bin`, 20 б заголовка + 151 запись по 16 б (+0 i32
#     «опыт до следующего», с 68-й строки делённый на 40; +8 i64 накопленный опыт).
#     Эталон: CharExpTable_Load (`RE/etalon/entity_char.c:49849`), чтение sub_EC1850.
#     В порт импортирована как `RawAssets/Data/ItemDB/charexp.csv` → `DT_charexp`
#     (int64 разрезан на CumExp = младшее слово и Zero2 = старшее).
#   • СЕРВЕР — `GameServer/ConfigSet/ExpTable.txt`: "151", далее «уровень<TAB>накопленный
#     опыт» (загрузчик sub_4AC640@0x4AC640). Повышение уровня делает СЕРВЕР по ЭТОЙ таблице.
# Соответствие: клиентская строка i = порог ВХОДА в уровень i+1 = серверная ExpTable[i+1].
# До 60-го уровня таблицы совпадают (±1..2 опыта в нескольких строках), с 61-го расходятся
# сильно (строка 61: клиент 868 349 765, сервер 794 373 900). Потолок был 59, поэтому
# расхождение не проявлялось; после подъёма потолка (P29) полоса опыта на 61+ врала бы:
# сервер поднимает уровень по своей таблице, а клиент рисует процент по чужой.
# Закон `client-server-table-desync`: авторитет — СЕРВЕР, клиент приводится к нему.
#
# ═══ ЧТО ДЕЛАЕТ ══════════════════════════════════════════════════════════════════════════════
#   без ключей    — СРАВНИВАЕТ накопленный опыт построчно и печатает отчёт (ничего не пишет);
#   --apply       — переписывает charexp.csv по серверной таблице: CumExp/Zero2 (int64 по
#                   половинам) и ExpToNext в кодировке оригинала (разность соседних порогов,
#                   с 68-й строки делённая на 40 — так её умножает statGetScaledValueByIndex);
#   --server PATH — другой источник (по умолчанию живой сервер, ★ ТОЛЬКО ЧТЕНИЕ).
# После `--apply` таблицу надо переимпортировать в `DT_charexp`:
#   $env:ASDA_TABLES='charexp'; $env:ASDA_DIFF_COLS='CumExp,Zero2,ExpToNext'
#   Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\_import_one_itemdb_table.py
#
# ⚠ ЗАКОН `tools-no-import-side-effects`: при импорте модуля скрипт НИЧЕГО не делает.
# ⚠ Живой сервер открываем ТОЛЬКО НА ЧТЕНИЕ; серверный файл не правим отсюда никогда.
# ⚠ Сам charexp.bin (RawAssets/Data/Character) не трогается — это оригинал клиента.
import argparse
import io
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_server_txt as _ST  # noqa: E402  ★ дедуп №2 В2 Т7: путь к живому серверу и чтение .txt как у сервера

HERE = os.path.dirname(os.path.abspath(__file__))
CLIENT_CSV = os.path.join(HERE, "..", "RawAssets", "Data", "ItemDB", "charexp.csv")
# ★ дедуп №2 В2 Т7: путь к живому серверу — один на все инструменты (`asda_server_txt.LIVE_V2`, переопределение
#   ASDA_SERVER_V2). Прежняя форма `\\?\UNC\…` больше не нужна: 11.10.2026 все написания открываются одинаково.
SERVER_TXT = _ST.live_configset("ExpTable.txt")

HEADER = "_row,ExpToNext,Zero,CumExp,Zero2"   # ровно как у сгенерированного CSV
ROWS = 151                                     # записей в charexp.bin (0x97)
DIV40_FROM = 68                                # с этой строки ExpToNext хранится делённым на 40


def read_server(path):
    """ExpTable.txt — ★ ИСПРАВЛЕНИЕ дедуп №2 В2 Т7: читается ТАК, КАК ЕГО ЧИТАЕТ СЕРВЕР (sub_4AC640,
    `RE/ida/server_work/GameServer.c:69114-69138`): `fscanf("%d")` — число N, затем N записей потоком слов
    «уровень (`atol` → BYTE) + накопленный опыт (`_atoi64` → QWORD)»; запись №i (i = 1..N) кладётся в ExpTable[i] —
    по ПОЗИЦИИ, а не по числу «уровень» в файле. Возвращает (declared, {i: опыт}).
    Было: разбор ПО СТРОКАМ, `int()` (падал на «1.5»/«12abc», а сервер берёт префикс), ключ — число из файла
    (при пропуске/повторе уровня сервер и скрипт видели бы разные таблицы). На живой таблице 11.10.2026 итог тот же
    (151 запись, уровни 1..151 по порядку, дробных/мусорных слов нет). Расхождения «уровень в файле ≠ позиция»
    и усечённые `atol` слова печатаются — молча ничего не теряется."""
    ts = _ST.TokenStream(path)
    declared = ts.count()
    rows, mism = {}, []
    for i in range(1, declared + 1):
        lvl = _ST.as_u8(ts.i32())
        rows[i] = ts.i64()
        if lvl != (i & 0xFF):
            mism.append((i, lvl))
    if mism:
        print(f"⚠ ExpTable: у {len(mism)} записей число «уровень» не равно позиции (сервер берёт позицию): {mism[:10]}")
    if ts.lossy:
        print(f"⚠ ExpTable: atol сервера усёк слова: {ts.lossy_text()}")
    if ts.short:
        print(f"⚠ ExpTable: слов меньше, чем обещает заголовок {declared} — сервер дочитает нули")
    return declared, rows


def read_client(path):
    """charexp.csv → список (ExpToNext, Zero, CumExp64) по строкам 0..N-1."""
    with io.open(path, encoding="utf-8-sig", newline="") as f:
        head = f.readline().strip()
        if head != HEADER:
            raise SystemExit(f"неожиданный заголовок CSV: {head!r}")
        out = []
        for line in f:
            p = line.strip().split(",")
            if len(p) != 5:
                continue
            lo, hi = int(p[3]), int(p[4])
            out.append((int(p[1]), int(p[2]), ((hi & 0xFFFFFFFF) << 32) | (lo & 0xFFFFFFFF)))
    return out


def i32(v):
    """Младшие 32 бита как знаковое int32 (так их хранит сгенерированный CSV)."""
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def build_rows(srv):
    """Серверная таблица → строки charexp 0..150 в кодировке оригинала."""
    cum = [srv[i + 1] for i in range(ROWS)]
    rows, inexact = [], []
    for i in range(ROWS):
        diff = cum[i] - cum[i - 1] if i > 0 else 0
        if i >= DIV40_FROM:
            if diff % 40:
                inexact.append(i)
            e2n = diff // 40
        else:
            e2n = diff
        rows.append((e2n, 0, cum[i]))
    return rows, inexact


def main():
    ap = argparse.ArgumentParser(description="Сверка/синхронизация charexp (клиент) с ExpTable (сервер)")
    ap.add_argument("--apply", action="store_true", help="переписать charexp.csv по серверу")
    ap.add_argument("--server", default=SERVER_TXT, help="путь к серверному ExpTable.txt")
    ap.add_argument("--csv", default=CLIENT_CSV, help="путь к клиентскому charexp.csv")
    a = ap.parse_args()

    declared, srv = read_server(a.server)
    missing = [i for i in range(1, ROWS + 1) if i not in srv]
    if missing:
        raise SystemExit(f"в серверной таблице нет уровней {missing[:10]} (объявлено {declared})")
    cli = read_client(a.csv)
    if len(cli) != ROWS:
        raise SystemExit(f"в CSV {len(cli)} строк, ожидалось {ROWS}")

    bad = [i for i in range(ROWS) if cli[i][2] != srv[i + 1]]
    big = [i for i in bad if abs(cli[i][2] - srv[i + 1]) > 10]
    print(f"сервер: объявлено {declared}, клиент: {len(cli)} строк")
    print(f"расхождений накопленного опыта: {len(bad)} (из них больше 10 опыта: {len(big)})")
    for i in bad[:12]:
        print(f"  строка {i:3} (вход в ур. {i + 1:3}): клиент {cli[i][2]:>16}  сервер {srv[i + 1]:>16}")
    if len(bad) > 12:
        print(f"  … ещё {len(bad) - 12}")

    if not a.apply:
        print("режим сверки — ничего не записано (--apply для записи)")
        return 0

    rows, inexact = build_rows(srv)
    with io.open(a.csv, "w", encoding="utf-8", newline="") as f:
        f.write(HEADER + "\n")
        for i, (e2n, z, cum) in enumerate(rows):
            f.write(f"{i},{e2n},{z},{i32(cum)},{cum >> 32}\n")
    print(f"записано {len(rows)} строк в {os.path.normpath(a.csv)}")
    if inexact:
        print(f"⚠ ExpToNext с 68-й строки не делится на 40 нацело в {len(inexact)} строках "
              f"(порт это поле не читает; CumExp записан точно): {inexact[:10]}")
    # ★ 27.09.2026 (itemsdatafix, КРИТ-1): правка в одном CSV пропадает при декоде Bin.bbz (так 27.09
    #   откатились 233 ячейки). Сразу переносим результат в ведомость декодера `_value_overlay/charexp.csv`.
    if os.path.normcase(os.path.abspath(a.csv)) == os.path.normcase(os.path.abspath(CLIENT_CSV)):
        sys.path.insert(0, os.path.join(HERE, "..", "..", "AstoryClient", "Tools", "ItemDB"))
        import build_itemdb_poststep_ledgers as _L
        if _L.main(["--table", "charexp", "--write", "--replace"]):
            print("⚠ ведомость charexp НЕ обновлена — разобрать до любого декода/переимпорта")
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
