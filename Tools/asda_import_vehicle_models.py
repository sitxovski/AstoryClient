# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_vehicle_models.py — импорт СКЕЛЕТАЛЬНОГО транспорта (маунтов) в проект.
# Источник: RawAssets/Models/Vehicle/itm1_n_96_vehicle_00NN.glb (1 SkeletalMesh + клипы anim_00/01/02).
# Назначение: /Game/AsdaAssets/Models/Skeletal/Vehicle/<model>/...
# Материал: мастер M_AsdaObjectLit, параметр Tex := <model>_t  (эталон SetParts_RidingSystem@0x9EC900 п.7 —
#   ОДНА текстура <ModelEntry>_t.TEX копируется во ВСЕ подсетки, встроенное в .jbm имя игнорируется).
# Слоты анимации (эталон п.9): anim_00 = idle, anim_02 = движение, anim_01 в игре не выбирается.
# Существующие СТАТИЧЕСКИЕ /Game/AsdaAssets/Models/Char/itm1_n_96_vehicle_* НЕ трогаются.
# Запуск: run_headless.bat asda_import_vehicle_models.py
import os, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_ue_paths as _U

HERE = os.path.dirname(os.path.abspath(__file__))
RAW  = os.path.abspath(os.path.join(HERE, "..", "RawAssets", "Models", "Vehicle"))
DEST = _U.G_MODELS_VEHICLE
TEX_ROOT = _U.G_TEXTURES
MASTER   = _U.M_OBJECT_LIT
# ★ VEHICLE_REIMPORT=1 (переменная окружения) — ПОЛНЫЙ ПЕРЕИМПОРТ: сначала удалить папку Vehicle,
#   потом импорт. Обязателен после смены скин-пайплайна (двух-позиционный bind, 2026-07-30): у мешей
#   МЕНЯЕТСЯ СОСТАВ КОСТЕЙ (prune_unskinned отсекает не-скиннутые хелперы), а Interchange, переиспользуя
#   старый Skeleton с другим числом костей, молча оставляет старые ассеты. Ровно та же грабля была у петов.
REIMPORT = os.environ.get("VEHICLE_REIMPORT") == "1"
INCREMENTAL = not REIMPORT  # уже импортированные модели пропускать
CHUNK = 8                   # маленький чанк — меньше всплеск памяти/GPU

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
AR  = unreal.AssetRegistryHelpers.get_asset_registry()
def log(m): unreal.log("[vehicle] " + str(m))


# ★ ВОЛНА 2 (14.09.2026, дефект E11): индекс/поиск текстур, подвязка MIC, импорт и Nanite — из ЕДИНОЙ
#   библиотеки asda_model_import_ue. Пресеты повторяют прежние копии этого файла (правило дублей имён,
#   нормализация ключа) — найденная текстура прежняя; добавлено только снятие меток `_msk`/`_blend` там, где
#   раньше текстура не находилась (решение координатора, `Reports/dedup_audit/wave2_import.md` §2, §8).
import asda_model_import_ue as MI

def tex_index():
    """Индекс Texture2D (пресет «all_last» библиотеки = прежняя копия этого файла)."""
    return MI.tex_index("all_last", root=TEX_ROOT)


def import_batch(paths, dest):
    """Импорт пачкой (replace_existing=True) — asda_model_import_ue.import_batch."""
    return MI.import_batch(paths, dest)


def wire_model_materials(model, tex, master):
    """Все MIC модели → мастер + Tex = <model>_t (как в оригинале)."""
    tp = tex.get((model + "_t").lower())
    n = 0
    for ad in AR.get_assets_by_path(unreal.Name("%s/%s" % (DEST, model)), recursive=True):
        if str(ad.asset_class_path.asset_name) != "MaterialInstanceConstant":
            continue
        a = unreal.load_asset(str(ad.package_name))
        MEL.set_material_instance_parent(a, master)
        if tp:
            MEL.set_material_instance_texture_parameter_value(a, "Tex", unreal.load_asset(tp))
        MEL.set_material_instance_scalar_parameter_value(a, "Ambient", 1.0)
        n += 1
    if not tp:
        log("  !! нет текстуры %s_t" % model)
    return n


def disable_nanite(model):
    """Снять Nanite с мешей модели — asda_model_import_ue.disable_nanite."""
    MI.disable_nanite("%s/%s" % (DEST, model))


def main():
    if not os.path.isdir(RAW):
        log("НЕТ папки %s — сначала: python asda_build_skinned_models.py vehicle" % RAW); return
    if REIMPORT and EAL.does_directory_exist(DEST):
        log("ПЕРЕИМПОРТ: удаляю %s (старые скелеты/меши несовместимы по составу костей)" % DEST)
        EAL.delete_directory(DEST)
    master = unreal.load_asset(MASTER)
    tex = tex_index()
    log("текстур в индексе: %d" % len(tex))

    glbs = sorted(f for f in os.listdir(RAW) if f.lower().endswith(".glb"))
    todo, skip = [], 0
    for fn in glbs:
        model = os.path.splitext(fn)[0]
        mdir = "%s/%s" % (DEST, model)
        if INCREMENTAL and EAL.does_directory_exist(mdir) and EAL.list_assets(mdir):
            skip += 1
        else:
            todo.append(os.path.join(RAW, fn))
    log("к импорту: %d, пропуск (уже есть): %d" % (len(todo), skip))

    done = 0
    for i in range(0, len(todo), CHUNK):
        chunk = todo[i:i + CHUNK]
        import_batch(chunk, DEST)
        done += len(chunk)
        log("  импортировано %d/%d" % (done, len(todo)))
        EAL.save_directory(DEST, only_if_is_dirty=True, recursive=True)
        try: unreal.SystemLibrary.collect_garbage()
        except Exception: pass

    mats = 0
    for fn in glbs:
        model = os.path.splitext(fn)[0]
        if EAL.does_directory_exist("%s/%s" % (DEST, model)):
            mats += wire_model_materials(model, tex, master)
            disable_nanite(model)
    EAL.save_directory(DEST, only_if_is_dirty=True, recursive=True)
    log("ГОТОВО: импортировано=%d, пропущено=%d, материалов настроено=%d" % (done, skip, mats))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
