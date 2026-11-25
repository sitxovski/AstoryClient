# Copyright (c) 2026 @sitxovski (Discord)
# Доказательство, что DT_title_list переимпортирована: читаем строки 65…69 ИЗ АССЕТА
#   (а не из CSV) и печатаем грейд/очки/показ. Нужно после каждой правки таблицы званий —
#   30.08.2026 уже был случай, когда CSV обновили, а DataTable нет, и в игре правки не было.
import unreal

TABLE = "/Game/AsdaAssets/Data/ItemDB/DT_title_list.DT_title_list"
IDS = ["65", "66", "67", "68", "69", "276"]

dt = unreal.load_asset(TABLE)
if dt is None:
    unreal.log_error("[ЗВАНИЯ-DT] ассет не загрузился: %s" % TABLE)
else:
    names = [str(n) for n in unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)]
    unreal.log("[ЗВАНИЯ-DT] строк в ассете: %d" % len(names))
    # Имя строки у импортёра = значение первой колонки CSV (_row). Ищем по полю Id.
    rows = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, "Id")
    colors = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, "ColorIndex")
    points = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, "Point")
    enabled = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, "Enabled")
    for i, rid in enumerate(rows):
        if rid in IDS:
            unreal.log("[ЗВАНИЯ-DT] id %s: ColorIndex=%s Point=%s Enabled=%s"
                       % (rid, colors[i], points[i], enabled[i]))
unreal.SystemLibrary.quit_editor()
