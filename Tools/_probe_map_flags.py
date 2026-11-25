# Copyright (c) 2026 @sitxovski (Discord)
# _probe_map_flags.py — проверка колонок ItemRestrict / CampwarPkFlag в DT_MapTable (ничего не меняет).
import unreal


def main():
    dt = unreal.EditorAssetLibrary.load_asset("/Game/AsdaAssets/Data/DT_MapTable")
    if not dt:
        unreal.log_warning("[map-flags] нет DT_MapTable")
        return
    # Колонки читаем как строки — единственный переносимый доступ к строкам C++-структуры из Python.
    names = list(unreal.DataTableFunctionLibrary.get_data_table_row_names(dt))
    cols = {}
    for c in ("MapId", "DirName", "ItemRestrict", "CampwarPkFlag"):
        cols[c] = list(unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, c))
    for i, rn in enumerate(names):
        unreal.log("[map-flags] row=%-4s MapId=%-4s Dir=%-20s ItemRestrict=%-3s CampwarPkFlag=%s" % (
            rn, cols["MapId"][i], cols["DirName"][i], cols["ItemRestrict"][i], cols["CampwarPkFlag"][i]))
    unreal.log("[map-flags] ГОТОВО")


if __name__ == "__main__":
    main()
