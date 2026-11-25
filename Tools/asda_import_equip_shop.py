# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_equip_shop.py — ВИТРИНА МАГАЗИНА СНАРЯЖЕНИЯ (окно SHELLITEM_STORE2_WIN 6800):
#   `Data\NPC\Shop\Freeshop.she` → /Game/AsdaAssets/Data/NPC/DT_EquipShopList (11 строк).
#
# ══════════════════════ ФОРМАТ `.she` (реверс загрузчика, НЕ догадка) ═══════════════════════════
# `cShopLoadFreeShop@0xCAB270`:
#     fread(counts, 2, 11, f);                       // ОДИННАДЦАТЬ u16 — длины списков
#     for (k = 0; k < 11; ++k)                       // затем 11 массивов подряд, без выравнивания
#         fread(list[k], 4, counts[k], f);           // i32 itemCode
# То есть файл = `[11 × u16 count][count0 × i32][count1 × i32]…[count10 × i32]`,
#   размер = 22 + 4*sum(counts). На нашем `Freeshop.she` сходится побайтово:
#   286 = 22 + 4*66, счётчики (0,0,0,0,24,9,9,9,3,3,9).
#
# ★ ПОРЯДОК КОДОВ ВНУТРИ СПИСКА ЗНАЧИМ. Эталон раскладывает витрину так:
#   `page = i / ItemsPerPage`, `slot = i % ItemsPerPage` — то есть место карточки берётся из
#   ПОЗИЦИИ кода в массиве. Сортировать список нельзя: страницы перемешаются.
#
# ★ ТРИ ПРОИЗВОДНЫХ СПИСКА (11 «Воин», 12 «Лучник», 13 «Маг») В ТАБЛИЦУ НЕ КЛАДЁМ.
#   Загрузчик строит их из списка №4, фильтруя по `itemType == 9` и `itemSubType`
#   (`<4` → 11, `8…11` → 12, `4…7` → 13), то есть по ДАННЫМ item-БД, а не по файлу витрины.
#   Заморозить их в таблице значило бы, что правка `item_list` перестала бы на них влиять,
#   — их по-прежнему строит рантайм (`UAsdaEquipShopSubsystem::LoadShe`).
#
# ⚠ БЕРЁМ ФАЙЛ НАШЕГО КЛИЕНТА `AsdaChroniclesInterlude_MINE` — тот же, что лежал в Content
#   (md5 совпал: 2cd61cac2e41355bdae8fc6194d92dc4).
#
# `--verify` собирает байты файла ОБРАТНО из разобранных списков и сверяет md5 с исходником.
#
# Запуск:  Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_import_equip_shop.py
#   без UE: python asda_import_equip_shop.py --json-only
#   сверка: python asda_import_equip_shop.py --verify
import os as _os
import struct
import sys as _sys

_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_dt_common as _DT

SHE = _os.path.join(_P.DATA, "NPC", "Shop", "Freeshop.she")
OUT_DIR = _os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Data", "NPC")
OUT_JSON = _os.path.join(OUT_DIR, "EquipShopList.json")

PKG = "/Game/AsdaAssets/Data/NPC"
NAME = "DT_EquipShopList"
ROW_STRUCT = "/Script/AsdaReborn.AsdaEquipShopListRow"

LISTS = 11              # столько списков лежит в самом файле


def _read(path):
    with open(path, "rb") as f:
        return f.read()


def parse_she(raw):
    """`.she` → список из 11 словарей `{ListIndex, ItemCodes}`. None — файл не бьётся с шапкой."""
    if len(raw) < 2 * LISTS:
        return None
    counts = struct.unpack_from("<%dH" % LISTS, raw, 0)
    if 2 * LISTS + 4 * sum(counts) != len(raw):
        return None
    rows = []
    off = 2 * LISTS
    for k, n in enumerate(counts):
        codes = list(struct.unpack_from("<%di" % n, raw, off)) if n else []
        off += 4 * n
        rows.append({"ListIndex": k, "ItemCodes": codes})
    return rows


def rebuild_she(rows):
    """Собрать байты `.she` обратно из разобранных списков."""
    out = bytearray(struct.pack("<%dH" % LISTS, *(len(r["ItemCodes"]) for r in rows)))
    for r in rows:
        if r["ItemCodes"]:
            out += struct.pack("<%di" % len(r["ItemCodes"]), *r["ItemCodes"])
    return bytes(out)


def verify(log):
    import hashlib
    raw = _read(SHE)
    rows = parse_she(raw)
    if rows is None:
        log("!! файл не бьётся с шапкой: %s" % SHE)
        return False
    back = rebuild_she(rows)
    same = hashlib.md5(back).hexdigest() == hashlib.md5(raw).hexdigest()
    log("списков %d, товаров всего %d, обратная сборка %s (%d б)"
        % (len(rows), sum(len(r["ItemCodes"]) for r in rows),
           "СОВПАЛА побайтово" if same else "РАСХОДИТСЯ", len(raw)))
    log("длины списков: %s" % ", ".join(str(len(r["ItemCodes"])) for r in rows))
    return same


def _norm_cell(v):
    """Ячейка для сравнения ассета с прежним JSON: числа по значению, списки — поэлементно, строки без регистра."""
    if isinstance(v, list):
        return [_norm_cell(x) for x in v]
    if isinstance(v, bool):
        return str(v).lower()
    try:
        return float(v)
    except (TypeError, ValueError):
        return str(v).strip().lower()


def asset_matches_previous_json(unreal, log):
    """★ 19.11.2026 (админ-центр А4.1, закон owner-asset-edits-never-lose): сторож ручной правки — строки ассета DT_EquipShopList
    == прежний OUT_JSON (до этого прогона) по всем общим полям. Нет ассета или прежнего JSON — сравнивать нечего (True).
    Образец — asda_import_premium_shop_bin.asset_matches_previous_json."""
    import json
    dt = unreal.EditorAssetLibrary.load_asset(PKG + "/" + NAME)
    if not dt or not _os.path.isfile(OUT_JSON):
        return True
    prev = {str(r.get("Name", r.get("ListIndex"))): r for r in json.load(open(OUT_JSON, encoding="utf-8"))}
    cur = {str(r["Name"]): r for r in json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(dt))}
    diff = sorted(k for k in set(prev) ^ set(cur))
    for k in set(prev) & set(cur):
        a, b = prev[k], cur[k]
        # ★ ревизия 33 К-3: ВСЕ поля, которые импортёр пишет; поля нет в экспорте ассета — тоже расхождение
        if any(f not in b or _norm_cell(a[f]) != _norm_cell(b[f]) for f in a if f != "Name"):
            diff.append(k)
    if diff:
        log("СТОП: %s расходится с прежним %s (ручная правка?) — строки %s; ассет не трогаю" % (NAME, OUT_JSON, sorted(diff)[:10]))
        return False
    return True


def main(unreal=None):
    def log(m):
        if unreal:
            unreal.log_warning("[магазин снаряжения] %s" % m)
        else:
            print("[магазин снаряжения] %s" % m)

    rows = parse_she(_read(SHE))
    if rows is None:
        log("!! исходник не разобран: %s" % SHE)
        return False
    log("списков %d, товаров всего %d" % (len(rows), sum(len(r["ItemCodes"]) for r in rows)))

    text = _DT.json_rows(rows, lambda r: str(r["ListIndex"]))
    if unreal is not None and not asset_matches_previous_json(unreal, log):   # ★ 19.11 А4.1: ДО записи JSON и DT
        log("ЗАВЕРШЕНО: импорт %s (СТОП: ручная правка)" % NAME)
        return False
    _os.makedirs(OUT_DIR, exist_ok=True)
    with open(OUT_JSON, "w", encoding="utf-8", newline="") as f:
        f.write(text)
    log("JSON: %s" % OUT_JSON)
    if unreal is None:
        return True
    ok = _DT.fill_dt(unreal, PKG, NAME, ROW_STRUCT, text, log, fmt="json",
                     missing_hint="Public/UI/AsdaEquipShopSubsystem.h, FAsdaEquipShopListRow") is not None
    log("ЗАВЕРШЕНО: импорт %s (%s)" % (NAME, "ok" if ok else "ПРОВАЛ"))   # ★ 19.11 А4.1: маркер ПОСЛЕ сохранения DT (панель)
    return ok


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__":
    if "--verify" in _sys.argv:
        verify(lambda m: print("[магазин снаряжения] %s" % m))
    elif "--json-only" in _sys.argv:
        main(None)
    else:
        import unreal as _u
        main(_u)
