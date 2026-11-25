# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""
server_item_tail_extend.py — дописывает недостающий ХВОСТ кодов предметов в серверные таблицы
`ItemTable_Info\\item_table.txt` (он же `Item_Table.txt`) и `ItemTable_Info\\SowelCombine.txt`.

ЗАЧЕМ. У живого сервера item_table обрывается на коде 76470 (55 920 строк), у клиента
`item_list` идёт до 79428 (58 878 записей). Из-за этого `sub_532A90` (проверка
`20551 <= code <= dword_5F5EAC`) отвергает 2 958 кодов, и, в частности, 580 коробок
клиента вообще не открываются (отказ 4).

ОТКУДА ДАННЫЕ.
  * `item_table.txt` — строки строятся ИЗ КЛИЕНТА (`item_list.csv`, колонка `DataHex` =
    248-байтовая запись `ItemRecord`). Соответствие «серверная колонка → смещение в клиентской
    записи» снято эмпирически по 55 920 общим кодам и совпадает 1:1 (см. COLMAP ниже).
  * `SowelCombine.txt` — клиентского источника НЕТ (проверено корреляцией всех 17 варьирующихся
    колонок против всех смещений клиентской записи: максимум 92,6 %, точного нет). Поэтому
    берётся НЕЙТРАЛЬНЫЙ образец из собственного хвоста таблицы (157 одинаковых строк с кода
    76314, 148 из них — коробки `itemType 11 / subType 200`).
    ЕДИНСТВЕННОЕ отступление от образца: колонка 50 (смещение +170 = ID титула). Для предметов
    с `col8 == 208` (маркер «предмет-титул») ноль там означает НЕ «нет», а «титул №0»
    (`RecvClient_TitleItemCheck@0x55F090`, `sub_55F650@0x55F650`). Настоящий ID титула не выводится
    ни из одного клиентского поля, поэтому ставится −1 — собственный сентинел бинаря
    («нет титула»: одна ветка пропускает блок, вторая отвечает отказом 12).

ПРОВЕРКИ ПЕРЕД ЗАПИСЬЮ (все — инварианты, при нарушении которых сервер НЕ стартует):
  1. `count(item_table) == count(SowelCombine)`      (GameServer.c:75187 / :75268)
  2. коды в обеих таблицах идут подряд без пропусков (GameServer.c:68134 / :65121)
  3. `col39 <= count(PackageItem_Table)` для subType 200/201, иначе `<= count(GachaItemRateTable)`
                                                      (GameServer.c:75193 / :75200)
  4. отсутствие дублей кодов внутри строки пакета среди предметов с `col23 == 1`
                                                      (GameServer.c:75223-75244, там `exit(1)`)
  5. все значения помещаются в ширину поля серверной записи (u8 / i16 / i32)
  6. байты первых 55 920 строк не меняются

ЗАПУСК:
    python Tools/server_item_tail_extend.py --report            # только анализ, ничего не пишет
    python Tools/server_item_tail_extend.py --build             # собрать в RE/dumps/server_item_tail/out
    python Tools/server_item_tail_extend.py --verify            # прогнать проверки по собранному
    python Tools/server_item_tail_extend.py --write             # бэкап + запись на ОБЕ площадки
    python Tools/server_item_tail_extend.py --check-live        # перечитать с сервера и проверить
"""

import argparse
import csv
import hashlib
import os
import re
import shutil
import sys
import time

# --- пути -------------------------------------------------------------------------------------
# Живой сервер. Скобки в имени шары ОБЯЗАТЕЛЬНЫ (без них Test-Path/os.path дают ложное «нет»).
# ★ 26.10.2026: живой сервер — ТОЛЬКО по SSH (srv_io); шара \\@@ASTORY_SERVER_HOSTNAME@@ запрещена (22.10). Площадка = (имя, папка ОТ v2, файл).
# Две площадки: у Indun_GameServer свой каталог ItemTable_Info и своё написание имени файла.
SITES = [
    ("GameServer", "GameServer/ItemTable_Info", "item_table.txt"),
    ("Indun_GameServer", "Indun_GameServer/ItemTable_Info", "Item_Table.txt"),
]
sys.path.insert(0, os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "AstoryServer", "Patches", "AsdaPatch", "tools")))   # <ROOT>/AstoryServer
import stack_layout   # noqa: E402  ★ 26.10.2026: единый состав стойки (канал 2: Ch2_*) — Reports/channel2/REPORT.md §3.1
# 26.10: + копии канала 2 — тот же файл и регистр, что у поля, свой каталог
import srv_io   # noqa: E402
SITES += [(twin, twin + "/ItemTable_Info", fn) for twin, field in stack_layout.TWINS.items()
          for s, _d, fn in list(SITES) if s == field]
REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))
WORK = os.path.join(REPO, "RE", "dumps", "server_item_tail")
DIR_ORIG = os.path.join(WORK, "live_orig")
DIR_OUT = os.path.join(WORK, "out")
DIR_CHECK = os.path.join(WORK, "written_check")
CLIENT_CSV = os.path.join(REPO, "AstoryClient", "RawAssets", "Data", "ItemDB", "item_list.csv")


FIRST_CODE = 20551          # g_ItemCodeFirst — база индексации обеих таблиц
OLD_LAST = 76470            # последний код живой item_table
NEW_LAST = 79428            # последний код клиентского item_list (g_ItemCodeLast)

# --- раскладка серверных записей --------------------------------------------------------------
# item_table.txt: 42 колонки -> запись 71 б (sub_4A96A0, GameServer.c:68130-68260).
# Ширина поля в памяти нужна только для проверки «значение не переполнит поле».
IT_WIDTH = {
    0: "i32", 1: "u8", 2: "i16", 3: "u8", 4: "u8", 5: "u8", 6: "u8", 7: "u8", 8: "u8",
    9: "i32", 10: "i16", 11: "u8", 12: "u8", 13: "i16", 14: "u8", 15: "u8", 16: "u8",
    17: "u8", 18: "i16", 19: "i16", 20: "u8", 21: "u8", 22: "u8", 23: "u8", 24: "u8",
    25: "i32", 26: "i32", 27: "u8", 28: "u8", 29: "i16", 30: "u8", 31: "i32", 32: "i16",
    33: "u8", 34: "u8", 35: "u8", 36: "u8", 37: "i16", 38: "u8", 39: "i16", 40: "i16", 41: "i32",
}
IT_NCOL = 42
SC_NCOL = 57                # SowelCombine.txt: 57 токенов -> запись 172 б (sub_4A0DB0)

# Соответствие «серверная колонка item_table → поле клиентской записи ItemRecord(248)».
# Получено перебором ВСЕХ смещений/ширин против 55 920 общих кодов (см. отчёт §2).
# Имя char[64] занимает +4..+67, поэтому полезные поля начинаются с +68.
COLMAP = {}

LIMITS = {"u8": (0, 255), "i16": (-32768, 32767), "i32": (-2 ** 31, 2 ** 31 - 1)}

COL8_TITLE_MARKER = 208     # item_table col8 (+12) == 208 -> «предмет-титул»
SC_COL_TITLE = 50           # SowelCombine col50 (+170) -> ID титула
SC_TITLE_NONE = -1          # сентинел «титула нет» (обе ветки бинаря его переваривают)


# --- чтение/запись ----------------------------------------------------------------------------
def _atol(tok):
    """Повторяет atol() из бинаря: берёт ведущее целое, мусор -> 0 (в файлах встречается «(1)»)."""
    m = re.match(rb"\s*[-+]?\d+", tok)
    return int(m.group()) if m else 0


def read_table(path):
    """Возвращает (сырой заголовок, список строк-списков int, сырое тело в байтах)."""
    raw = open(path, "rb").read()
    parts = raw.split(b"\r\n")
    header = parts[0]
    body = raw[len(header) + 2:]
    rows = [[_atol(t) for t in ln.split()] for ln in parts[1:] if ln.strip()]
    return header, rows, body


def md5(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def load_client(path):
    """code -> 248-байтовая запись ItemRecord из колонки DataHex."""
    csv.field_size_limit(10 ** 9)
    out = {}
    with open(path, newline="", encoding="utf-8", errors="replace") as f:
        for row in csv.DictReader(f):
            out[int(row["ItemCode"])] = bytes.fromhex(row["DataHex"])
    return out


def field(rec, kind, off):
    """Читает поле клиентской записи так же, как его прочёл бы сервер из своей колонки."""
    if kind == "u8":
        return rec[off]
    if kind == "u16":
        return int.from_bytes(rec[off:off + 2], "little", signed=False)
    if kind == "i16":
        return int.from_bytes(rec[off:off + 2], "little", signed=True)
    if kind == "i32":
        return int.from_bytes(rec[off:off + 4], "little", signed=True)
    raise ValueError(kind)


# --- сборка -----------------------------------------------------------------------------------
def build_item_row(code, rec):
    """Одна строка item_table.txt из клиентской записи."""
    row = [code]
    for c in range(1, IT_NCOL):
        kind, off = COLMAP[c]
        row.append(field(rec, kind, off))
    return row


def build_sowel_row(code, template, is_title_item):
    """Одна строка SowelCombine.txt: код + нейтральный образец (кроме ID титула)."""
    row = [code] + list(template[1:])
    if is_title_item:
        row[SC_COL_TITLE] = SC_TITLE_NONE
    return row


def fmt(row):
    return "\t".join(str(v) for v in row).encode("ascii") + b"\r\n"


def build(verbose=True):
    os.makedirs(DIR_OUT, exist_ok=True)
    it_hdr, it_rows, it_body = read_table(os.path.join(DIR_ORIG, "item_table.txt"))
    sc_hdr, sc_rows, sc_body = read_table(os.path.join(DIR_ORIG, "SowelCombine.txt"))
    client = load_client(CLIENT_CSV)

    assert it_rows[-1][0] == OLD_LAST, it_rows[-1][0]
    assert sc_rows[-1][0] == OLD_LAST, sc_rows[-1][0]

    tail = list(range(OLD_LAST + 1, NEW_LAST + 1))
    missing = [c for c in tail if c not in client]
    template = sc_rows[-1]                      # нейтральный образец — последняя строка хвоста

    it_new, sc_new, titles = [], [], []
    for code in tail:
        rec = client.get(code)
        if rec is None:                         # клиентских данных нет — пустая нейтральная запись
            it_row = [code] + [0] * (IT_NCOL - 1)
        else:
            it_row = build_item_row(code, rec)
        it_new.append(it_row)
        is_title = it_row[8] == COL8_TITLE_MARKER
        if is_title:
            titles.append(code)
        sc_new.append(build_sowel_row(code, template, is_title))

    n = len(it_rows) + len(it_new)
    out_it = os.path.join(DIR_OUT, "item_table.txt")
    out_sc = os.path.join(DIR_OUT, "SowelCombine.txt")
    with open(out_it, "wb") as f:
        f.write(str(n).encode("ascii") + b"\r\n")
        f.write(it_body)
        for r in it_new:
            f.write(fmt(r))
    with open(out_sc, "wb") as f:
        f.write(str(n).encode("ascii") + b"\r\n")
        f.write(sc_body)
        for r in sc_new:
            f.write(fmt(r))

    if verbose:
        print("собрано: %d строк в обеих таблицах (было %d, добавлено %d)" % (n, len(it_rows), len(it_new)))
        print("кодов без клиентских данных: %d %s" % (len(missing), missing[:20]))
        print("предметов-титулов в хвосте (col50 := -1): %d %s" % (len(titles), titles))
        print("out:", out_it)
        print("out:", out_sc)
    return out_it, out_sc, missing, titles


# --- проверки ---------------------------------------------------------------------------------
def verify(it_path, sc_path, pkg_path, gacha_path, orig_it, orig_sc):
    ok = True

    def chk(cond, msg):
        nonlocal ok
        print(("OK  " if cond else "FAIL ") + msg)
        if not cond:
            ok = False

    it_hdr, it_rows, _ = read_table(it_path)
    sc_hdr, sc_rows, _ = read_table(sc_path)
    _, pk_rows, _ = read_table(pkg_path)
    _, ga_rows, _ = read_table(gacha_path)
    n_pkg, n_gacha = len(pk_rows), len(ga_rows)

    chk(int(it_hdr) == len(it_rows), "item_table: заголовок %s == строк %d" % (it_hdr.decode(), len(it_rows)))
    chk(int(sc_hdr) == len(sc_rows), "SowelCombine: заголовок %s == строк %d" % (sc_hdr.decode(), len(sc_rows)))
    chk(len(it_rows) == len(sc_rows),
        "равенство таблиц (GameServer.c:75187): item_table %d == SowelCombine %d" % (len(it_rows), len(sc_rows)))
    top = FIRST_CODE + len(it_rows) - 1
    chk(top - 20550 == len(sc_rows), "проверка сервера dword_5F5EAC-20550 == count(SowelCombine): %d == %d"
        % (top - 20550, len(sc_rows)))
    chk(it_rows[-1][0] == NEW_LAST, "верхний код item_table = %d (ожидался %d)" % (it_rows[-1][0], NEW_LAST))

    chk(all(len(r) == IT_NCOL for r in it_rows), "item_table: во всех строках ровно %d колонок" % IT_NCOL)
    chk(all(len(r) == SC_NCOL for r in sc_rows), "SowelCombine: во всех строках ровно %d колонок" % SC_NCOL)

    bad = [i for i in range(1, len(it_rows)) if it_rows[i][0] != it_rows[i - 1][0] + 1]
    chk(not bad, "item_table: коды подряд без пропусков (Sync Error, GameServer.c:68134)")
    bad = [i for i in range(1, len(sc_rows)) if sc_rows[i][0] != sc_rows[i - 1][0] + 1]
    chk(not bad, "SowelCombine: коды подряд без пропусков (Sync Error, GameServer.c:65121)")
    chk([r[0] for r in it_rows] == [r[0] for r in sc_rows], "коды двух таблиц совпадают построчно")

    over = [(r[0], c, r[c]) for r in it_rows for c in range(IT_NCOL)
            if not (LIMITS[IT_WIDTH[c]][0] <= r[c] <= LIMITS[IT_WIDTH[c]][1])]
    chk(not over, "item_table: все значения помещаются в ширину поля (нарушений %d) %s" % (len(over), over[:3]))

    v = [(r[0], r[39]) for r in it_rows if r[17] in (200, 201) and r[39] > n_pkg]
    chk(not v, "коробки: col39 <= PackageItem_Table (%d) — нарушений %d %s" % (n_pkg, len(v), v[:3]))
    v = [(r[0], r[39]) for r in it_rows if r[17] not in (200, 201) and r[39] > n_gacha]
    chk(not v, "не-коробки: col39 <= GachaItemRateTable (%d) — нарушений %d %s" % (n_gacha, len(v), v[:3]))

    col23 = {r[0]: r[23] for r in it_rows}
    dup = []
    for row in pk_rows:
        seen = []
        for j in range(5):
            c = row[1 + 2 * j]
            if FIRST_CODE <= c <= top and col23.get(c) == 1:
                if c in seen:
                    dup.append((row[0], c))
                seen.append(c)
    chk(not dup, "PackageItem_Table: нет дублей кодов внутри строки (иначе exit(1)) — нарушений %d %s"
        % (len(dup), dup[:3]))

    idmatch = all(pk_rows[i][0] == i for i in range(len(pk_rows)))
    chk(idmatch, "PackageItem_Table: id == номеру строки (не трогали)")

    # неизменность старой части
    _, old_it, _ = read_table(orig_it)
    _, old_sc, _ = read_table(orig_sc)
    chk(it_rows[:len(old_it)] == old_it, "item_table: первые %d строк не изменились" % len(old_it))
    chk(sc_rows[:len(old_sc)] == old_sc, "SowelCombine: первые %d строк не изменились" % len(old_sc))

    boxes = [r for r in it_rows if r[17] in (200, 201)]
    new_boxes = [r for r in boxes if r[0] > OLD_LAST]
    print("---- коробок на сервере: %d, из них новых: %d; их индексы пакетов %d..%d"
          % (len(boxes), len(new_boxes),
             min(r[39] for r in new_boxes), max(r[39] for r in new_boxes)))
    unresolved = [r[0] for r in new_boxes if not (0 <= r[39] < n_pkg)]
    chk(not unresolved, "все новые коробки резолвятся в PackageItem_Table (нерезолвенных %d)" % len(unresolved))
    return ok


# --- операции над живым сервером --------------------------------------------------------------
def snapshot():
    os.makedirs(DIR_ORIG, exist_ok=True)
    src = SITES[0][1]
    for name in ("item_table.txt", "SowelCombine.txt", "PackageItem_Table.txt", "GachaItemRateTable.txt"):
        with open(os.path.join(DIR_ORIG, name), "wb") as f:   # по SSH (было: shutil.copy2 с \\@@ASTORY_SERVER_HOSTNAME@@)
            f.write(srv_io.read_live(src + "/" + name))
        print("снято:", name, md5(os.path.join(DIR_ORIG, name)))


def write_live(out_it, out_sc):
    for site, folder, itname in SITES:
        for src, dst_name in ((out_it, itname), (out_sc, "SowelCombine.txt")):
            # по SSH: бэкап рядом (.BAK_SUFFIX), в v2\_backup и локально, scp + сверка md5 (srv_io; было: shutil с \\@@ASTORY_SERVER_HOSTNAME@@)
            with open(src, "rb") as f:
                data = f.read()
            print("площадка %s:" % site)
            srv_io.put_live(folder + "/" + dst_name, data, "." + BAK_SUFFIX,
                            "@@ASTORY_SERVER_ROOT_ESC@@\\Working_ASDA\\v2\\_backup\\" + BAK_SUFFIX, os.path.join(WORK, "live_backup"))


def check_live(out_it, out_sc):
    os.makedirs(DIR_CHECK, exist_ok=True)
    ok = True
    for site, folder, itname in SITES:
        for src, dst_name in ((out_it, itname), (out_sc, "SowelCombine.txt")):
            local = os.path.join(DIR_CHECK, site + "_" + dst_name)
            with open(local, "wb") as f:   # по SSH (было: shutil.copy2 с \\@@ASTORY_SERVER_HOSTNAME@@)
                f.write(srv_io.read_live(folder + "/" + dst_name))
            same = md5(local) == md5(src)
            print(("OK  " if same else "FAIL ") + "%s/%s перечитан, md5 %s" % (site, dst_name, md5(local)))
            ok = ok and same
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--snapshot", action="store_true", help="снять свежую копию живых таблиц")
    ap.add_argument("--build", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--check-live", action="store_true")
    a = ap.parse_args()
    if a.snapshot:
        snapshot()
    out_it = os.path.join(DIR_OUT, "item_table.txt")
    out_sc = os.path.join(DIR_OUT, "SowelCombine.txt")
    if a.build:
        build()
    if a.verify:
        ok = verify(out_it, out_sc,
                    os.path.join(DIR_ORIG, "PackageItem_Table.txt"),
                    os.path.join(DIR_ORIG, "GachaItemRateTable.txt"),
                    os.path.join(DIR_ORIG, "item_table.txt"),
                    os.path.join(DIR_ORIG, "SowelCombine.txt"))
        print("ИТОГ ПРОВЕРОК:", "ВСЁ ОК" if ok else "ЕСТЬ ОШИБКИ")
        if not ok:
            sys.exit(1)
    if a.write:
        write_live(out_it, out_sc)
    if a.check_live:
        sys.exit(0 if check_live(out_it, out_sc) else 1)


def _script_main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global BAK_SUFFIX
    BAK_SUFFIX = "bak_itemtail_" + time.strftime("%Y%m%d")
    for _c in range(1, 18):
        COLMAP[_c] = ("i32", 68 + 4 * (_c - 1))
    COLMAP[18] = ("u16", 136)   # dword@136 расщеплён сервером на два слова
    COLMAP[19] = ("u16", 138)
    for _c in range(20, 42):
        COLMAP[_c] = ("i32", 68 + 4 * (_c - 2))
    main()


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    _script_main()
