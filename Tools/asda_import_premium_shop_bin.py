# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_premium_shop_bin.py — ★★★ НАВИГАЦИЯ ЭЛИТНОГО МАГАЗИНА (окно 6000):
#   `Data\NPC\Shop\PremiumShop.BIN` -> /Game/AsdaAssets/Data/NPC/DT_PremiumShopBin (715 строк).
#
# ══════════════════════ ЗАЧЕМ ЭТО, ЕСЛИ УЖЕ ЕСТЬ `DT_PremiumShopList` ══════════════════════════
# Это РАЗНЫЕ данные для РАЗНЫХ полос одного окна, и до 2026-08-28 порт знал только первую:
#   • `Premium.SHE` -> `DT_PremiumShopList` — списки СТАРОЙ полосы 6009 (`SHELLITEM_STORE_MENU`),
#     которую эталон ГАСИТ в `CShellItemStore_InitControls@0xC28820` (`ida.c:479949`) и больше
#     никогда не показывает;
#   • `PremiumShop.BIN` -> ЭТА таблица — ЖИВАЯ навигация: полоса 58200 «Горячая новинка! | Новинка! |
#     Инструмент | Снаряжение | Волосы | Питомец | Элитный набор | Лавка мелочей» и её под-полосы
#     58201/58202/58203. Её показывает `itemBagDlgInit@0xC2C040`: `SetVisible(58200, 1u)` и четыре
#     цикла подписей `loc 10401+k / 10421+m / 10426+n / 10441+ii` (`ida.c:480019-480051`).
#
# ══════════════════════ ФОРМАТ (реверс `premiumShopLoadBin@0xC18F30`) ══════════════════════════
#     fread(hdr, 0x14, 1, f);            // ЗАГОЛОВОК 20 б: count @+4 (i32), магия "PST" @+0xC
#     cnt = *(i32*)(hdr+4);
#     fread(data, 0x44, cnt, f);         // записи по 68 б
#   Проверка на нашем файле: 48640 = 20 + 68*715, магия `PST\0`, count 715. Сходится побайтово.
#
# Раскладка полей записи и смысл категорий — в шапке `FAsdaPremiumShopBinRow`
# (`Public/UI/AsdaPremiumShopSubsystem.h`), снята с `ui_rebuildCategorizedItemList@0xC191F0`.
#
# `--verify` собирает файл ОБРАТНО из разобранных строк и сверяет md5 с исходником.
#
# Запуск:  Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_import_premium_shop_bin.py
#   без UE: python asda_import_premium_shop_bin.py --json-only
#   сверка: python asda_import_premium_shop_bin.py --verify
import os as _os
import struct as _struct
import sys as _sys

_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_dt_common as _DT
import asda_bintable as _BT     # ★ дедуп №2 В1 Т5: общий обход записей и C-строки

BIN = _os.path.join(_P.DATA, "NPC", "Shop", "PremiumShop.BIN")
OUT_DIR = _os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Data", "NPC")
OUT_JSON = _os.path.join(OUT_DIR, "PremiumShopBin.json")

PKG = "/Game/AsdaAssets/Data/NPC"
NAME = "DT_PremiumShopBin"
ROW_STRUCT = "/Script/AsdaReborn.AsdaPremiumShopBinRow"

STRIDE = 68
HDR = 20
TAG = "элитный магазин BIN"


def _read(path):
    with open(path, "rb") as f:
        return f.read()


def parse_bin(raw):
    """Возвращает список строк или None, если файл не бьётся с заголовком."""
    if len(raw) < HDR:
        return None
    cnt = _struct.unpack_from("<i", raw, 4)[0]
    if raw[12:15] != b"PST":
        return None
    if len(raw) != HDR + STRIDE * cnt:
        return None
    rows = []
    # ★ дедуп №2 В1 Т5: обход — общий `asda_bintable.iter_records` (длина файла уже сверена выше).
    for i, rec in _BT.iter_records(raw, STRIDE, count=cnt, base=HDR):
        item_id, cat, s1, s2 = _struct.unpack_from("<iiii", rec, 0)
        # ★ Байт `Note` — CP1251 (клиент русский); режем по первому нулю, как `sprintf("%s", …)`.
        note = _BT.cstr(rec, 25, 32, "cp1251", "replace")
        rows.append({
            "_row": i,
            "ItemId": item_id,
            "Category": cat,
            "Sub1": s1,
            "Sub2": s2,
            "Field16": rec[16],
            "Field20": _struct.unpack_from("<i", rec, 20)[0],
            "NewFlag": rec[24],
            "Note": note,
            "Field57": rec[57],
            # Сырая запись 1:1 — чтобы обратная сборка была побайтовой, а не «по полям».
            "DataHex": rec.hex(),
        })
    return rows


def rebuild_bin(rows):
    cnt = len(rows)
    out = bytearray(b"\x00" * HDR)
    _struct.pack_into("<i", out, 4, cnt)
    _struct.pack_into("<i", out, 8, 11)      # то же значение, что в исходном заголовке
    out[12:16] = b"PST\x00"
    for r in rows:
        out += bytes.fromhex(r["DataHex"])
    return bytes(out)


def verify(log):
    import hashlib
    raw = _read(BIN)
    rows = parse_bin(raw)
    if rows is None:
        log("!! файл не бьётся с шапкой: %s" % BIN)
        return False
    back = rebuild_bin(rows)
    same = hashlib.md5(back).hexdigest() == hashlib.md5(raw).hexdigest()
    from collections import Counter
    cats = Counter(r["Category"] for r in rows)
    flags = Counter(r["NewFlag"] for r in rows)
    log("записей %d (%d б), обратная сборка %s" % (len(rows), len(raw),
        "СОВПАЛА побайтово" if same else "РАСХОДИТСЯ"))
    log("категории(+4): %s | флаги новинки(+24): %s" % (dict(sorted(cats.items())), dict(sorted(flags.items()))))
    return same


TAIL_JSON = _os.path.join(OUT_DIR, "PremiumShopBin_tail.json")   # ★ 23.10.2026: ведомость дописываемых строк витрины
# ★ 26.10.2026: вторая ведомость хвоста — премиум-коробки «предмет ×N» (строит Tools/asda_premium_box_add.py по
#   Tools/data/premium_box_sets.json; рецепт Docs/Port/systems/PREMIUM_ITEM_ADD_FLOW.md). Дописывается ПОСЛЕ
#   ведомости маунтов теми же правилами; у каждого генератора свой файл — перегенерация одного не стирает строки другого.
# ★ 19.11.2026: третья ведомость — позиции витрины, добавленные наборами изменений админ-центра (AstoryAdminCenter,
#   domains/shopdata.py, позиция shop6000.add; пишет её только `asda_admin.py changeset client-apply`). Нет файла — ничего не дописывается.
TAIL_JSONS = (TAIL_JSON, _os.path.join(OUT_DIR, "PremiumShopBin_tail_boxes.json"), _os.path.join(OUT_DIR, "PremiumShopBin_tail_panel.json"))


def tail_rows(rows, log, tail_json=TAIL_JSON):
    """★ 23.10.2026 (маунты донора AsdaFableClassic, Reports/fable_mounts/FABLE_MOUNTS.md). Строки витрины,
    которых нет в PremiumShop.BIN основной сборки, — из ведомости PremiumShopBin_tail.json (её строит
    asda_fable_mounts_data.py). Запись 68 б собирается из первой записи архива той же категории/подкатегории
    (остальные байты — как у неё), поля ItemId/Category/Sub1/Sub2/NewFlag — из ведомости. Код, уже стоящий в
    витрине, — отказ всей ведомости (витрина остаётся 1:1 с архивом)."""
    if not _os.path.isfile(tail_json):
        return []
    import json
    led = json.load(open(tail_json, encoding="utf-8"))["rows"]
    have = {r["ItemId"] for r in rows}
    out = []
    for j, t in enumerate(led):
        tpl = next((r for r in rows if r["Category"] == t["Category"] and r["Sub1"] == t["Sub1"]), None)
        if tpl is None or t["ItemId"] in have:
            log("!! ведомость %s: строка %d (код %s) — %s; хвост НЕ дописан"
                % (tail_json, j, t["ItemId"], "нет образца категории" if tpl is None else "код уже в витрине"))
            return []
        have.add(t["ItemId"])
        b = bytearray(bytes.fromhex(tpl["DataHex"]))
        _struct.pack_into("<iiii", b, 0, t["ItemId"], t["Category"], t["Sub1"], t["Sub2"])
        b[24] = t["NewFlag"]
        out.append(dict(tpl, _row=len(rows) + j, ItemId=t["ItemId"], Category=t["Category"], Sub1=t["Sub1"],
                        Sub2=t["Sub2"], NewFlag=t["NewFlag"], DataHex=bytes(b).hex()))
    log("хвост из %s: +%d строк" % (_os.path.basename(tail_json), len(out)))
    return out


def dedup_rows(rows, log):
    """★ 01.11.2026 (item_dedup, этап 2): код-дубль витрины -> основной (общий модуль asda_item_dedup, карта
    Tools/data/item_dedup_map.json). Зовётся ПОСЛЕ ведомостей хвоста: их сторож «код уже в витрине» сверяет коды архива.
    Меняются ItemId и его копия DataHex+0 (обратная сборка .BIN идёт по DataHex). Строки не удаляются: если основной код
    уже стоит в той же категории, витрина покажет его дважды (5 строк категории 3 на карте 01.11 — см. отчёт item_dedup).
    ASDA_NO_DEDUP_MAP=1 — витрина как в PremiumShop.BIN; `--verify` (обратная сборка архива) перекодировку не делает.
    Возврат — число перекодированных строк."""
    import asda_item_dedup as _DD
    dmap = _DD.active_map()
    n_dd = 0
    for r in rows:
        nv = _DD.remap(r["ItemId"], dmap)
        if nv != r["ItemId"]:
            b = bytearray(bytes.fromhex(r["DataHex"]))
            _struct.pack_into("<i", b, 0, nv)
            r["ItemId"], r["DataHex"] = nv, bytes(b).hex()
            n_dd += 1
    # ★ 01.11.2026 (решение владельца по дедупу): после перекодировки поздняя ПОЛНАЯ копия строки (та же категория и код,
    #   все именованные поля, кроме номера строки, равны) снимается — витрина не показывает предмет дважды; первая строка
    #   остаётся. DataHex в сравнение не входит: у копий 141/142 он различается только мусорным байтом после NUL поля Note.
    seen, kept = set(), []
    for r in rows:
        k = tuple((f, r[f]) for f in sorted(r) if f not in ("_row", "Name", "DataHex"))
        if k in seen:
            continue
        seen.add(k)
        kept.append(r)
    n_drop = len(rows) - len(kept)
    rows[:] = kept
    log("item_dedup: перекодировано кодов витрины %d, снято полных копий строк %d" % (n_dd, n_drop))
    return n_dd


QTY_JSON = _os.path.join(OUT_DIR, "PremiumShopBin_card_qty.json")   # ★ 06.11.2026: ведомость штук (Tools/asda_premium_card_labels.py)


def card_qty(rows, log):
    """Столбец CardQty: штук в коробке, которую выдаёт карточка (0 — подписи нет). Категория 3 — причёски, код совпадает лишь номером."""
    qty = {}
    gift = {}
    if _os.path.isfile(QTY_JSON):
        with open(QTY_JSON, encoding="utf-8") as f:
            doc = __import__("json").load(f)
        qty, gift = doc["qty"], doc.get("gift", {})
    n = g = 0
    for r in rows:
        r["CardQty"] = qty.get(str(r["ItemId"]), 0) if r["Category"] != 3 else 0
        r["GiftCode"] = gift.get(str(r["ItemId"]), 0) if r["Category"] != 3 else 0   # ★ 06.11.2026: код выдачи для подарка (0 — сам код)
        n += 1 if r["CardQty"] else 0
        g += 1 if r["GiftCode"] else 0
    log("штуки на карточках: строк %d, код подарка: строк %d" % (n, g))


def _norm_cell(v):
    """Ячейка для сравнения ассета с прежним JSON: числа по значению, bool и строки без регистра."""
    if isinstance(v, bool):
        return str(v).lower()
    try:
        return float(v)
    except (TypeError, ValueError):
        return str(v).strip().lower()


def asset_matches_previous_json(unreal, log):
    """★ 19.11.2026 (ревизия 30 В-7, закон owner-asset-edits-never-lose): сторож ручной правки — строки ассета DT_PremiumShopBin
    == прежний OUT_JSON (до этого прогона) по всем общим полям. Нет ассета или прежнего JSON — сравнивать нечего (True).
    Образец — asda_import_npc_shop.asset_matches_previous_json."""
    import json
    dt = unreal.EditorAssetLibrary.load_asset(PKG + "/" + NAME)
    if not dt or not _os.path.isfile(OUT_JSON):
        return True
    prev = {str(r.get("Name", r.get("_row"))): r for r in json.load(open(OUT_JSON, encoding="utf-8"))}
    cur = {str(r["Name"]): r for r in json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(dt))}
    diff = sorted(k for k in set(prev) ^ set(cur))
    for k in set(prev) & set(cur):
        a, b = prev[k], cur[k]
        if any(_norm_cell(a[f]) != _norm_cell(b[f]) for f in a if f not in ("Name", "_row") and f in b):
            diff.append(k)
    if diff:
        log("СТОП: %s расходится с прежним %s (ручная правка?) — строки %s; ассет не трогаю" % (NAME, OUT_JSON, sorted(diff)[:10]))
        return False
    return True


def main(unreal=None):
    def log(m):
        if unreal:
            unreal.log_warning("[%s] %s" % (TAG, m))
        else:
            print("[%s] %s" % (TAG, m))

    if not _os.path.isfile(BIN):
        log("!! нет исходника %s" % BIN)
        return False
    rows = parse_bin(_read(BIN))
    if rows is None:
        log("!! исходник не разобран: %s" % BIN)
        return False
    log("записей %d" % len(rows))
    for tj in TAIL_JSONS:                 # ★ 26.10.2026: маунты, затем премиум-коробки (у каждой ведомости свой генератор)
        rows += tail_rows(rows, log, tj)
    dedup_rows(rows, log)                 # ★ 01.11.2026 item_dedup: после ведомостей хвоста
    card_qty(rows, log)                   # ★ 06.11.2026: штуки коробки на карточке (столбец CardQty)

    text = _DT.json_rows(rows, lambda r: str(r["_row"]))
    if unreal is not None and not asset_matches_previous_json(unreal, log):   # ★ 19.11 (ревизия 30 В-7): ДО записи JSON и DT
        log("ЗАВЕРШЕНО: импорт %s (СТОП: ручная правка)" % NAME)
        return False
    _os.makedirs(OUT_DIR, exist_ok=True)
    with open(OUT_JSON, "w", encoding="utf-8", newline="") as f:
        f.write(text)
    log("JSON: %s" % OUT_JSON)
    if unreal is None:
        return True
    ok = _DT.fill_dt(unreal, PKG, NAME, ROW_STRUCT, text, log, fmt="json",
                     missing_hint="Public/UI/AsdaPremiumShopSubsystem.h, FAsdaPremiumShopBinRow") is not None
    log("ЗАВЕРШЕНО: импорт %s (%s)" % (NAME, "ok" if ok else "ПРОВАЛ"))   # ★ 19.11 (ревизия 30 В-6): маркер ПОСЛЕ сохранения DT
    return ok


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__":
    if "--verify" in _sys.argv:
        verify(lambda m: print("[%s] %s" % (TAG, m)))
    else:
        main(None if "--json-only" in _sys.argv else __import__("unreal"))
