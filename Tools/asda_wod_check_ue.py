# Copyright (c) 2026 @sitxovski (Discord)
# asda_wod_check_ue.py — headless UE, ТОЛЬКО ЧТЕНИЕ: проверка залитых предметов донора WorldOfDreams по самим ассетам
# (не по CSV): строки DT_item_list / DT_item_img_table / DT_item_tooltip / DT_pricetable / DT_packagetable и EN-оверлеев
# для кодов data/wod/wod_codes_final.csv, существование мешей (EquipParts / Char / Skeletal/Fishing), текстур и атласов иконок.
# Ничего не сохраняет. Запуск: python run_headless_safe.py asda_wod_check_ue.py --marker "ЗАВЕРШЕНО: wod-проверка" --timeout 1800
import csv
import io
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
FINAL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "wod", "wod_codes_final.csv")
G = "/Game/AsdaAssets"


def dt_rows(unreal, path):
    """DT → {имя строки: словарь колонок} через экспорт в CSV-строку."""
    dt = unreal.load_asset(path)
    txt = unreal.DataTableFunctionLibrary.export_data_table_to_csv_string(dt)
    rows = list(csv.reader(io.StringIO(txt)))
    head = rows[0]
    return {r[0]: dict(zip(head, r)) for r in rows[1:]}


def main():
    import unreal
    eal = unreal.EditorAssetLibrary
    final = list(csv.DictReader(open(FINAL, encoding="utf-8")))
    codes = {int(r["our_code"]): r for r in final}
    il = dt_rows(unreal, G + "/Data/ItemDB/DT_item_list")
    im = dt_rows(unreal, G + "/Data/ItemDB/DT_item_img_table")
    tt = dt_rows(unreal, G + "/Data/ItemDB/DT_item_tooltip")
    pt = dt_rows(unreal, G + "/Data/ItemDB/DT_pricetable")
    pk = dt_rows(unreal, G + "/Data/ItemDB/DT_packagetable")
    en_il = dt_rows(unreal, G + "/Text/L10N/en/DT_Loc_item_list_en")
    en_tt = dt_rows(unreal, G + "/Text/L10N/en/DT_Loc_item_tooltip_en")
    tips = {int(r["ItemCode"]): (k, r) for k, r in tt.items() if r.get("ItemCode", "").lstrip("-").isdigit()}
    prices = {int(r["ItemCode"]) for r in pt.values() if r.get("ItemCode", "").lstrip("-").isdigit()}
    errs, seen_assets = [], {}

    def exists(p):
        if p not in seen_assets:
            seen_assets[p] = eal.does_asset_exist(p)
        return seen_assets[p]

    for code, fr in sorted(codes.items()):
        row = str(code - 20551)
        r = il.get(row)
        if not r or int(r["ItemCode"]) != code:
            errs.append("%d: нет строки DT_item_list %s" % (code, row)); continue
        if not r.get("ItemName") or row + ".ItemName" not in en_il:
            errs.append("%d: имя RU/EN пусто" % code)
        g = im.get(row)
        if not g:
            errs.append("%d: нет img %s" % (code, row)); continue
        atlas = g["IconAtlasMale"]
        if not exists("%s/Textures/JUI/%s" % (G, atlas)):
            errs.append("%d: нет атласа %s" % (code, atlas))
        t, s = int(r["ItemType"]), int(r["ItemSubType"])
        for mk, tk in (("MeshMale", "TexMale"), ("MeshFemale", "TexFemale")):
            m = g[mk]
            if not m or t == 11:
                continue
            cands = ["%s/Models/Skeletal/EquipParts/%s/%s" % (G, m, m), "%s/Models/Char/%s/%s" % (G, m, m),
                     "%s/Models/Char/%s/StaticMeshes/%s" % (G, m, m)]
            if not any(exists(c) for c in cands):
                errs.append("%d: нет меша %s" % (code, m))
            if t == 9 and s == 10 and not exists("%s/Models/Skeletal/Fishing/%s_00/SkeletalMeshes/%s_00" % (G, m, m)):
                errs.append("%d: нет скелетала удочки %s_00" % (code, m))
        if code not in tips:
            errs.append("%d: нет подсказки" % code)
        elif tips[code][0] + ".Description" not in en_tt:
            errs.append("%d: нет EN-подсказки" % code)
        if fr["cat"] in ("gear",) and code not in prices:
            errs.append("%d: нет pricetable" % code)
        if fr["cat"] == "box":
            p = pk.get(fr["idx"])
            if not p or int(r["DurationOrEnchant"]) != int(fr["idx"]):
                errs.append("%d: пакет %s не найден/не совпал" % (code, fr["idx"]))
    unreal.log("wod-проверка: кодов %d, ассетов проверено %d, ошибок %d" % (len(codes), len(seen_assets), len(errs)))
    for e in errs[:60]:
        unreal.log_warning("wod-проверка: " + e)
    unreal.log("ЗАВЕРШЕНО: wod-проверка" if not errs else "ОШИБКИ: wod-проверка")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
