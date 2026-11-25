# Copyright (c) 2026 @sitxovski (Discord)
import unreal
AT=unreal.AssetToolsHelpers.get_asset_tools(); EAL=unreal.EditorAssetLibrary
dest="/Game/AsdaAssets/Fonts"


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    face=unreal.load_asset("%s/AsdaUIFont"%dest)
    print("FACE:",face, type(face).__name__)
    # типы для composite font
    for t in ("FontData","TypefaceEntry","Typeface","CompositeFont","Font","FontFace"):
        print("HAS unreal.%s ="%t, hasattr(unreal,t))
    try:
        font=AT.create_asset("AsdaUIFontRT",dest,unreal.Font,unreal.FontFactory())
        font.set_editor_property("font_cache_type",unreal.FontCacheType.RUNTIME)
        fd=unreal.FontData(); fd.set_editor_property("font_face_asset",face)
        entry=unreal.TypefaceEntry(); entry.set_editor_property("name","Regular"); entry.set_editor_property("font",fd)
        tf=unreal.Typeface(); tf.set_editor_property("fonts",[entry])
        cf=unreal.CompositeFont(); cf.set_editor_property("default_typeface",tf)
        font.set_editor_property("composite_font",cf)
        EAL.save_loaded_asset(font)
        print("UFONT ok:",font,"typeface_fonts=",len(tf.get_editor_property("fonts")))
    except Exception as e:
        print("UFONT ERR:",e)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
