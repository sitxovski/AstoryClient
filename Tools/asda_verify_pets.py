# Copyright (c) 2026 @sitxovski (Discord)
# asda_verify_pets.py — приёмка ассетов ПИТОМЦЕВ: скелеталы, клипы 0/1/2, DT_PetModels, текстуры.
# Проверяет ровно те пути, которые грузит AAsdaPetActor.
# Запуск: Tools/run_headless.bat Tools\asda_verify_pets.py
import unreal
import os as _au_os, sys as _au_sys   # ★ волна 2 п. 7 (14.09.2026): реестр путей ассетов (Tools/asda_ue_paths.py)
_au_sys.path.insert(0, _au_os.path.dirname(_au_os.path.abspath(__file__)))
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)

PET = _U.G_MODELS_PET
DT = "/Game/AsdaAssets/Data/DT_PetModels"
AR = unreal.AssetRegistryHelpers.get_asset_registry()
EAL = unreal.EditorAssetLibrary


def log(m): unreal.log_warning("[pet-verify] %s" % m)


def main():
    meshes, anims, mics = {}, set(), {}
    for ad in AR.get_assets_by_path(unreal.Name(PET), recursive=True):
        cls, nm, pkg = str(ad.asset_class_path.asset_name), str(ad.asset_name), str(ad.package_name)
        if cls == "SkeletalMesh": meshes[nm] = pkg
        elif cls == "AnimSequence": anims.add(nm)
        elif cls == "MaterialInstanceConstant": mics[nm] = pkg
    log("скелеталов: %d" % len(meshes))

    full3 = [m for m in meshes if all("%sanim_%02d" % (m, t) in anims for t in (0, 1, 2))]
    log("с клипами 0/1/2: %d  (всего AnimSequence=%d)" % (len(full3), len(anims)))
    miss = sorted(set(meshes) - set(full3))
    if miss: log("  без полного набора: %s" % miss)

    # текстуры: у скольких моделей ВСЕ материалы имеют параметр Tex
    withtex = 0
    for m in meshes:
        mm = [n for n in mics if n.startswith(m)]
        if not mm: continue
        ok = True
        for n in mm:
            a = unreal.load_asset(mics[n])
            if not unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(a, "Tex"): ok = False
        if ok: withtex += 1
    log("моделей с назначенной текстурой во ВСЕХ материалах: %d/%d (материалов=%d)" % (withtex, len(meshes), len(mics)))

    if not EAL.does_asset_exist(DT):
        log("!! DT_PetModels НЕТ"); return
    dt = unreal.load_asset(DT)
    names = dt.get_row_names()
    log("DT_PetModels: строк=%d, RowStruct=%s" % (len(names), dt.get_editor_property("row_struct").get_name()))
    # сверка строк с реально импортированными ассетами
    bad_mesh = bad_clip = 0
    col = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, "ModelName")
    for rn, mn in zip(names, col):
        if mn not in meshes: bad_mesh += 1; log("  ! строка %s: меша '%s' нет" % (rn, mn)); continue
        for t in (0, 1, 2):
            if "%sanim_%02d" % (mn, t) not in anims: bad_clip += 1; log("  ! строка %s: нет клипа %d" % (rn, t))
    log("ИТОГ: строк без меша=%d, строк с нехваткой клипов=%d" % (bad_mesh, bad_clip))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
