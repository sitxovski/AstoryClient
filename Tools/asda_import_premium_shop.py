# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_premium_shop.py — ВИТРИНА ПРЕМИУМ-МАГАЗИНА (окно SHELLITEM_STORE_WIN 6000):
#   `Data\NPC\Shop\Premium.SHE` → /Game/AsdaAssets/Data/NPC/DT_PremiumShopList (11 строк).
#
# ══════════════════════ ПОЧЕМУ ЭТО НЕ ВТОРОЙ ИМПОРТЁР, А БЛИЗНЕЦ ═══════════════════════════════
# Окна **6000** и **6800** в оригинале обслуживает ОДИН класс `CShellItemStore`, и витрины у них
# в ОДНОМ формате: `shopPremiumMall_LoadShe@0xBD6460` и `cShopLoadFreeShop@0xCAB270` отличаются
# только именем файла. Доказательство «один объект — две витрины»:
#   `cShopDialogResetState@0xC32070` (`AsdaStory_ida.c:481412-481413`) на ОДНОМ `this` подряд зовёт
#   `shopPremiumMall_LoadShe` (Premium.she) и `cShopLoadFreeShop` (FreeShop.she);
#   `CShellItemStore_InitControls@0xC28820` на том же `this` привязывает и 6000 (`m_pStoreWin`,
#   `ida.c:479294`), и 6800 (`m_pStore2Win`, `ida.c:479783`).
# ⇒ РАЗБОР ФОРМАТА, СТРУКТУРА СТРОКИ И РАНТАЙМ-ЗАГРУЗЧИК — ОБЩИЕ:
#   • строка таблицы — тот же `FAsdaEquipShopListRow` (второй структуры не заводим);
#   • рантайм читает обе таблицы одной точкой `UAsdaEquipShopSubsystem::LoadSheListsFromTable`.
# Здесь отличается ровно одно: ИСХОДНЫЙ ФАЙЛ и ИМЯ ВЫХОДНОЙ ТАБЛИЦЫ.
#
# ══════════════════════ ФОРМАТ `.she` (реверс загрузчика, НЕ догадка) ═══════════════════════════
#     fread(counts, 2, 11, f);                       // ОДИННАДЦАТЬ u16 — длины списков
#     for (k = 0; k < 11; ++k)                       // затем 11 массивов подряд, без выравнивания
#         fread(list[k], 4, counts[k], f);           // i32 itemCode
# размер = 22 + 4*sum(counts). На `Premium.SHE` сходится побайтово:
#   482 = 22 + 4*115, счётчики (0,0,0,0,63,9,9,9,7,9,9).
#
# ★ ПОРЯДОК КОДОВ ВНУТРИ СПИСКА ЗНАЧИМ: `page = i / ItemsPerPage`, `slot = i % ItemsPerPage`.
#   Сортировать список нельзя — страницы перемешаются.
#
# ★ ТРИ ПРОИЗВОДНЫХ СПИСКА (11 «Воин», 12 «Лучник», 13 «Маг») В ТАБЛИЦУ НЕ КЛАДЁМ: их строит
#   рантайм из списка №4 по `itemType == 9` и `itemSubType` — то есть по данным item-БД, а не по
#   файлу витрины. Заморозить их в таблице значило бы, что правка `item_list` перестала бы на них
#   влиять.
#
# `--verify` собирает байты файла ОБРАТНО из разобранных списков и сверяет md5 с исходником.
#
# Запуск:  Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_import_premium_shop.py
#   без UE: python asda_import_premium_shop.py --json-only
#   сверка: python asda_import_premium_shop.py --verify
import os as _os
import sys as _sys

_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_dt_common as _DT
# ★ ОБЩАЯ ТОЧКА: разбор и обратная сборка `.she` живут в импортёре витрины 6800 — формат один.
import asda_import_equip_shop as _EQ

# ⚠ Имя файла в клиенте — ВЕРХНИМ регистром (`Premium.SHE`); на NTFS это неважно, но берём как есть.
SHE = _os.path.join(_P.DATA, "NPC", "Shop", "Premium.SHE")
OUT_DIR = _os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Data", "NPC")
OUT_JSON = _os.path.join(OUT_DIR, "PremiumShopList.json")

PKG = "/Game/AsdaAssets/Data/NPC"
NAME = "DT_PremiumShopList"
ROW_STRUCT = "/Script/AsdaReborn.AsdaEquipShopListRow"   # ★ та же структура, что у витрины 6800

TAG = "премиум-магазин"


def _read(path):
    with open(path, "rb") as f:
        return f.read()


def verify(log):
    import hashlib
    raw = _read(SHE)
    rows = _EQ.parse_she(raw)
    if rows is None:
        log("!! файл не бьётся с шапкой: %s" % SHE)
        return False
    back = _EQ.rebuild_she(rows)
    same = hashlib.md5(back).hexdigest() == hashlib.md5(raw).hexdigest()
    log("списков %d, товаров всего %d, обратная сборка %s (%d б)"
        % (len(rows), sum(len(r["ItemCodes"]) for r in rows),
           "СОВПАЛА побайтово" if same else "РАСХОДИТСЯ", len(raw)))
    log("длины списков: %s" % ", ".join(str(len(r["ItemCodes"])) for r in rows))
    return same


def main(unreal=None):
    def log(m):
        if unreal:
            unreal.log_warning("[%s] %s" % (TAG, m))
        else:
            print("[%s] %s" % (TAG, m))

    if not _os.path.isfile(SHE):
        log("!! нет исходника %s" % SHE)
        return False
    rows = _EQ.parse_she(_read(SHE))
    if rows is None:
        log("!! исходник не разобран: %s" % SHE)
        return False
    log("списков %d, товаров всего %d" % (len(rows), sum(len(r["ItemCodes"]) for r in rows)))

    text = _DT.json_rows(rows, lambda r: str(r["ListIndex"]))
    _os.makedirs(OUT_DIR, exist_ok=True)
    with open(OUT_JSON, "w", encoding="utf-8", newline="") as f:
        f.write(text)
    log("JSON: %s" % OUT_JSON)
    if unreal is None:
        return True
    return _DT.fill_dt(unreal, PKG, NAME, ROW_STRUCT, text, log, fmt="json",
                       missing_hint="Public/UI/AsdaEquipShopSubsystem.h, FAsdaEquipShopListRow") is not None


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__":
    if "--verify" in _sys.argv:
        verify(lambda m: print("[%s] %s" % (TAG, m)))
    elif "--json-only" in _sys.argv:
        main(None)
    else:
        import unreal as _u
        main(_u)
