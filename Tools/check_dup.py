# Copyright (c) 2026 @sitxovski (Discord)
import unreal
DTL = unreal.DataTableFunctionLibrary
def col(path, c):
    dt = unreal.load_asset(path)
    if not dt: return None
    try: return [v for v in DTL.get_data_table_column_as_string(dt, c) if v and v.strip()][:5]
    except Exception as e: return "err:%s"%e


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    unreal.log("itemDB stateinfotip Name: %s" % col("/Game/AsdaAssets/Data/ItemDB/DT_stateinfotip.DT_stateinfotip","Name"))
    unreal.log("itemDB skilltooltip Name: %s" % col("/Game/AsdaAssets/Data/ItemDB/DT_skilltooltip.DT_skilltooltip","Name"))
    unreal.log("Text Skilltooltip name: %s" % col("/Game/AsdaAssets/Text/Skill/DT_Skilltooltip.DT_Skilltooltip","name"))
    unreal.log("itemDB emotiontip Name: %s" % col("/Game/AsdaAssets/Data/ItemDB/DT_emotiontip.DT_emotiontip","Name"))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
