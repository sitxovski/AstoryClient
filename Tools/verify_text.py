# Copyright (c) 2026 @sitxovski (Discord)
import unreal


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for tbl,rid in [("NPC/DT_npc_list","1"),("Skill/DT_SkillTreeTooltip","0"),("World/DT_Map_info","0"),("Dialogue/DT_Talk",None)]:
        dt=unreal.load_asset("/Game/AsdaAssets/Text/%s"%tbl)
        if not dt: unreal.log("MISS %s"%tbl); continue
        names=unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)
        r=str(rid) if rid else str(names[0])
        row=unreal.DataTableFunctionLibrary.get_data_table_row_from_name(dt, unreal.Name(r)) if False else None
        # прочитать через export
        s=unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt,"name") if False else None
        unreal.log("TBL %-26s строк=%d пример-row=%s"%(tbl,len(names),r))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
