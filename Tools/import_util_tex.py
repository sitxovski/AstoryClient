# Copyright (c) 2026 @sitxovski (Discord)
import unreal, os
SRC=r"@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Textures\Util\T_Transparent.png"
DEST="/Game/AsdaAssets/Textures/Util"


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    import sys as _sys
    _sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import asda_model_import_ue as _MIU   # ★ дедуп №2 Т17: общий импорт пачкой (AutomatedAssetImportData)
    a=_MIU.import_batch([SRC], DEST)
    for x in a:
        if isinstance(x,unreal.Texture2D):
            x.set_editor_property("compression_settings",unreal.TextureCompressionSettings.TC_EDITOR_ICON)
            x.set_editor_property("mip_gen_settings",unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
            x.set_editor_property("srgb",False)
    unreal.EditorAssetLibrary.save_directory(DEST,only_if_is_dirty=False,recursive=True)
    unreal.log("[UTIL] импортировано T_Transparent: %d"%len(a))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
