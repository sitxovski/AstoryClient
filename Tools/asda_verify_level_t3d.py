# Copyright (c) 2026 @sitxovski (Discord)
# asda_verify_level_t3d.py — независимая проверка «на карте изменился только держатель AsdaMapObjects_*» (28.10.2026).
#   Для каждой карты экспортирует в T3D (все не-дефолтные свойства всех акторов и компонентов) текущий уровень и его
#   бэкап. Бэкап временно кладётся копией в /Game/_VerifyTmp/<карта>.umap (после прогона папка удаляется). Ничего не
#   сохраняет. Сравнение T3D — Tools/asda_verify_level_t3d_diff.py (вне UE).
# env: ASDA_MAPS (через запятую), ASDA_BACKUP (корень бэкапа с AstoryClient/Content/Map/...), ASDA_OUT (папка T3D).
import os, sys, shutil
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_maps as _MAPS

CONTENT = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Content"
TMP_DIR = os.path.join(CONTENT, "_VerifyTmp")
MAPS = [m for m in os.environ.get("ASDA_MAPS", "").split(",") if m]
BACKUP = os.environ.get("ASDA_BACKUP", "")
OUT = os.environ.get("ASDA_OUT", "")


def export_t3d(world, path):
    """Весь уровень в T3D через штатный экспортёр уровня."""
    t = unreal.AssetExportTask()
    t.object = world
    t.filename = path
    t.automated = True
    t.prompt = False
    t.replace_identical = True
    t.selected = False
    return unreal.Exporter.run_asset_export_task(t)


def main():
    os.makedirs(OUT, exist_ok=True)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    os.makedirs(TMP_DIR, exist_ok=True)
    try:
        for m in MAPS:
            a = _MAPS.map_asset_name(m)
            src = os.path.join(BACKUP, "AstoryClient/Content/Map", a, a + ".umap")
            shutil.copy2(src, os.path.join(TMP_DIR, a + ".umap"))
        unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/Game/_VerifyTmp"], True)
        for m in MAPS:
            a = _MAPS.map_asset_name(m)
            seq = [("after", "/Game/Map/%s/%s" % (a, a)), ("before", "/Game/_VerifyTmp/%s" % a)]
            if os.environ.get("ASDA_TWICE") == "1":   # контроль шума: тот же бэкап второй раз (сгенерированные при загрузке поля)
                seq.append(("before2", "/Game/_VerifyTmp/%s" % a))
            if os.environ.get("ASDA_ORDER") == "before_first":
                # контроль порядка загрузки: бэкап первым, текущий уровень — вторым (<карта>_after_late.t3d)
                seq = [("before", "/Game/_VerifyTmp/%s" % a), ("after_late", "/Game/Map/%s/%s" % (a, a))]
            if os.environ.get("ASDA_RESAVE") == "1":
                # контроль «само сохранение»: временная копия бэкапа загружается и сохраняется БЕЗ правок, затем
                # перезагружается (через другой уровень) и выгружается как <карта>_resaved.t3d
                les.load_level("/Game/_VerifyTmp/%s" % a)
                les.save_current_level()
                les.load_level("/Game/Map/%s/%s" % (a, a))
                seq = [("before", "/Game/_VerifyTmp/%s" % a)]
                seq[0] = ("resaved", seq[0][1])
            for tag, lvl in seq:
                les.load_level(lvl)
                w = unreal.EditorLevelLibrary.get_editor_world()
                ok = export_t3d(w, os.path.join(OUT, "%s_%s.t3d" % (a, tag)))
                unreal.log("[t3d] %s %s -> %s" % (a, tag, ok))
        les.new_level("/Temp/Untitled_verify")   # отпустить временный пакет перед удалением
    finally:
        unreal.log("ЗАВЕРШЕНО asda_verify_level_t3d")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
