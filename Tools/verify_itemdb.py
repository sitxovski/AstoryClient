# Copyright (c) 2026 @sitxovski (Discord)
import unreal


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    il = unreal.load_asset("/Game/AsdaAssets/Data/ItemDB/DT_item_list.DT_item_list")
    if not il: unreal.log("ITEM_LIST MISS"); 
    else:
        names = unreal.DataTableFunctionLibrary.get_data_table_row_names(il)
        unreal.log("ITEM_LIST строк=%d" % len(names))
        # столбцы + пример имён: экспорт первых строк как JSON
        exp = unreal.DataTableFunctionLibrary.export_data_table_to_string(il) if hasattr(unreal.DataTableFunctionLibrary,'export_data_table_to_string') else None
        # попробуем прочитать имя-поле у нескольких строк
        import json
        js = unreal.DataTableFunctionLibrary.export_data_table_to_json(il) if hasattr(unreal.DataTableFunctionLibrary,'export_data_table_to_json') else None
        if js:
            d = json.loads(js)
            # найти поле с текстом-именем
            sample = d[:3]
            cols = list(sample[0].keys()) if sample else []
            unreal.log("ITEM_LIST колонки=%s" % cols)
            for row in sample:
                nm = row.get("Name") or row.get("name") or row.get("ItemName") or row.get("Title") or ""
                unreal.log("  row=%s поля-с-текстом=%s" % (row.get("Name",""), {k:v for k,v in row.items() if isinstance(v,str) and v.strip() and not v.isdigit()}))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
