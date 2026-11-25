# Copyright (c) 2026 @sitxovski (Discord)
import unreal
AT=unreal.AssetToolsHelpers.get_asset_tools()
EAL=unreal.EditorAssetLibrary
src=r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/RawAssets/Fonts/AsdaUIFont.ttf"
dest="/Game/AsdaAssets/Fonts"


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    d=unreal.AutomatedAssetImportData(); d.set_editor_property("destination_path",dest)
    d.set_editor_property("filenames",[src]); d.set_editor_property("replace_existing",True)
    made=AT.import_assets_automated(d)
    for a in made:
        print("IMPORTED", a, type(a).__name__)
        # ★ Пиксель-шрифт: хинтинг=None (иначе FreeType grid-fit искажает глифы при 14px = «сжат/растянут»).
        for prop,val in [("hinting", unreal.FontHinting.NONE)]:
            try: a.set_editor_property(prop, val); print("  set %s=%s"%(prop,val))
            except Exception as e: print("  set %s ERR %s"%(prop,e))
        try: EAL.save_loaded_asset(a)
        except Exception as e: print("save err",e)
    # probe: SlateFontInfo + TextBlock.set_font
    try:
        fi=unreal.SlateFontInfo()
        print("SFI attrs:", [p for p in dir(fi) if p in ('font_object','typeface_font_name','size','set_editor_property')][:5])
        fnt=unreal.load_asset("%s/AsdaUIFont"%dest)
        fi.set_editor_property("font_object", fnt)
        fi.set_editor_property("size", 14)
        print("SFI ok font=",fi.get_editor_property("font_object"))
    except Exception as e: print("SFI err", e)
    print("TextBlock has set_font:", hasattr(unreal.TextBlock,"set_font"))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
