# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_verify_raw_datatables.py — ОБРАТНЫЙ ЭКСПОРТ четырёх таблиц, заменивших сырые файлы, и
#   побайтовая сверка с исходниками. Это не «проверка импортёра его же кодом»: строки читаются
#   ИЗ ГОТОВОГО DataTable (то, что реально будет в игре), собираются обратно в формат оригинала
#   и сравниваются с сырым файлом по md5.
#
#     DT_WarShopList   (140) ⇄ RawAssets/Data/Character/Honor_price_table.bin
#     DT_WarRank       ( 21) ⇄ RawAssets/Data/Character/CampWar_HonorTable.BIN
#     DT_EquipShopList ( 11) ⇄ RawAssets/Data/NPC/Shop/Freeshop.she
#     DT_EventSchedule (308) ⇄ RawAssets/Data/act_secession3.txt   (текст — сверка по значениям)
#
# ⚠ У `CampWar_HonorTable.BIN` в таблицу не кладётся MD5 записи (16 б) — эталонный загрузчик его
#   отбрасывает. При обратной сборке эти байты берутся из исходного файла; всё остальное (сдвиги
#   −43 у двух чисел и −1 у каждого байта имени) восстанавливается ИЗ ТАБЛИЦЫ.
#
# Запуск: Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_verify_raw_datatables.py
import hashlib
import os as _os
import struct
import sys as _sys

_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P

RAW = _os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Data")

PRICE_BIN = _os.path.join(RAW, "Character", "Honor_price_table.bin")
RANK_BIN = _os.path.join(RAW, "Character", "CampWar_HonorTable.BIN")
SHE = _os.path.join(RAW, "NPC", "Shop", "Freeshop.she")
SCHED = _os.path.join(RAW, "act_secession3.txt")

DT_PRICE = "/Game/AsdaAssets/Data/Character/DT_WarShopList"
DT_RANK = "/Game/AsdaAssets/Data/Character/DT_WarRank"
DT_SHE = "/Game/AsdaAssets/Data/NPC/DT_EquipShopList"
DT_SCHED = "/Game/AsdaAssets/Data/DT_EventSchedule"

HDR = 20
PRICE_FIELDS = ("StorePos", "ItemCode", "Flag8", "PriceType0", "PriceType1", "PriceCode0",
                "PriceCode1", "PriceQty0", "PriceQty1", "Category", "VictorOnly", "RegionFilter")


def _rows(unreal, path):
    """DataTable → {имя строки: словарь полей}, ЧЕРЕЗ ШТАТНЫЙ ЭКСПОРТ ТАБЛИЦЫ В JSON.

    Именно экспорт, а не чтение исходного JSON импортёра: так сверяется то, что реально легло
    в ассет (включая потери типов при заливке, если бы они были).
    """
    import json
    dt = unreal.EditorAssetLibrary.load_asset(path)
    if not dt:
        return None
    text = unreal.DataTableFunctionLibrary.export_data_table_to_json_string(dt)
    out = {}
    for rec in json.loads(text):
        out[str(rec.get("Name"))] = rec
    return out


def _get(row, field):
    """Значение поля строки из экспортированного JSON (ключи там — имена UPROPERTY как есть)."""
    return row[field]


def _read(path):
    with open(path, "rb") as f:
        return f.read()


def check_price(unreal, log):
    rows = _rows(unreal, DT_PRICE)
    if rows is None:
        log("!! нет таблицы %s" % DT_PRICE)
        return False
    raw = _read(PRICE_BIN)
    out = bytearray(raw[:HDR])
    for i in range(len(rows)):
        r = rows.get(str(i))
        if r is None:
            log("!! DT_WarShopList: нет строки с ключом %d" % i)
            return False
        out += struct.pack("<12i", *(int(_get(r, k)) for k in PRICE_FIELDS))
    return _report(log, "DT_WarShopList", len(rows), bytes(out), raw)


def check_rank(unreal, log):
    rows = _rows(unreal, DT_RANK)
    if rows is None:
        log("!! нет таблицы %s" % DT_RANK)
        return False
    raw = _read(RANK_BIN)
    rec = 56
    out = bytearray(raw[:HDR])
    for i in range(len(rows)):
        r = rows.get(str(i))
        if r is None:
            log("!! DT_WarRank: нет строки с ключом %d" % i)
            return False
        src = HDR + rec * i
        out += raw[src:src + 16]                                   # MD5 в таблицу не кладётся
        out += struct.pack("<ii", int(_get(r, "RankIndex")) + 43, int(_get(r, "HonorRequired")) + 43)
        name = str(_get(r, "Title")).encode("cp1251")
        enc = bytes(((c + 1) & 0xFF) for c in name)
        out += enc + raw[src + 24 + len(enc):src + rec]
    return _report(log, "DT_WarRank", len(rows), bytes(out), raw)


def check_she(unreal, log):
    rows = _rows(unreal, DT_SHE)
    if rows is None:
        log("!! нет таблицы %s" % DT_SHE)
        return False
    raw = _read(SHE)
    lists = []
    for i in range(len(rows)):
        r = rows.get(str(i))
        if r is None:
            log("!! DT_EquipShopList: нет строки с ключом %d" % i)
            return False
        lists.append([int(c) for c in _get(r, "ItemCodes")])
    out = bytearray(struct.pack("<%dH" % len(lists), *(len(l) for l in lists)))
    for l in lists:
        if l:
            out += struct.pack("<%di" % len(l), *l)
    return _report(log, "DT_EquipShopList", len(rows), bytes(out), raw)


def check_schedule(unreal, log):
    """Текстовый файл: сверяем ЗНАЧЕНИЯ (пробелы/табуляции в байтах не воспроизводимы)."""
    rows = _rows(unreal, DT_SCHED)
    if rows is None:
        log("!! нет таблицы %s" % DT_SCHED)
        return False
    src = []
    text = _read(SCHED).decode("cp1251", "replace")
    for line in text.splitlines():
        tok = line.split()
        if len(tok) < 6:
            continue
        if int(tok[0]) == 0:
            break
        src.append((int(tok[0]), int(tok[1]), int(tok[2]), int(tok[3]), int(tok[4]), float(tok[5])))
    bad = 0
    if len(rows) != len(src):
        log("!! DT_EventSchedule: строк в таблице %d, в файле %d" % (len(rows), len(src)))
        return False
    for i, exp in enumerate(src):
        r = rows.get(str(i))
        got = (int(_get(r, "Year")), int(_get(r, "Month")), int(_get(r, "Day")),
               int(_get(r, "StartHour")), int(_get(r, "EndHour")), float(_get(r, "Bonus")))
        if got != exp:
            bad += 1
            if bad <= 3:
                log("!! строка %d: таблица %s, файл %s" % (i, got, exp))
    log("DT_EventSchedule: строк %d, расхождений с файлом: %d%s"
        % (len(rows), bad, " — СОВПАЛО" if bad == 0 else ""))
    return bad == 0


def _report(log, name, count, built, raw):
    same = hashlib.md5(built).hexdigest() == hashlib.md5(raw).hexdigest()
    log("%s: строк %d, обратная сборка %s (%d б, ожидалось %d)"
        % (name, count, "СОВПАЛА побайтово с сырым файлом" if same else "РАСХОДИТСЯ",
           len(built), len(raw)))
    return same


def main(unreal):
    def log(m):
        unreal.log_warning("[сверка таблиц] %s" % m)

    ok = True
    for fn in (check_price, check_rank, check_she, check_schedule):
        ok = fn(unreal, log) and ok
    log("ИТОГ: %s" % ("все четыре таблицы совпали с исходниками" if ok else "ЕСТЬ РАСХОЖДЕНИЯ"))
    return ok


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__":
    import unreal as _u
    main(_u)
