# Copyright (c) 2026 @sitxovski (Discord)
# Ассеты Load Manager: DT_MapTable (импорт из CSV со структурой FAsdaMapRow) + пустой WBP_LoadingScreen.
#   Запуск: run_headless.bat make_load_assets.py
import unreal


# 2) Пустой WBP_LoadingScreen (concrete-класс; наполняется C++ в рантайме, как WBP_ResPicker)
B = unreal.AsdaUmgBuilder
ui_dir = "/Game/AsdaAssets/UI/Dev"
full = ui_dir + "/WBP_LoadingScreen"


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    # 1) DT_MapTable из CSV (row-struct = FAsdaMapRow)
    row_struct = unreal.load_object(None, "/Script/AsdaReborn.AsdaMapRow")
    if not row_struct:
        unreal.log_warning("[load_assets] FAsdaMapRow не найден (собери C++)")
    else:
        data_dir = "/Game/AsdaAssets/Data"
        if not unreal.EditorAssetLibrary.does_directory_exist(data_dir):
            unreal.EditorAssetLibrary.make_directory(data_dir)
        if unreal.EditorAssetLibrary.does_asset_exist(data_dir + "/DT_MapTable"):
            unreal.EditorAssetLibrary.delete_asset(data_dir + "/DT_MapTable")
        factory = unreal.CSVImportFactory()
        factory.automated_import_settings.import_row_struct = row_struct
        task = unreal.AssetImportTask()
        task.filename = r"@@ASTORY_DEV_ROOT@@\Reports\map_table.csv"
        task.destination_path = data_dir
        task.destination_name = "DT_MapTable"
        task.factory = factory
        task.replace_existing = True
        task.automated = True
        task.save = True
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        dt = unreal.EditorAssetLibrary.load_asset(data_dir + "/DT_MapTable")
        if dt:
            unreal.EditorAssetLibrary.save_asset(data_dir + "/DT_MapTable")
            unreal.log("[load_assets] DT_MapTable импортирован")
        else:
            unreal.log_warning("[load_assets] DT_MapTable НЕ создан")
    if not unreal.EditorAssetLibrary.does_directory_exist(ui_dir):
        unreal.EditorAssetLibrary.make_directory(ui_dir)
    if unreal.EditorAssetLibrary.does_asset_exist(full):
        unreal.EditorAssetLibrary.delete_asset(full)
    bp = B.create_widget_bp(ui_dir, "WBP_LoadingScreen")
    if bp:
        B.compile_and_save(bp)
        unreal.EditorAssetLibrary.save_loaded_asset(bp)
        unreal.log("[load_assets] WBP_LoadingScreen создан")
    else:
        unreal.log_warning("[load_assets] WBP_LoadingScreen НЕ создан")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
