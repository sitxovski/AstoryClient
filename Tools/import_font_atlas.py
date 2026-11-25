# Copyright (c) 2026 @sitxovski (Discord)
# import_font_atlas.py — импорт bitmap-атласа шрифта в /Game/AsdaAssets/Fonts/FontAtlas0.
#   Пиксель-perfect: без DXT (UserInterface2D/TC_EDITOR_ICON), без мипов, sRGB OFF, Filter=Nearest (POINT), NeverStream.
#   Запуск: run_headless.bat import_font_atlas.py   (после make_font_atlas.py).
import unreal, os

SRC  = r"@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Fonts\FontAtlas0.png"
DEST = "/Game/AsdaAssets/Fonts"

def log(m): unreal.log("[import_font] " + str(m))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    import sys as _sys
    _sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import asda_model_import_ue as _MIU   # ★ дедуп №2 Т17: общий импорт пачкой (AutomatedAssetImportData)
    assets = _MIU.import_batch([SRC], DEST)
    n = 0
    for a in assets:
        if isinstance(a, unreal.Texture2D):
            a.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)  # UserInterface2D, без DXT
            a.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
            a.set_editor_property("srgb", False)                                   # alpha-маска: линейное покрытие
            a.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)       # POINT-семплинг (без слипания/AA)
            a.set_editor_property("never_stream", True)
            n += 1
    log("импортировано атласов: %d" % n)
    unreal.EditorAssetLibrary.save_directory(DEST, only_if_is_dirty=False, recursive=True)
    log("ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
