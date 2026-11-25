# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
asda_reimport_item_list.py — headless UE5 Python: точечный переимпорт ОДНОЙ таблицы
/Game/AsdaAssets/Data/ItemDB/DT_item_list из RawAssets/Data/ItemDB/item_list.csv
+ проверка результата (кол-во строк, сколько имён «-», примеры по языкам).

Отличие от gen_itemdb_datatables.py — не трогает остальные 50 таблиц.
Логика импорта идентична (CSVImportFactory + FAsdaItemRow), чтобы ассет остался прежним по схеме.

Запуск:
  Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_reimport_item_list.py
"""
import io
import json
import os

import unreal

PROJECT  = r'@@ASTORY_DEV_ROOT@@\AstoryClient'
MANIFEST = os.path.join(PROJECT, 'Tools', '_out', 'itemdb_manifest.json')
CSV_DIR  = os.path.join(PROJECT, 'RawAssets', 'Data', 'ItemDB')
TMP_DIR  = os.path.join(PROJECT, 'Tools', '_out', 'dt_csv')
TABLE    = 'item_list'


def log(m):  unreal.log('[item-names] ' + m)
def err(m):  unreal.log_error('[item-names] ' + m)


def _gen():
    """Единственный переимпортёр item-БД `gen_itemdb_datatables` (★ дедуп №2 В2 Т9); импорт при вызове."""
    import sys
    here = os.path.dirname(os.path.abspath(__file__))
    if here not in sys.path:
        sys.path.append(here)
    import gen_itemdb_datatables
    return gen_itemdb_datatables


def main():
    with io.open(MANIFEST, 'r', encoding='utf-8') as f:
        info = next(i for i in json.load(f) if i['table'] == TABLE)

    # ★ дедуп №2 В2 Т9: тело импорта — общее `gen_itemdb_datatables.import_table` (гейт слоя имён, `_data_hex`, маркер
    #   ASDA_WRITES_ASSET, сохранение только этого ассета). Проверки содержимого ниже — свои, как были.
    dt = _gen().import_table(info, log=log, warn=log, err=err)
    if dt is None:
        return

    # ── проверка содержимого ассета ───────────────────────────────────────────
    names = unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)
    log('строк в DT_item_list: %d' % len(names))

    probe_ids = [20551, 20917, 20921,                       # RU (были до правки)
                 20625, 20802,                              # EN
                 23317, 68764,                              # AR
                 22705, 22706,                              # KO
                 20626, 23110]                              # без имени (ожидается «-»)
    codes = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, 'ItemCode')
    items = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, 'ItemName')
    id2row = {}
    dash = 0
    for c, n in zip(codes, items):
        s = (n or '').strip()
        if s == '-' or s == '':
            dash += 1
        ci = int(c)
        if ci in probe_ids:
            id2row[ci] = n
    log('строк с именем «-»: %d (колонок: code=%d name=%d)' % (dash, len(codes), len(items)))
    for i in probe_ids:
        log('  itemCode=%d -> %s' % (i, id2row.get(i, '<НЕТ СТРОКИ>')))

    # компактный отчёт в файл (в чат — только итог)
    outp = r'@@ASTORY_DEV_ROOT@@\Reports\item_names_reimport.json'
    with io.open(outp, 'w', encoding='utf-8') as f:
        f.write(json.dumps({'rows': len(names), 'dash': dash,
                            'probes': {str(k): v for k, v in id2row.items()}},
                           ensure_ascii=False, indent=1))
    log('отчёт: %s' % outp)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
