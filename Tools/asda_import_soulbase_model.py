# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_soulbase_model.py — импорт скиннутой модели ДУХА ДРУГА (слот Дружбы 0) в проект.
# По образцу asda_import_soulguard_models.py; трогает ТОЛЬКО ветку Skeletal/SoulBase.
# Источник : RawAssets/Models/SoulBase/*.glb        (asda_build_soulbase_model.py)
# Приёмник : /Game/AsdaAssets/Models/Skeletal/SoulBase/<модель>/SkeletalMeshes/<модель>[<суффикс клипа>]
#            — ровно те пути, что грузит AAsdaSoulBaseActor
#            (Source/AsdaReborn/Private/World/AsdaSoulBaseActor.cpp).
# Запуск   : Tools/run_headless.bat Tools\asda_import_soulguard_models.py
#
# ⚠ СОСТАВ КОСТЕЙ (грабля 3 из [[skin-pipeline-fixed-path]]): prune_unskinned меняет число костей
#   (46→25 и т.п.). Если Skeleton уже существует со старым составом, Interchange молча оставит старую
#   привязку. Переменная окружения SOULBASE_REIMPORT=1 удаляет каталог целиком перед импортом.
#
# ★ ПРАВИЛО tools-no-import-side-effects: работа только через main(), вызываемый в конце ЯВНО
#   (как в остальных импортёрах ветки, которые headless-раннер запускает файлом).
import os, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_ue_paths as _U

RAW = os.path.join(_U.RAW_MODELS, "SoulBase")
DEST = _U.G_MODELS_SKELETAL + "/SoulBase"
TEX_ROOT = _U.G_TEXTURES
import asda_material_policy as _POL
# Подсетка духа: meshType 0x100 = tRigid, класс CHAR. Тело непрозрачное, альфа текстуры — маска блика (`asda_mesh_render_class`).
MASTER = _POL.master_for_class("CHAR")
REIMPORT = os.environ.get("SOULBASE_REIMPORT") == "1"
INCREMENTAL = not REIMPORT
CHUNK = 3                                   # мелкий чанк: у хранителей до 11 клипов на модель

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()


def log(m): unreal.log_warning("[soulbase-import] %s" % m)


# ★ ВОЛНА 2 (14.09.2026, дефект E11): индекс/поиск текстур, подвязка MIC, импорт и Nanite — из ЕДИНОЙ
#   библиотеки asda_model_import_ue. Пресеты повторяют прежние копии этого файла (правило дублей имён,
#   нормализация ключа) — найденная текстура прежняя; добавлено только снятие меток `_msk`/`_blend` там, где
#   раньше текстура не находилась (решение координатора, `Reports/dedup_audit/wave2_import.md` §2, §8).
import asda_model_import_ue as MI

def tex_index():
    """Индекс Texture2D (пресет «all_last» библиотеки = прежняя копия этого файла)."""
    return MI.tex_index("all_last", root=TEX_ROOT)


def find_tex(name, tex):
    """Текстура для слота/MIC (пресет «soulguard» библиотеки = прежняя копия этого файла)."""
    return MI.find_tex(name, tex, "soulguard")


def import_batch(paths, dest):
    """Импорт пачкой (replace_existing=True) — asda_model_import_ue.import_batch."""
    return MI.import_batch(paths, dest)


def main():
    if not os.path.isdir(RAW):
        log("нет папки %s — сначала python asda_build_soulbase_model.py" % RAW); return
    if REIMPORT and EAL.does_directory_exist(DEST):
        log("ПЕРЕИМПОРТ: удаляю %s (состав костей меняется — старые скелеты несовместимы)" % DEST)
        EAL.delete_directory(DEST)
    glbs = sorted(f for f in os.listdir(RAW) if f.lower().endswith(".glb"))
    todo, skip = [], 0
    for fn in glbs:
        mdir = "%s/%s" % (DEST, os.path.splitext(fn)[0])
        if INCREMENTAL and EAL.does_directory_exist(mdir) and EAL.list_assets(mdir):
            skip += 1
        else:
            todo.append(os.path.join(RAW, fn))
    log("моделей=%d, к импорту=%d, пропуск=%d (переимпорт=%d)" % (len(glbs), len(todo), skip, 1 if REIMPORT else 0))

    for i in range(0, len(todo), CHUNK):
        import_batch(todo[i:i + CHUNK], DEST)
        log("  импортировано %d/%d" % (min(i + CHUNK, len(todo)), len(todo)))
        EAL.save_directory(DEST, only_if_is_dirty=True, recursive=True)
        try: unreal.SystemLibrary.collect_garbage()
        except Exception: pass

    # Материалы: мастер класса CHAR + параметр Tex по имени материала (= имя .tex модели).
    tex = tex_index()
    log("текстур в индексе: %d" % len(tex))
    master = unreal.load_asset(MASTER)
    mats = nomat = meshes = anims = 0
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
            meshes += 1
            m = unreal.load_asset(str(ad.package_name))
            try:
                ns = m.get_editor_property("nanite_settings")
                if ns and ns.get_editor_property("enabled"):
                    ns.set_editor_property("enabled", False); m.set_editor_property("nanite_settings", ns)
            except Exception: pass
        elif cls == "AnimSequence":
            anims += 1
    EAL.save_directory(DEST, only_if_is_dirty=True, recursive=True)
    log("ГОТОВО: импортировано=%d, пропущено=%d, скелеталов=%d, клипов=%d, материалов=%d (без текстуры=%d)"
        % (len(todo), skip, meshes, anims, mats, nomat))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
