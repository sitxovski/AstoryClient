# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# Импорт ТАБЛИЦЫ МАРШРУТИЗАЦИИ СООБЩЕНИЙ в DataTable /Game/AsdaAssets/Data/DT_MessageRoutes.
#
# Источник: RawAssets/Data/MessageRoutes.csv (тот же файл, что читает рантайм-фолбэк
#   UAsdaMessageSubsystem::ApplyCsv). После импорта таблица правится ПРЯМО В РЕДАКТОРЕ UE
#   (закон ue-editor-friendly), а рантайм берёт именно DataTable (приоритет выше CSV).
#
# Запуск:  Tools\run_headless.bat Tools\asda_import_message_routes.py
#
# ★ Закон tools-no-import-side-effects: при импорте модуля НИЧЕГО не выполняется — только main().
import os
import unreal

CSV = r'@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Data\MessageRoutes.csv'
DST_DIR = '/Game/AsdaAssets/Data'
DST_NAME = 'DT_MessageRoutes'
ROW_STRUCT = '/Script/AsdaReborn.AsdaMsgRoute'


def log(s):
    unreal.log('[msgroutes] ' + s)


def main():
    struct = unreal.load_object(None, ROW_STRUCT)
    if struct is None:
        unreal.log_error('[msgroutes] RowStruct %s не найден — модуль AsdaReborn не собран?' % ROW_STRUCT)
        return False
    if not os.path.isfile(CSV):
        unreal.log_error('[msgroutes] нет CSV %s' % CSV)
        return False

    factory = unreal.CSVImportFactory()
    settings = factory.get_editor_property('automated_import_settings')
    settings.set_editor_property('import_type', unreal.CSVImportType.ECSV_DATA_TABLE)
    settings.set_editor_property('import_row_struct', struct)

    task = unreal.AssetImportTask()
    task.set_editor_property('filename', CSV)
    task.set_editor_property('destination_path', DST_DIR)
    task.set_editor_property('destination_name', DST_NAME)
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('automated', True)
    task.set_editor_property('save', True)
    task.set_editor_property('factory', factory)

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    obj = unreal.load_object(None, '%s/%s.%s' % (DST_DIR, DST_NAME, DST_NAME))
    if obj is None:
        unreal.log_error('[msgroutes] DataTable НЕ создан')
        return False
    dt = unreal.DataTable.cast(obj)
    rows = unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)
    log('OK -> %s, строк %d: %s' % (DST_NAME, len(rows), ', '.join([str(r) for r in rows])))
    # ★ 16.09.2026 (раздел 22, Reports/dedup_audit/savedir_guard_2026-09-16.md): в
    #   /Game/AsdaAssets/Data лежит DT_ServerList — пакет сцены логина, а only_if_is_dirty=False пересохранял
    #   ВСЮ папку, включая его (даже нетронутый). Сохраняем без пакетов логина.
    import sys as _sys_lg
    _sys_lg.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import asda_login_guard as _LG
    _LG.save_dir_login_safe(DST_DIR, 'asda_import_message_routes.py', only_if_is_dirty=False)
    return True


if __name__ == '__main__':
    main()
