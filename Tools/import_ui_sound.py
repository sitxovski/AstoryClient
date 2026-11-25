# Copyright (c) 2026 @sitxovski (Discord)
import unreal
SRC=r"@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Sounds\UI\S_e_button09.wav"
DEST="/Game/AsdaAssets/Sounds/UI"; FULL=DEST+"/S_e_button09"
def log(m): unreal.log("[ui_sound] "+str(m))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    if unreal.EditorAssetLibrary.does_asset_exist(FULL): unreal.EditorAssetLibrary.delete_asset(FULL)
    d=unreal.AutomatedAssetImportData(); d.set_editor_property("destination_path",DEST)
    d.set_editor_property("filenames",[SRC]); d.set_editor_property("replace_existing",True)
    unreal.AssetToolsHelpers.get_asset_tools().import_assets_automated(d)
    unreal.EditorAssetLibrary.save_directory(DEST,only_if_is_dirty=False,recursive=True)
    log("ГОТОВО -> "+FULL)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
