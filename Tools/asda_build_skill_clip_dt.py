# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_skill_clip_dt.py — DataTable `DT_SkillClips` из `RawAssets/Data/SkillClips.csv` (ВНЕ Content!).
#
# CSV готовит `asda_dump_skill_dispatch.py` (снимает таблицу `CharSkillEffect_InitDispatchTable@0xA89510`
#   из бинаря + combo time.bin + skill_info.sif + карту клип→anim_NN от `export_char_skillclips.py`).
# RowStruct = `FAsdaSkillClipRow` (Public/Skill/AsdaSkillClipRow.h) — ТРЕБУЕТ сборки C++ до запуска.
#
# Запуск: Tools\run_headless.bat Tools\asda_build_skill_clip_dt.py
# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
import os

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
CSV = os.path.abspath(os.path.join(HERE, "..", "RawAssets", "Data", "SkillClips.csv"))
OUT_PKG = "/Game/AsdaAssets/Data"
OUT_NAME = "DT_SkillClips"
STRUCT_PATH = "/Script/AsdaReborn.AsdaSkillClipRow"


def log(m):
    unreal.log_warning("[skillclip-dt] %s" % m)


def main():
    if not os.path.exists(CSV):
        log("НЕТ %s — сначала python Tools\\asda_dump_skill_dispatch.py" % CSV)
        return
    row_struct = unreal.load_object(None, STRUCT_PATH)
    if not row_struct:
        log("НЕТ структуры %s — собери C++ (AsdaSkillClipRow.h)" % STRUCT_PATH)
        return
    factory = unreal.CSVImportFactory()
    factory.set_editor_property("automated_import_settings",
                                unreal.CSVImportSettings(import_row_struct=row_struct))
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", CSV)
    task.set_editor_property("destination_path", OUT_PKG)
    task.set_editor_property("destination_name", OUT_NAME)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    task.set_editor_property("factory", factory)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    full = "%s/%s" % (OUT_PKG, OUT_NAME)
    dt = unreal.load_asset(full)
    if not dt:
        log("!! таблица не создалась: %s" % full)
        return
    rows = unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)
    unreal.EditorAssetLibrary.save_asset(full, only_if_is_dirty=False)
    log("готово: %s — строк %d" % (full, len(rows)))


# ★★★ Закон tools-no-import-side-effects: main() только при ПРЯМОМ запуске.
#   Прежняя проверка `__name__ != "asda_build_skill_clip_dt"` ловила лишь импорт ровно под этим именем:
#   `import Tools.asda_build_skill_clip_dt` или `importlib.util.spec_from_file_location("X", ...)` дают другое
#   имя — и main() выстреливал бы. Надёжный признак импорта — `__spec__`: его выставляет ЛЮБАЯ
#   машинерия импорта (import / importlib / пакет / runpy.run_module) и НЕ выставляет прямое
#   исполнение файла (python file.py, UE -ExecutePythonScript). `python -m` даёт "__main__".
def _is_direct_run():
    """True — файл запущен НАПРЯМУЮ, а не импортирован."""
    return __name__ == "__main__" or globals().get("__spec__") is None


if _is_direct_run():
    main()
