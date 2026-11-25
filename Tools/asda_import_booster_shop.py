# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_booster_shop.py — АССОРТИМЕНТ МАГАЗИНА БУСТЕРОВ (окно `BOOSTER_SHOP_WIN` 15300):
#   `Data\NPC\Shop\Bpremium.BIN` + `Data\NPC\Shop\Fpremium.BIN`
#     → /Game/AsdaAssets/Data/NPC/DT_BoosterShopList
#   + (05.11.2026) ведомость вкладок `Tools/data/booster_shop_tabs.csv` → /Game/AsdaAssets/Data/NPC/DT_BoosterShopTabs.
#   Витрину правит редактор `Tools/BoosterShopEditor` (README там же); этот импортёр — его последний шаг (DT).
#
# ══════════════════════ ПОЧЕМУ ОТДЕЛЬНАЯ ТАБЛИЦА, А НЕ `DT_booster_list` ══════════════════════
# ⚠ В отчёте `Reports/premium_mall_shops.md` §8.1 написано «ассортимент уже импортирован в
#   `DT_booster_list`» — ЭТО ОШИБКА ОТЧЁТА. `DT_booster_list` — это `booster_list.bin` (шаг 284 б,
#   35 пар «код+вес»), таблица БАРАБАНА `BOOSTING_WIN` 10050, к магазину 15300 отношения не имеет
#   (закон `premium-gacha-vs-booster`, память `dead-code-markings-suspect`: проверять, а не верить).
#   Ассортимент магазина лежит в ДВУХ ДРУГИХ файлах, и их в порту не было вовсе.
#
# ══════════════════════ ФОРМАТ (реверс ЗАГРУЗЧИКОВ, не догадка) ═══════════════════════════════
# `BoosterShop_LoadBpremium@0xB882F0` (`AsdaStory_ida.c:423491-423533`) и
# `BoosterShop_LoadFpremium@0xB884E0` (`:423547-423586`):
#     fread(header, 0x14, 1, f);          // ШАПКА 20 байт
#     if (strncmp(magicRef, magic, 4)) return 0;
#     reserve(count);  for (i < count) fread(rec, 0x50, 1, f);   // ЗАПИСЬ 80 байт
# Раскладка шапки восстановлена по стеку функции (`header[4]` @ebp−0x20, `count` @ebp−0x1C,
#   4 неиспользуемых байта @ebp−0x18, `magic[8]` @ebp−0x14; всего ровно 0x14):
#     +0  i32  ноль (в обоих файлах RU-сборки)
#     +4  i32  **count** — число записей
#     +8  i32  версия (5 у B, 3 у F)
#     +12 4 б  **magic**: "BII" у Bpremium, "FII" у Fpremium (сверяется `strncmp(..., 4)`)
#     +16 4 б  ноль
#   Сходится побайтово: Bpremium 5780 = 20 + 72×80 (count=72), Fpremium 20 = 20 + 0×80 (count=0).
#
# Поля записи 80 б — по коду ОТРИСОВКИ (`itemGridRefreshPage@0xB81460`, `:421061-421110`):
#     +0x00 i32  itemCode                          (`:421035`)
#     +0x04 …    имя предмета, cp1251, до 64 б      — кодом отрисовки НЕ ЧИТАЕТСЯ (имя берётся из
#                                                    `item_list` по коду); сохраняем как справку
#     +0x44 i32  itemType — селектор категории 9/12/13 (`:421036`, читается как u8)
#     +0x48 i32  процент скидки 10…90, 0 = скидки нет (`:421071-421085`)
#     +0x4C i32  бейдж: 1 → плашка `SHOP_HOT`, 2 → `SHOP_NEW` (`:421087-421095`, читается как u8)
#   Остальные байты в срезе не читаются НИКЕМ — кладём в `DataHex` целиком (закон «1:1»).
#
# ★ ПОРЯДОК ЗАПИСЕЙ ЗНАЧИМ: `page = i / cellsPerPage`. Bpremium эталон ПОСЛЕ чтения сортирует
#   (`introSort` c компаратором `sub_930F25` → `sub_B81E10`, тело `return a20 > (int)a40`, где
#   a20/a40 — dword №19 каждой из двух 80-байтовых записей, т.е. **байт +0x4C = БЕЙДЖ**):
#   сортировка по бейджу ПО УБЫВАНИЮ (NEW=2, затем HOT=1, затем 0). Fpremium НЕ сортируется.
#   Мы сортируем УСТОЙЧИВО (`sorted(..., key=-badge)`), эталонный introsort устойчивости не даёт;
#   при равных бейджах (в RU-данных бейдж=2 у всех 72) порядок файла сохраняется — это
#   ДЕТЕРМИНИРОВАННОЕ поведение вместо неопределённого. Поле `Order` фиксирует итог.
#
# ══════════════════════ ЧТО В ДАННЫХ RU-СБОРКИ (факт, не предположение) ═══════════════════════
#   Bpremium: 72 записи, У ВСЕХ itemType = 12 («Доспех»), скидка = 0, бейдж = 2 (NEW).
#   Fpremium: 0 записей ⇒ вкладка «Популярные» (индекс 1) в этой сборке ПУСТА.
#
# `--verify` собирает байты файлов ОБРАТНО из разобранных строк и сверяет md5 с исходниками.
#
# Запуск:  Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_import_booster_shop.py
#   без UE: python asda_import_booster_shop.py --json-only
#   сверка: python asda_import_booster_shop.py --verify
import os as _os
import struct as _struct
import sys as _sys

_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_dt_common as _DT
import asda_bintable as _BT     # ★ дедуп №2 В1 Т5: общий обход записей и C-строки

SRC_DIR = _os.path.join(_P.DATA, "NPC", "Shop")
FILES = [("B", "Bpremium.BIN", b"BII"), ("F", "Fpremium.BIN", b"FII")]

OUT_DIR = _os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Data", "NPC")
OUT_JSON = _os.path.join(OUT_DIR, "BoosterShopList.json")

PKG = "/Game/AsdaAssets/Data/NPC"
NAME = "DT_BoosterShopList"
ROW_STRUCT = "/Script/AsdaReborn.AsdaBoosterShopRow"

HDR = 20
REC = 80
TAG = "магазин бустеров"


def _read(path):
    with open(path, "rb") as f:
        return f.read()


def _name_cp1251(raw):
    """Имя записи: cp1251-строка до первого нуля (не декодировалась — latin-1 с заменой).
    Только СПРАВКА — рантайм берёт имя из item_list. Тело — общий `asda_bintable`."""
    return _BT.decode_text(_BT.cstr_bytes(raw), "cp1251", fallback="latin-1")


def parse_bin(raw, magic):
    """Разбор одного файла. None — шапка не совпала (ровно как `strncmp` эталона)."""
    if len(raw) < HDR:
        return None
    if raw[12:12 + len(magic)] != magic:
        return None
    count = _struct.unpack_from("<i", raw, 4)[0]
    if count < 0 or HDR + REC * count > len(raw):
        return None
    out = []
    # ★ дедуп №2 В1 Т5: обход — общий `asda_bintable.iter_records` (размер уже сверен выше: все записи целы).
    for i, rec in _BT.iter_records(raw, REC, count=count, base=HDR):
        out.append({
            "Order": i,
            "ItemCode": _struct.unpack_from("<i", rec, 0)[0],
            "RawName": _name_cp1251(rec[4:68]),
            "ItemType": _struct.unpack_from("<i", rec, 0x44)[0],
            "DiscountPct": _struct.unpack_from("<i", rec, 0x48)[0],
            "Badge": _struct.unpack_from("<i", rec, 0x4C)[0],
            "DataHex": rec.hex(),
        })
    return out


def rebuild_bin(rows, header20):
    """Обратная сборка: шапка как в исходнике + записи из `DataHex` в ПОРЯДКЕ ФАЙЛА."""
    body = b"".join(bytes.fromhex(r["DataHex"]) for r in sorted(rows, key=lambda r: r["Order"]))
    return header20 + body


def verify(log):
    import hashlib
    ok = True
    for src, fname, magic in FILES:
        path = _os.path.join(SRC_DIR, fname)
        if not _os.path.isfile(path):
            log("!! нет исходника %s" % path)
            ok = False
            continue
        raw = _read(path)
        rows = parse_bin(raw, magic)
        if rows is None:
            log("!! %s: шапка не бьётся (magic %s)" % (fname, magic))
            ok = False
            continue
        back = rebuild_bin(rows, raw[:HDR])
        same = hashlib.md5(back).hexdigest() == hashlib.md5(raw).hexdigest()
        log("%s: записей %d, обратная сборка %s (%d б)"
            % (fname, len(rows), "СОВПАЛА побайтово" if same else "РАСХОДИТСЯ", len(raw)))
        ok = ok and same
    return ok


def collect(log):
    """Обе таблицы одним списком строк. Ключ строки — `B000`…`B071` / `F000`…"""
    all_rows = []
    for src, fname, magic in FILES:
        path = _os.path.join(SRC_DIR, fname)
        if not _os.path.isfile(path):
            log("!! нет исходника %s" % path)
            continue
        rows = parse_bin(_read(path), magic)
        if rows is None:
            log("!! %s: шапка не бьётся" % fname)
            continue
        if src == "B":
            # ★ Эталон сортирует Bpremium по бейджу ПО УБЫВАНИЮ (см. шапку файла). Устойчиво.
            rows = sorted(rows, key=lambda r: -int(r["Badge"] & 0xFF))
        for pos, r in enumerate(rows):
            r["Source"] = src
            r["Order"] = pos          # ПОРЯДОК ПОСЛЕ сортировки — по нему рантайм строит страницы
        all_rows.extend(rows)
        log("%s: записей %d%s" % (fname, len(rows), " (отсортировано по бейджу)" if src == "B" else ""))
    return all_rows


# ★★★ [ИСПРАВЛЕНИЕ] 31.10.2026 (задача владельца «вкладки магазина бустеров не распределены», сессия item-level-boxes):
#   категория витрины (байт +0x44 записи, по нему эталон `itemGridRefreshPage@0xB81460` раскладывает вкладки) у ВСЕХ 72
#   записей RU-сборки = 12 («Доспех») — оружейные бустеры во вкладки «Воин/Лучник/Маг» не попадали никогда. Владелец:
#   «согласно предметам внутри бустеров разместить соответственно». Ведомость CATEGORY_CSV задаёт категорию по составу
#   бустера (booster_list): оружие (тип 9) → 9, набор брони → 12, прочее → 13. Класс оружия для вкладок 2/3/4 рантайм берёт
#   из состава (`UAsdaBoosterShopSubsystem::PassesCategory`, ключ asda.PremiumMall.BoosterCatByContents).
#   `--verify` сверяет ИСХОДНЫЕ файлы — ведомость там не применяется.
CATEGORY_CSV = _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "data", "booster_shop_category.csv")


def apply_categories(rows, log):
    """Ведомость категорий (source,code,…,category) → поле ItemType и байты +0x44 в DataHex записи."""
    import csv as _csv
    if not _os.path.isfile(CATEGORY_CSV):
        return
    with open(CATEGORY_CSV, encoding="utf-8-sig", newline="") as f:
        cat = {(r["source"], int(r["code"])): int(r["category"]) for r in _csv.DictReader(f)}
    n = 0
    for r in rows:
        c = cat.get((r["Source"], int(r["ItemCode"])))
        if c is None or c == r["ItemType"]:
            continue
        rec = bytearray(bytes.fromhex(r["DataHex"]))
        _struct.pack_into("<i", rec, 0x44, c)
        r["DataHex"] = rec.hex()
        r["ItemType"] = c
        n += 1
    log("ведомость категорий %s: строк %d, изменено %d" % (_os.path.basename(CATEGORY_CSV), len(cat), n))


def asset_matches_previous_json(unreal, log, name=None, out_json=None, keys=None):
    """Сторож ручной правки (закон owner-asset-edits-never-lose): строки ассета == прежний JSON. Нет ассета/JSON — True.
    По умолчанию — ассортимент (NAME/OUT_JSON); для вкладок зовётся с NAME_TABS/TABS_JSON/TAB_KEYS."""
    import json as _json
    name = name or NAME
    out_json = out_json or OUT_JSON
    keys = keys or ("ItemCode", "ItemType", "DiscountPct", "Badge", "Order", "Source")
    full = PKG + "/" + name
    # Сначала «есть ли ассет»: load_asset несуществующего пишет в журнал ложную ошибку (DT вкладок создаётся впервые).
    dt = unreal.EditorAssetLibrary.load_asset(full) if unreal.EditorAssetLibrary.does_asset_exist(full) else None
    if not dt or not _os.path.isfile(out_json):
        return True
    prev = {str(r["Name"]): tuple(r.get(k) for k in keys) for r in _json.load(open(out_json, encoding="utf-8"))}
    cur = {str(r["Name"]): tuple(r.get(k) for k in keys)
           for r in _json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(dt))}
    if cur != prev:
        diff = sorted(k for k in set(cur) | set(prev) if cur.get(k) != prev.get(k))
        log("СТОП: %s расходится с прежним %s (ручная правка?) — строки %s; ассет не трогаю" % (name, out_json, diff[:10]))
        return False
    return True


# ★★★ [ПОРТ] 05.11.2026 (задача владельца «редактор бустер-шопа», Tools/BoosterShopEditor): ПОРЯДОК И НАБОР ВКЛАДОК
#   полосы 15545. В оригинале они зашиты в коде (кнопка i = категория i, все семь; подписи `ida.c:420398-420411`), файла у
#   них нет. Источник — ведомость TABS_CSV (slot,filter,name_ru): кнопка `slot` показывает вкладку с эталонным смыслом
#   `filter` (0 Все · 1 Популярные/Fpremium · 2 Воин · 3 Лучник · 4 Маг · 5 Доспех · 6 Инструмент). Видимые вкладки идут
#   подряд с 0, отсутствующие фильтры сняты с витрины. Нет ведомости — DT_BoosterShopTabs не трогается, рантайм берёт
#   эталонную раскладку (`UAsdaBoosterShopSubsystem::LoadTabs`, ключ asda.PremiumMall.BoosterTabsFromTable).
TABS_CSV = _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "data", "booster_shop_tabs.csv")
TABS_JSON = _os.path.join(OUT_DIR, "BoosterShopTabs.json")
NAME_TABS = "DT_BoosterShopTabs"
ROW_STRUCT_TABS = "/Script/AsdaReborn.AsdaBoosterShopTabRow"
TAB_KEYS = ("Slot", "Filter")
TAB_COUNT = 7   # кнопок на полосе 15545 (BOOSTER_SHOP_MENU_OPT0…6)


def collect_tabs(log):
    """Ведомость вкладок → строки {Slot, Filter} по порядку кнопок. None — ведомости нет; ValueError — она битая."""
    import csv as _csv
    if not _os.path.isfile(TABS_CSV):
        return None
    with open(TABS_CSV, encoding="utf-8-sig", newline="") as f:
        rows = [{"Slot": int(r["slot"]), "Filter": int(r["filter"])} for r in _csv.DictReader(f)]
    rows.sort(key=lambda r: r["Slot"])
    seen = set()
    for i, r in enumerate(rows):
        # Те же правила, что у рантайма: кнопки подряд с нуля, фильтр 0…6 без повторов (иначе рантайм возьмёт эталон).
        if r["Slot"] != i or i >= TAB_COUNT or not 0 <= r["Filter"] < TAB_COUNT or r["Filter"] in seen:
            raise ValueError("ведомость вкладок %s: строка slot=%d filter=%d неверна" % (TABS_CSV, r["Slot"], r["Filter"]))
        seen.add(r["Filter"])
    log("ведомость вкладок %s: вкладок %d" % (_os.path.basename(TABS_CSV), len(rows)))
    return rows


def tabs_json_text(rows):
    """JSON строк вкладок для DataTable (ключ строки T0…T6)."""
    return _DT.json_rows(rows, lambda r: "T%d" % r["Slot"])


def main(unreal=None):
    def log(m):
        if unreal:
            unreal.log_warning("[%s] %s" % (TAG, m))
        else:
            print("[%s] %s" % (TAG, m))

    rows = collect(log)
    if not rows:
        log("!! ассортимент пуст — таблица не создана")
        return False
    apply_categories(rows, log)

    text = _DT.json_rows(rows, lambda r: "%s%03d" % (r["Source"], r["Order"]))
    tabs = collect_tabs(log)   # [ПОРТ] 05.11.2026: раскладка полосы вкладок (None — ведомости нет)
    tabs_text = tabs_json_text(tabs) if tabs is not None else None
    if unreal is not None and not asset_matches_previous_json(unreal, log):
        return False
    if unreal is not None and tabs is not None and \
            not asset_matches_previous_json(unreal, log, NAME_TABS, TABS_JSON, TAB_KEYS):
        return False
    _os.makedirs(OUT_DIR, exist_ok=True)
    with open(OUT_JSON, "w", encoding="utf-8", newline="") as f:
        f.write(text)
    log("JSON: %s (строк %d)" % (OUT_JSON, len(rows)))
    if tabs_text is not None:
        with open(TABS_JSON, "w", encoding="utf-8", newline="") as f:
            f.write(tabs_text)
        log("JSON: %s (вкладок %d)" % (TABS_JSON, len(tabs)))
    if unreal is None:
        return True
    ok = _DT.fill_dt(unreal, PKG, NAME, ROW_STRUCT, text, log, fmt="json",
                     missing_hint="Public/UI/AsdaBoosterShopSubsystem.h, FAsdaBoosterShopRow") is not None
    if ok and tabs_text is not None:
        ok = _DT.fill_dt(unreal, PKG, NAME_TABS, ROW_STRUCT_TABS, tabs_text, log, fmt="json",
                         missing_hint="Public/UI/AsdaBoosterShopSubsystem.h, FAsdaBoosterShopTabRow") is not None
    if unreal is not None:
        log("ИМПОРТ ВИТРИНЫ ЗАВЕРШЁН: %s" % ("OK" if ok else "ОШИБКА"))
    return ok


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__":
    if "--verify" in _sys.argv:
        verify(lambda m: print("[%s] %s" % (TAG, m)))
    elif "--json-only" in _sys.argv:
        main(None)
    else:
        import unreal as _u
        main(_u)
