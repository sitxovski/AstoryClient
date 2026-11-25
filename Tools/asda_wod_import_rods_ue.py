# Copyright (c) 2026 @sitxovski (Discord)
# asda_wod_import_rods_ue.py — headless UE: импорт ТОЛЬКО скелеталов удочек донора WorldOfDreams (ch1_n_00_fird_03…10_00_0_00,
# экспорт `asda_wod_records.py rods-export`) в /Game/AsdaAssets/Models/Skeletal/Fishing/<модель> — путь 1:1 fishing_import_models.py
# (MI.import_batch, MIC → M_AsdaObjectLit + Tex по имени материала, без Nanite), но материалы и сохранение — только в папках
# этих моделей (штатный импортёр перепривязывает MIC всей папки Fishing — чужие ассеты не трогаем).
# Запуск: python run_headless_safe.py asda_wod_import_rods_ue.py --marker "ЗАВЕРШЕНО: wod-удочки" --timeout 1800
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

RODS = ["ch1_n_00_fird_%02d_00_0_00" % i for i in range(3, 11)]   # удочки донора (sys_petfish.md §2)


def main():
    import unreal
    import asda_ue_paths as _U
    import asda_model_import_ue as MI
    eal, mel = unreal.EditorAssetLibrary, unreal.MaterialEditingLibrary
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    raw = os.path.join(_U.RAW_MODELS, "Fishing")
    dest = _U.G_MODELS_SKELETAL + "/Fishing"
    todo = [os.path.join(raw, m + ".glb") for m in RODS
            if os.path.isfile(os.path.join(raw, m + ".glb")) and not (eal.does_directory_exist(dest + "/" + m) and eal.list_assets(dest + "/" + m))]
    unreal.log("wod-удочки: к импорту %d из %d" % (len(todo), len(RODS)))
    if todo:
        MI.import_batch(todo, dest)
    tex = MI.tex_index("all_last", root=_U.G_TEXTURES)
    master = unreal.load_asset(_U.M_OBJECT_LIT)
    nomat = meshes = anims = 0
    for m in RODS:
        d = dest + "/" + m
        for ad in ar.get_assets_by_path(unreal.Name(d), recursive=True):
            cls = str(ad.asset_class_path.asset_name)
            if cls == "MaterialInstanceConstant":
                a = unreal.load_asset(str(ad.package_name))
                tp = MI.find_tex(str(ad.asset_name), tex, "soulguard")
                mel.set_material_instance_parent(a, master)
                if tp:
                    mel.set_material_instance_texture_parameter_value(a, "Tex", unreal.load_asset(tp))
                else:
                    nomat += 1
                    unreal.log_warning("wod-удочки: нет текстуры для %s" % ad.asset_name)
                mel.set_material_instance_scalar_parameter_value(a, "Ambient", 1.0)
            elif cls == "SkeletalMesh":
                meshes += 1
            elif cls == "AnimSequence":
                anims += 1
        MI.disable_nanite(d, ("SkeletalMesh",))
        eal.save_directory(d, only_if_is_dirty=False, recursive=True)
        unreal.log("ASDA_WRITES_ASSET: %s/SkeletalMeshes/%s" % (d, m))
    unreal.log("wod-удочки: скелеталов %d, клипов %d, материалов без текстуры %d" % (meshes, anims, nomat))
    unreal.log("ЗАВЕРШЕНО: wod-удочки" if meshes == len(RODS) and anims == 8 * len(RODS) and not nomat else "ОШИБКИ: wod-удочки")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
