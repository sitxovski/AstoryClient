# Copyright (c) 2026 @sitxovski (Discord)
# asda_wod_import_parts_ue.py — headless UE: импорт ТОЛЬКО скиннутых частей предметов донора WorldOfDreams
# (план `data/wod/wod_models.json`, action new/wod, kind skin) из RawAssets/Models/Skeletal/EquipParts
# в /Game/AsdaAssets/Models/Skeletal/EquipParts/<часть> на ОБЩИЙ скелет cre_cremmy (как import_equip_parts).
# Функции штатного import_equip_parts переиспользуются (make_task / post_process / verify_skeleton), но:
#   • пол берётся из плана (у mantle_* пола в имени нет → мужской скелет; у gender_of прочие уйдут на женский);
#   • сохраняются ТОЛЬКО папки новых частей (штатный main пересохраняет всю EquipParts — правки владельца не терять);
#   • у копий `wod_<имя>` (у нас одноимённая часть с другой геометрией) текстура MIC по умолчанию ищется по имени
#     без префикса, если `wod_<имя>` нет (рантайм всё равно ставит TexMale/TexFemale предмета, AsdaCharAssembly ApplyItemTexture).
# Уже импортированные части пропускаются. Текстуры предметов (TexMale/TexFemale) импортирует asda_import_textures.py.
# Запуск: python run_headless_safe.py asda_wod_import_parts_ue.py --marker "ЗАВЕРШЕНО: wod-части" --timeout 5400
import os, sys, json, importlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

PLAN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "wod", "wod_models.json")   # план моделей
PREFIX = "wod_"                                                                                     # префикс копий донора


def main():
    import unreal
    E = importlib.import_module("import_equip_parts")   # модуль без побочных эффектов при импорте
    eal = unreal.EditorAssetLibrary
    plan = json.load(open(PLAN, encoding="utf-8"))
    parts = [(p["out"], "f" if p["gender"] == "f" else "m") for p in plan["meshes"]
             if p["kind"] == "skin" and p["action"] in ("new", "wod")]
    todo = [(n, g) for n, g in parts if os.path.exists(os.path.join(E.RAW, n + ".glb"))
            and not (eal.does_directory_exist(E.DEST + "/" + n) and eal.list_assets(E.DEST + "/" + n))]
    nofile = [n for n, g in parts if not os.path.exists(os.path.join(E.RAW, n + ".glb"))]
    E.log("wod: частей в плане %d, к импорту %d, нет .glb %d %s" % (len(parts), len(todo), len(nofile), nofile[:10]))
    skel = {g: unreal.load_asset(E.SKEL[g]) for g in ("m", "f")}
    master = unreal.load_asset(E.MASTER)
    nok = mats = 0
    bad = []
    for i in range(0, len(todo), E.CHUNK):
        chunk = todo[i:i + E.CHUNK]
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(
            [E.make_task(os.path.join(E.RAW, n + ".glb"), n, skel[g]) for n, g in chunk])
        tex = E.tex_index()
        for n, g in chunk:
            key = n if E.find_tex(n, tex) or not n.startswith(PREFIX) else n[len(PREFIX):]
            if key != n:   # копия донора: MIC по умолчанию — текстура исходного имени
                tex = dict(tex)
                tp = E.find_tex(key, tex)
                if tp:
                    tex[n.lower()] = tp
            if E.post_process(n, tex, master):
                mats += 1
            ok, strays = E.verify_skeleton(n, E.SKEL[g])
            if ok:
                nok += 1
            else:
                bad.append(n)
            for s in strays:   # левый скелет внутри папки части (override даёт общий — не должно быть)
                if s.startswith(E.DEST + "/" + n) and eal.does_asset_exist(s):
                    eal.delete_asset(s)
            eal.save_directory(E.DEST + "/" + n, only_if_is_dirty=False, recursive=True)
            unreal.log("ASDA_WRITES_ASSET: %s/%s/%s" % (E.DEST, n, n))
        unreal.SystemLibrary.collect_garbage()
        E.log("wod: импортировано %d/%d (скелет-ok=%d, tex-mat=%d)" % (min(i + E.CHUNK, len(todo)), len(todo), nok, mats))
    E.log("wod: ИТОГ импортировано=%d, материалов(с tex)=%d, общий-скелет-ok=%d/%d, ошибок скелета %d %s"
          % (len(todo), mats, nok, len(todo), len(bad), bad[:20]))
    unreal.log("ЗАВЕРШЕНО: wod-части" if not bad else "ОШИБКИ: wod-части")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
