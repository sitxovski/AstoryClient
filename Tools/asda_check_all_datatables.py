# Copyright (c) 2026 @sitxovski (Discord)
# asda_check_all_datatables.py — ОБЩАЯ ПРОВЕРКА всех DataTable проекта после правки структур.
# Грузит каждый ассет-DataTable из /Game, печатает {путь, RowStruct, строк, полей} и ловит:
#   * ассеты, у которых RowStruct потерян (структуру переименовали/удалили);
#   * ассеты с нулём строк (признак того, что импорт молча не прошёл);
#   * расхождения «данные ↔ структура» проявятся в логе строками
#     «cannot be found in struct» / «is missing an entry» — их ищем грепом по логу запуска.
# Запуск: UnrealEditor-Cmd.exe <проект> -ExecutePythonScript=<этот файл> -unattended -NullRHI
# ★ ПРАВИЛО tools-no-import-side-effects: при импорте модуля НИЧЕГО не выполняется.
import unreal


def L(m):
    unreal.log_warning("[dt-check] %s" % m)


def main():
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    ar.wait_for_completion()
    assets = ar.get_assets_by_class(unreal.TopLevelAssetPath("/Script/Engine", "DataTable"), True)
    L("DataTable-ассетов в проекте: %d" % len(assets))
    bad = 0
    rows = []
    for a in sorted(assets, key=lambda x: str(x.package_name)):
        path = str(a.package_name)
        dt = unreal.EditorAssetLibrary.load_asset(path)
        if not dt:
            L("!! НЕ ЗАГРУЗИЛСЯ: %s" % path)
            bad += 1
            continue
        rs = dt.get_editor_property("row_struct")
        n = len(unreal.DataTableFunctionLibrary.get_data_table_row_names(dt))
        cols = list(unreal.DataTableFunctionLibrary.get_data_table_column_names(dt)) \
            if hasattr(unreal.DataTableFunctionLibrary, "get_data_table_column_names") else []
        rows.append((path, rs.get_name() if rs else "<НЕТ>", n, len(cols)))
        if not rs:
            L("!! RowStruct ПОТЕРЯН: %s" % path)
            bad += 1
        elif n == 0:
            L("!! 0 строк: %s (RowStruct %s)" % (path, rs.get_name()))
            bad += 1
    for path, rs, n, nc in rows:
        L("%-70s %-32s строк=%-6d полей=%d" % (path, rs, n, nc))
    L("ИТОГ: проблемных ассетов %d из %d" % (bad, len(assets)))


if __name__ == "__main__":
    main()
