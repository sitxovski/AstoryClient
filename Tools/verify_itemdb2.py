# Copyright (c) 2026 @sitxovski (Discord)
import unreal, json
def dump(path, nfield, n=4):
    dt = unreal.load_asset(path)
    if not dt: unreal.log("MISS %s" % path); return
    try: js = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json(dt))
    except Exception as e: unreal.log("%s export err %s" % (path,e)); return
    rows = len(js)
    samples = [r.get(nfield,"") for r in js if r.get(nfield,"").strip()][:n]
    unreal.log("DT %-42s строк=%d непустых-%s: %s" % (path.split('.')[0].split('/')[-1], rows, nfield, samples))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    dump("/Game/AsdaAssets/Data/ItemDB/DT_item_list.DT_item_list", "ItemName")
    dump("/Game/AsdaAssets/Data/ItemDB/DT_pettable.DT_pettable", "Name")
    dump("/Game/AsdaAssets/Data/ItemDB/DT_soulmateskill.DT_soulmateskill", "Name")
    dump("/Game/AsdaAssets/Data/ItemDB/DT_emotiontip.DT_emotiontip", "Name")
    dump("/Game/AsdaAssets/Data/ItemDB/DT_item_tooltip.DT_item_tooltip", "Description")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
