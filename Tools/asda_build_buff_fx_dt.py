# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_buff_fx_dt.py — DataTable `DT_BuffFx` из `RawAssets/Data/BuffFx.csv` (ВНЕ Content!).
#
# CSV готовит `Tools/asda_dump_buff_fx.py` (дамп канала состояний из бинаря + разбор тел
#   обработчиков эталона). RowStruct = `FAsdaBuffFxRow` (Public/UI/AsdaBuffFxRow.h) — ТРЕБУЕТ
#   сборки C++ до запуска.
#
# Запуск: Tools\run_headless.bat Tools\asda_build_buff_fx_dt.py
# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
import os

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
CSV = os.path.abspath(os.path.join(HERE, "..", "RawAssets", "Data", "BuffFx.csv"))
OUT_PKG = "/Game/AsdaAssets/Data"
OUT_NAME = "DT_BuffFx"
STRUCT_PATH = "/Script/AsdaReborn.AsdaBuffFxRow"


def log(m):
    unreal.log_warning("[bufffx-dt] %s" % m)


def main():
    if not os.path.exists(CSV):
        log("НЕТ %s — сначала python Tools\\asda_dump_buff_fx.py" % CSV)
        return
    row_struct = unreal.load_object(None, STRUCT_PATH)
    if not row_struct:
        log("НЕТ структуры %s — собери C++ (AsdaBuffFxRow.h)" % STRUCT_PATH)
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


def _is_direct_run():
    """True — файл запущен НАПРЯМУЮ, а не импортирован ([[tools-no-import-side-effects]])."""
    return __name__ == "__main__" or globals().get("__spec__") is None


if _is_direct_run():
    main()
