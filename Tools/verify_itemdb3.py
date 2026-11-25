# Copyright (c) 2026 @sitxovski (Discord)
import unreal
DTL = unreal.DataTableFunctionLibrary
def dump(path, col):
    dt = unreal.load_asset(path)
    if not dt: unreal.log("MISS %s"%path); return
    try:
        vals = DTL.get_data_table_column_as_string(dt, col)
        nonempty = [v for v in vals if v and v.strip()][:4]
        unreal.log("DT %-24s строк=%d непустых-%s(%d)=%s" % (path.split('/')[-1].split('.')[0], len(vals), col, len([v for v in vals if v and v.strip()]), nonempty))
    except Exception as e: unreal.log("%s err %s" % (path.split('/')[-1], e))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    unreal.log("METHODS: %s" % [m for m in dir(DTL) if 'column' in m.lower() or 'string' in m.lower() or 'row' in m.lower()][:8])
    dump("/Game/AsdaAssets/Data/ItemDB/DT_item_list.DT_item_list","ItemName")
    dump("/Game/AsdaAssets/Data/ItemDB/DT_pettable.DT_pettable","Name")
    dump("/Game/AsdaAssets/Data/ItemDB/DT_soulmateskill.DT_soulmateskill","Name")
    dump("/Game/AsdaAssets/Data/ItemDB/DT_item_tooltip.DT_item_tooltip","Description")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
