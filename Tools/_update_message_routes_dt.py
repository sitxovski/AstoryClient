# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# Обновление DataTable /Game/AsdaAssets/Data/DT_MessageRoutes из CSV-эталона
#   RawAssets/Data/MessageRoutes.csv.
#
# ЗАЧЕМ. UAsdaMessageSubsystem::ReloadRoutes читает источники по приоритету:
#   встроенные умолчания → DataTable → CSV, причём DataTable ПЕРЕКРЫВАЕТ CSV
#   (ApplyDataTable возвращает true и до ApplyCsv дело не доходит). После добавления
#   приёмника ChatRibbon (32) и полей ChatSlot/ChatColor старые строки таблицы содержали
#   бы прежние маски Targets — дубль оповещений в ленту чата не включился бы.
#
# ЧТО ДЕЛАЕТ. Импортирует CSV в существующий ассет (имена строк и структура те же),
#   печатает получившиеся колонки для сверки и сохраняет ассет.
#   Ассет НЕ пересоздаётся — правится существующий (закон ui-window-drag-hittest:
#   перегенерация ассетов стирает ручные правки; здесь это простой re-import данных).
#
# Запуск (без редактора):
#   UnrealEditor-Cmd.exe AsdaReborn.uproject -ExecutePythonScript="...\_update_message_routes_dt.py"
#     -unattended -nopause -nosplash -nosound -NullRHI -stdout -FullStdOutLogOutput -abslog=...
import unreal

DT_PATH = '/Game/AsdaAssets/Data/DT_MessageRoutes.DT_MessageRoutes'


def main():
    dt = unreal.load_object(None, DT_PATH)
    if dt is None:
        unreal.log_error('[routes] DT_MessageRoutes не найден: %s' % DT_PATH)
        return

    csv_abs = unreal.Paths.convert_relative_path_to_full(CSV_PATH)
    unreal.log('[routes] импорт CSV: %s' % csv_abs)
    # Возвращает bool (в 5.8 — успех/неуспех), а не список проблем.
    ok = unreal.DataTableFunctionLibrary.fill_data_table_from_csv_file(dt, csv_abs)
    unreal.log('[routes] импорт CSV: %s' % ('OK' if ok else 'НЕ УДАЛСЯ'))
    if not ok:
        unreal.log_error('[routes] импорт не удался — ассет НЕ сохраняю')
        return

    for col in ('Targets', 'ChatSlot', 'ChatColor', 'Color', 'TtlMs', 'Sound', 'Category'):
        try:
            txt = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, col)
        except Exception as e:                                   # колонки может не быть в старой сборке
            unreal.log_error('[routes] колонка %s недоступна: %s' % (col, e))
            continue
        unreal.log('[routes] %-9s: %s' % (col, txt))

    saved = unreal.EditorAssetLibrary.save_loaded_asset(dt, only_if_is_dirty=False)
    unreal.log('[routes] сохранение ассета: %s' % ('OK' if saved else 'НЕ УДАЛОСЬ'))


def _script_main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global CSV_PATH
    CSV_PATH = unreal.Paths.project_content_dir() + 'AsdaAssets/Data/MessageRoutes.csv'
    main()


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    _script_main()
