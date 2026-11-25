# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_npc_shop.py — АССОРТИМЕНТ МАГАЗИНОВ NPC: `Data\NPC\Shop\js<npcId>.spd` → DT_NpcShopList.
#
# ══════════════════════════ ФОРМАТ `.spd` (реверс загрузчика, НЕ догадка) ═══════════════════════
# `NPCShop_LoadBuyList @0xE28700` (npc_mob_pet.c:6641):
#     sprintf(filePath, "./data/npc/Shop/js%d.spd", ЭТОТ_ОКНО[+476]);   // +476 = npcId, см. ниже
#     fread(fileHeader, 8, 1, f);                 // ШАПКА 8 байт: два u32
#     codeArray = new int[fileHeader[1]];         // fileHeader[1] = ЧИСЛО ЗАПИСЕЙ
#     fread(codeArray, 4, fileHeader[1], f);      // дальше подряд u32 — КОДЫ ПРЕДМЕТОВ
# То есть файл = `[u32 =0][u32 count][count × u32 itemCode]`, размер = 8 + 4*count.
# Контроль на всех 19 файлах сборки `AsdaChroniclesInterlude_MINE` сходится побайтово
# (js1: count=28 → 120 б; js3: 144 → 584 б; js16: 128 → 520 б; js131: 1 → 12 б).
#
# ★ ИМЯ ФАЙЛА = npcId. В `NPCShop_OpenWindow @0xE25C00` третий аргумент (`pThis[436]` окна разговора
#   = «с каким NPC говорим») кладётся в поле окна, которое `NPCShop_LoadBuyList` и подставляет в
#   `js%d.spd`. Никакого отдельного «идентификатора магазина» в данных нет.
#
# ★ КОЛИЧЕСТВО В ЗАПИСИ НЕ ХРАНИТСЯ: загрузчик сам достаёт «потолок стопки» из справочника
#   (`CItemManager_GetItemRecord(code)->flagWord176`, при <= 1 берётся 1) и кладёт его пятым
#   полем 20-байтовой записи витрины. Для кодов самоцветов (`g_SowelCodeRangeLo..Hi`) потолок = 0.
#   ⇒ в таблицу переносим ТОЛЬКО коды; потолок считает рантайм по DT_item_list (тот же источник).
#
# ⚠ БЕРЁМ НАБОР НАШЕГО КЛИЕНТА. В сборке `Donors\ArabicASDAStory\data\NPC\Shop\` файлов 263, но это
#   ДРУГАЯ сборка с другими npcId; наш клиент — `AsdaChroniclesInterlude_MINE` (19 файлов).
#
# ⚠ `PriceTable.BIN` из этого же каталога магазин NPC НЕ ЧИТАЕТ (см. отчёт `npc_shop_port.md` §4):
#   единственный его загрузчик — `CItemManager_LoadPriceTableBin @0x9C4AC0`, и берёт он запись
#   не отсюда, а из `Data\character\Bin.bbz`. Цены магазина приходят из `item_list`/`sowell_list`.
#
# Запуск: Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_import_npc_shop.py
#   без UE:  python asda_import_npc_shop.py --json-only
import os as _os
import re
import struct
import sys as _sys

_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_dt_common as _DT

SHOP_DIR = _os.path.join(_P.DATA, "NPC", "Shop")
OUT_DIR = _P.ROOT + "\\AstoryClient\\RawAssets\\Data\\NPC"
OUT_JSON = _os.path.join(OUT_DIR, "NpcShop.json")
PKG = "/Game/AsdaAssets/Data/NPC"
NAME = "DT_NpcShopList"
ROW_STRUCT = "/Script/AsdaReborn.AsdaNpcShopRow"


def parse_spd(path):
    """`.spd` → список кодов предметов. Возвращает None, если файл не бьётся с шапкой."""
    with open(path, "rb") as f:
        raw = f.read()
    if len(raw) < 8:
        return None
    _zero, count = struct.unpack_from("<II", raw, 0)
    if 8 + 4 * count != len(raw):
        return None
    return list(struct.unpack_from("<%dI" % count, raw, 8)) if count else []


def collect(log):
    """Все `js<N>.spd` каталога магазина → строки будущей таблицы, отсортированные по npcId."""
    rows = []
    if not _os.path.isdir(SHOP_DIR):
        log("!! каталога нет: %s" % SHOP_DIR)
        return rows
    for fn in sorted(_os.listdir(SHOP_DIR)):
        m = re.fullmatch(r"js(\d+)\.spd", fn, re.IGNORECASE)
        if not m:
            continue
        npc_id = int(m.group(1))
        codes = parse_spd(_os.path.join(SHOP_DIR, fn))
        if codes is None:
            log("!! %s не бьётся с шапкой (8 + 4*count != размер) — пропущен" % fn)
            continue
        rows.append({"NpcId": npc_id, "ItemCodes": codes})
    rows.sort(key=lambda r: r["NpcId"])
    total = sum(len(r["ItemCodes"]) for r in rows)
    log("магазинов: %d, товаров всего: %d" % (len(rows), total))
    # ★★★ [ИСПРАВЛЕНИЕ] 29.10.2026 (баг игроков «Blacksmith Vision Scroll … in John's shop», сессия item-level-boxes):
    #   товары, которые СЕРВЕР не продаёт (`CharItemBuy@0x50F170`: item_table кол.38/+62 = 0 → 5021 код 5, нотис 9569
    #   «Not an item you can purchase at a store»; у стокового сервера так же — дефект оригинала: эталон показывает всё
    #   из `js<N>.spd` без фильтра, npc_mob_pet.c:6670), в витрину не попадают. Список — ведомость HIDDEN_CSV
    #   (генератор и сверка: Reports/npc_shop_audit/shop_audit.py). Вернуть товар = убрать строку + включить флаг на сервере.
    hidden = load_hidden(log)
    if hidden:
        cut = 0
        for r in rows:
            keep = [c for c in r["ItemCodes"] if (r["NpcId"], c) not in hidden]
            cut += len(r["ItemCodes"]) - len(keep)
            r["ItemCodes"] = keep
        log("скрыто по ведомости %s: %d (в витрине %d)" % (_os.path.basename(HIDDEN_CSV), cut, total - cut))
    return rows


# Ведомость скрытых товаров: npc,code,name_ru,reason (UTF-8). Пары (npcId, код).
HIDDEN_CSV = _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "data", "npc_shop_hidden.csv")


def load_hidden(log):
    """Ведомость скрытых товаров → множество (npcId, код); нет файла — пусто."""
    import csv as _csv
    if not _os.path.isfile(HIDDEN_CSV):
        return set()
    with open(HIDDEN_CSV, encoding="utf-8-sig", newline="") as f:
        out = {(int(r["npc"]), int(r["code"])) for r in _csv.DictReader(f)}
    log("ведомость скрытых товаров: %d строк" % len(out))
    return out


def asset_matches_previous_json(unreal, log):
    """Сторож ручной правки (закон owner-asset-edits-never-lose): строки ассета == прежний OUT_JSON (до этого прогона).
    Нет ассета или нет прежнего JSON — нечего сравнивать (True)."""
    import json as _json
    dt = unreal.EditorAssetLibrary.load_asset(PKG + "/" + NAME)
    if not dt or not _os.path.isfile(OUT_JSON):
        return True
    prev = {str(r["Name"]): list(r["ItemCodes"]) for r in _json.load(open(OUT_JSON, encoding="utf-8"))}
    cur = {}
    for r in _json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(dt)):
        cur[str(r["Name"])] = [int(x) for x in r["ItemCodes"]]
    if cur != prev:
        diff = sorted(k for k in set(cur) | set(prev) if cur.get(k) != prev.get(k))
        log("СТОП: %s расходится с прежним %s (ручная правка?) — магазины %s; ассет не трогаю" % (NAME, OUT_JSON, diff[:10]))
        return False
    return True


def main(unreal=None):
    def log(m):
        if unreal:
            unreal.log_warning("[магазин-NPC] %s" % m)
        else:
            print("[магазин-NPC] %s" % m)

    rows = collect(log)
    text = _DT.json_rows(rows, lambda r: str(r["NpcId"]))
    if unreal is not None and not asset_matches_previous_json(unreal, log):
        log("ЗАВЕРШЕНО: импорт %s (СТОП: ручная правка)" % NAME)
        return False
    _os.makedirs(OUT_DIR, exist_ok=True)
    with open(OUT_JSON, "w", encoding="utf-8", newline="") as f:
        f.write(text)
    log("JSON: %s" % OUT_JSON)
    if unreal is None:
        return True
    ok = _DT.fill_dt(unreal, PKG, NAME, ROW_STRUCT, text, log, fmt="json",
                     missing_hint="Public/Npc/AsdaNpcDialogTypes.h, FAsdaNpcShopRow") is not None
    log("ЗАВЕРШЕНО: импорт %s (%s)" % (NAME, "ok" if ok else "ПРОВАЛ"))   # ★ 19.11.2026 (ревизия 30 В-6): маркер ПОСЛЕ сохранения DT
    return ok


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__":
    if "--json-only" in _sys.argv:
        main(None)
    else:
        import unreal as _u
        main(_u)
