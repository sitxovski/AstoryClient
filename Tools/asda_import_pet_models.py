# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_pet_models.py — импорт скиннутых моделей ПИТОМЦЕВ (.glb) в проект + материалы.
# По образцу asda_import_skinned_models.py, но ТОЛЬКО пет-ветка — прочие ассеты не трогает.
# Источник : RawAssets/Models/Pet/*.glb          (asda_build_pet_models.py)
# Приёмник : /Game/AsdaAssets/Models/Skeletal/Pet/<model>/SkeletalMeshes/<model>[anim_NN]
#            — ровно те пути, что грузит AAsdaPetActor (Source/AsdaReborn/Private/World/AsdaPetActor.cpp).
# Запуск   : Tools/run_headless.bat Tools\asda_import_pet_models.py
import os, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_ue_paths as _U

import asda_paths as _P   # ★ гигиена 15.09.2026 (§4 п.2): корни путей — Tools/asda_paths.py (ASDA_ROOT и др.)
RAW = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Models", "Pet")
DEST = _U.G_MODELS_PET
TEX_ROOT = _U.G_TEXTURES
MASTER = _U.M_OBJECT_LIT
# ★ PET_REIMPORT=1 (переменная окружения) — ПОЛНЫЙ ПЕРЕИМПОРТ: сначала удалить папку Pet, потом импорт.
#   Нужен после смены скин-пайплайна: у мешей меняется СОСТАВ КОСТЕЙ (prune_unskinned), а Interchange,
#   переиспользуя старый Skeleton с другим числом костей, либо падает, либо оставляет старую привязку.
REIMPORT = os.environ.get("PET_REIMPORT") == "1"
INCREMENTAL = not REIMPORT
CHUNK = 6                                   # мелкий чанк: устойчиво по RDP/NullRHI

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()


def log(m): unreal.log_warning("[pet-import] %s" % m)


# ★ ВОЛНА 2 (14.09.2026, дефект E11): индекс/поиск текстур, подвязка MIC, импорт и Nanite — из ЕДИНОЙ
#   библиотеки asda_model_import_ue. Пресеты повторяют прежние копии этого файла (правило дублей имён,
#   нормализация ключа) — найденная текстура прежняя; добавлено только снятие меток `_msk`/`_blend` там, где
#   раньше текстура не находилась (решение координатора, `Reports/dedup_audit/wave2_import.md` §2, §8).
import asda_model_import_ue as MI

def tex_index():
    """Индекс Texture2D (пресет «all_last» библиотеки = прежняя копия этого файла)."""
    return MI.tex_index("all_last", root=TEX_ROOT)


def find_tex(name, tex):
    """Текстура для слота/MIC (пресет «plain» библиотеки = прежняя копия этого файла)."""
    return MI.find_tex(name, tex, "plain")


def import_batch(paths, dest):
    """Импорт пачкой (replace_existing=True) — asda_model_import_ue.import_batch."""
    return MI.import_batch(paths, dest)


def main():
    if not os.path.isdir(RAW):
        log("нет папки %s — сначала python asda_build_pet_models.py" % RAW); return
    if REIMPORT and EAL.does_directory_exist(DEST):
        log("ПЕРЕИМПОРТ: удаляю %s (старые скелеты/меши несовместимы по костям)" % DEST)
        EAL.delete_directory(DEST)
    glbs = sorted(f for f in os.listdir(RAW) if f.lower().endswith(".glb"))
    todo, skip = [], 0
    for fn in glbs:
        mdir = "%s/%s" % (DEST, os.path.splitext(fn)[0])
        if INCREMENTAL and EAL.does_directory_exist(mdir) and EAL.list_assets(mdir):
            skip += 1
        else:
            todo.append(os.path.join(RAW, fn))
    log("моделей=%d, к импорту=%d, пропуск=%d" % (len(glbs), len(todo), skip))

    for i in range(0, len(todo), CHUNK):
        import_batch(todo[i:i + CHUNK], DEST)
        log("  импортировано %d/%d" % (min(i + CHUNK, len(todo)), len(todo)))
        EAL.save_directory(DEST, only_if_is_dirty=True, recursive=True)
        try: unreal.SystemLibrary.collect_garbage()
        except Exception: pass

    # Материалы: мастер M_AsdaObjectLit + параметр Tex по имени материала (= имя .tex модели).
    tex = tex_index()
    log("текстур в индексе: %d" % len(tex))
    master = unreal.load_asset(MASTER)
    mats = nomat = 0
    for ad in AR.get_assets_by_path(unreal.Name(DEST), recursive=True):
        cls = str(ad.asset_class_path.asset_name)
        if cls == "MaterialInstanceConstant":
            a = unreal.load_asset(str(ad.package_name))
            tp = find_tex(str(ad.asset_name), tex)
            MEL.set_material_instance_parent(a, master)
            if tp: MEL.set_material_instance_texture_parameter_value(a, "Tex", unreal.load_asset(tp))
            else:  nomat += 1; log("  ! текстура не найдена для материала %s" % ad.asset_name)
            MEL.set_material_instance_scalar_parameter_value(a, "Ambient", 1.0)
            mats += 1
        elif cls == "SkeletalMesh":
            m = unreal.load_asset(str(ad.package_name))
            try:
                ns = m.get_editor_property("nanite_settings")
                if ns and ns.get_editor_property("enabled"):
                    ns.set_editor_property("enabled", False); m.set_editor_property("nanite_settings", ns)
            except Exception: pass
    EAL.save_directory(DEST, only_if_is_dirty=True, recursive=True)
    log("ГОТОВО: импортировано=%d, пропущено=%d, материалов=%d (без текстуры=%d)" % (len(todo), skip, mats, nomat))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
