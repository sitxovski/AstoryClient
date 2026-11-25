# Copyright (c) 2026 @sitxovski (Discord)
# _probe_quest_marks.py — проверка, что DT_NpcQuestScript / DT_NpcQuestNpc импортированы корректно
#   (в т.ч. массивы целей/предметов) и что данные сходятся с сырым Nsr.bsz / npc_list.BIN.
# Запуск: Tools\run_headless.bat Tools\_probe_quest_marks.py
import unreal

L = unreal.log_warning


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    S = unreal.load_object(None, "/Game/AsdaAssets/Data/NPC/DT_NpcQuestScript.DT_NpcQuestScript")
    N = unreal.load_object(None, "/Game/AsdaAssets/Data/NPC/DT_NpcQuestNpc.DT_NpcQuestNpc")
    L("[probe] script rows=%d npc rows=%d" % (len(S.get_row_names()), len(N.get_row_names())))
    rows = [str(r) for r in unreal.DataTableFunctionLibrary.get_data_table_row_names(S)]
    sample = [r for r in rows if r.startswith("108npc_")][:4]
    L("[probe] образцы строк 108npc: %s" % sample)
    L("[probe] Mark-колонка (первые 12): %s" % unreal.DataTableFunctionLibrary.get_data_table_column_as_string(S, "Mark")[:12])
    L("[probe] ObjIds-колонка (первые 6): %s" % unreal.DataTableFunctionLibrary.get_data_table_column_as_string(S, "ObjIds")[:6])
    L("[probe] QuestDone (первые 12): %s" % unreal.DataTableFunctionLibrary.get_data_table_column_as_string(S, "QuestDone")[:12])
    L("[probe] npc NsrName (первые 6): %s" % unreal.DataTableFunctionLibrary.get_data_table_column_as_string(N, "NsrName")[:6])
    L("[probe] npc WorldX (первые 6): %s" % unreal.DataTableFunctionLibrary.get_data_table_column_as_string(N, "WorldX")[:6])


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
