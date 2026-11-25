# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# Диагностика: распечатать содержимое DT_MessageRoutes (проверка, что CSV разобрался корректно —
#   маски приёмников/цвета/TTL/звуки не потерялись при импорте). Только чтение.
import unreal


def main():
    dt = unreal.load_object(None, '/Game/AsdaAssets/Data/DT_MessageRoutes.DT_MessageRoutes')
    if dt is None:
        unreal.log_error('[probe] DT_MessageRoutes не найден')
        return
    txt = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, 'Targets')
    unreal.log('[probe] Targets: %s' % txt)
    txt = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, 'Color')
    unreal.log('[probe] Color: %s' % txt)
    txt = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, 'TtlMs')
    unreal.log('[probe] TtlMs: %s' % txt)
    txt = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, 'Sound')
    unreal.log('[probe] Sound: %s' % txt)
    txt = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, 'Category')
    unreal.log('[probe] Category: %s' % txt)


if __name__ == '__main__':
    main()
