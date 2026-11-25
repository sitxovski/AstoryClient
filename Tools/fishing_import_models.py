# Copyright (c) 2026 @sitxovski (Discord)
# fishing_import_models.py — РЫБАЛКА, волна В5а: импорт СКЕЛЕТНЫХ удочек/рыб/зоны (.glb) + материалы.
# По образцу asda_import_soulguard_models.py (тот же путь: Interchange-импорт пачкой, MIC → M_AsdaObjectLit
#   + Tex по имени материала, снятие Nanite); трогает ТОЛЬКО свою папку.
# Источник : RawAssets/Models/Fishing/*.glb            (fishing_export_assets.py rod/fish/zone)
# Приёмник : /Game/AsdaAssets/Models/Skeletal/Fishing/<модель>/SkeletalMeshes/<модель>[anim_NN]
#   удочки  ch1_n_00_fird_0{0,1,2}_00_0_00 + anim_00..07 (клип = action − 60, CFishing_LoadRodAnimations@0xFCD6F0)
#   рыбы    fs_fsm_<вид>_0N / fs_stuff_00 + anim_00   (<имя>_00.jbb, CItemCollectUI_LoadPreviewModel@0xFD1370)
#   зона    fs_zone + anim_00 (петля fs_zone.jbb, CMap_LoadFishingZoneMesh@0xFC5BE0)
# ⚠ Те же имена уже лежат СТАТИК-мешами в /Game/AsdaAssets/Models/Char/<имя> (оружие в руке, превью) —
#   поэтому скелеталы едут в ОТДЕЛЬНЫЙ каталог Skeletal/Fishing: коллизии пакетов нет, статики не трогаются.
# ⚠ Состав костей (грабля 3 [[skin-pipeline-fixed-path]]): FISHING_REIMPORT=1 удаляет каталог целиком
#   перед импортом (иначе Interchange молча оставит старый Skeleton).
# Запуск   : python Tools/run_headless_safe.py Tools/fishing_import_models.py  (или из fishing_import_all.py)
# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import unreal
import asda_ue_paths as _U
import asda_model_import_ue as MI

RAW = os.path.join(_U.RAW_MODELS, "Fishing")              # каталог .glb экспортёра
DEST = _U.G_MODELS_SKELETAL + "/Fishing"                  # каталог скелеталов рыбалки
TEX_ROOT = _U.G_TEXTURES                                  # корень индекса текстур
MASTER = _U.M_OBJECT_LIT                                  # мастер, как у петов/хранителей/транспорта
REIMPORT = os.environ.get("FISHING_REIMPORT") == "1"      # полный переимпорт (удалить каталог)
CHUNK = 6                                                 # размер пачки импорта (устойчиво по NullRHI)

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()


def log(m):
    """Журнал канала импорта рыбалки."""
    unreal.log_warning("[fishing-import] %s" % m)


def normalize_single_clip_names():
    """Interchange называет ЕДИНСТВЕННЫЙ клип glb «<модель>_Anim» (а не по имени анимации glb).

    Рантайм порта строит путь клипа как «<модель>anim_%02d» (UAsdaModelAnimSubsystem::BuildSkelAnimPath),
    поэтому у рыб/зоны (один клип = трек 0 эталона) переименовываем в «<модель>anim_00». Возвращает число правок.
    """
    n = 0
    for ad in AR.get_assets_by_path(unreal.Name(DEST), recursive=True):
        if str(ad.asset_class_path.asset_name) != "AnimSequence":
            continue
        pkg, an = str(ad.package_name), str(ad.asset_name)
        if not an.endswith("_Anim"):
            continue
        dst = "%s/%sanim_00" % (os.path.dirname(pkg), an[:-len("_Anim")])
        if EAL.does_asset_exist(dst):
            log("  ! %s уже есть — %s не переименован" % (dst, pkg))
            continue
        if EAL.rename_asset(pkg, dst):
            EAL.save_asset(dst, only_if_is_dirty=False)
            n += 1
        else:
            log("  ! переименование %s -> %s не удалось" % (pkg, dst))
    return n


def main():
    """Импорт всех .glb каталога (инкрементально) + подвязка материалов. Возвращает True при успехе."""
    if not os.path.isdir(RAW):
        log("нет папки %s — сначала python Tools/fishing_export_assets.py" % RAW)
        return False
    if REIMPORT and EAL.does_directory_exist(DEST):
        log("ПЕРЕИМПОРТ: удаляю %s" % DEST)
        EAL.delete_directory(DEST)
    glbs = sorted(f for f in os.listdir(RAW) if f.lower().endswith(".glb"))
    todo, skip = [], 0
    for fn in glbs:
        mdir = "%s/%s" % (DEST, os.path.splitext(fn)[0])
        if not REIMPORT and EAL.does_directory_exist(mdir) and EAL.list_assets(mdir):
            skip += 1
        else:
            todo.append(os.path.join(RAW, fn))
    log("моделей=%d, к импорту=%d, пропуск=%d" % (len(glbs), len(todo), skip))
    for i in range(0, len(todo), CHUNK):
        MI.import_batch(todo[i:i + CHUNK], DEST)
        log("  импортировано %d/%d" % (min(i + CHUNK, len(todo)), len(todo)))
        EAL.save_directory(DEST, only_if_is_dirty=True, recursive=True)
        try:
            unreal.SystemLibrary.collect_garbage()
        except Exception:
            pass

    tex = MI.tex_index("all_last", root=TEX_ROOT)
    log("текстур в индексе: %d" % len(tex))
    master = unreal.load_asset(MASTER)
    mats = nomat = meshes = anims = 0
    for ad in AR.get_assets_by_path(unreal.Name(DEST), recursive=True):
        cls = str(ad.asset_class_path.asset_name)
        if cls == "MaterialInstanceConstant":
            a = unreal.load_asset(str(ad.package_name))
            tp = MI.find_tex(str(ad.asset_name), tex, "soulguard")
            MEL.set_material_instance_parent(a, master)
            if tp:
                MEL.set_material_instance_texture_parameter_value(a, "Tex", unreal.load_asset(tp))
            else:
                nomat += 1
                log("  ! текстура не найдена для материала %s" % ad.asset_name)
            MEL.set_material_instance_scalar_parameter_value(a, "Ambient", 1.0)
            mats += 1
        elif cls == "SkeletalMesh":
            meshes += 1
        elif cls == "AnimSequence":
            anims += 1
    MI.disable_nanite(DEST, ("SkeletalMesh",))
    log("единственный клип «_Anim» → «anim_00»: переименовано %d" % normalize_single_clip_names())
    EAL.save_directory(DEST, only_if_is_dirty=True, recursive=True)
    log("ГОТОВО: импортировано=%d, пропущено=%d, скелеталов=%d, клипов=%d, материалов=%d (без текстуры=%d)"
        % (len(todo), skip, meshes, anims, mats, nomat))
    return nomat == 0


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
