# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
asda_reimport_item_tables.py — headless UE5 Python: переимпорт указанных таблиц item-БД
из RawAssets/Data/ItemDB/<table>.csv в /Game/AsdaAssets/Data/ItemDB/DT_<table>.

Зачем отдельно от gen_itemdb_datatables.py: тот перезаливает все 51 таблицу (~12 мин),
здесь — только нужные (по умолчанию item_list + item_img_table, ~1 мин).

Логика импорта идентична gen_itemdb_datatables.py (CSVImportFactory + RowStruct из манифеста),
чтобы схема ассета не менялась.

Запуск:
  Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_reimport_item_tables.py
  (список таблиц можно переопределить переменной окружения ASDA_TABLES="item_list,pricetable")
"""
import io
import json
import os

import unreal

PROJECT  = r'@@ASTORY_DEV_ROOT@@\AstoryClient'
MANIFEST = os.path.join(PROJECT, 'Tools', '_out', 'itemdb_manifest.json')
CSV_DIR  = os.path.join(PROJECT, 'RawAssets', 'Data', 'ItemDB')
TMP_DIR  = os.path.join(PROJECT, 'Tools', '_out', 'dt_csv')
REPORT   = r'@@ASTORY_DEV_ROOT@@\Reports\itemdb_reimport_tables.json'

TABLES = [t.strip() for t in os.environ.get('ASDA_TABLES', 'item_list,item_img_table').split(',') if t.strip()]

# что показать после импорта: таблица -> (колонка-ключ, [значения ключа], [колонки для печати])
PROBES = {
    'item_list': ('ItemCode',
                  [20551, 20917, 20625, 23317, 22705, 20626],
                  ['ItemName', 'AttackRangeTiles']),
    'item_img_table': ('_row',
                       [0, 1, 2],
                       ['MeshMale', 'IconAtlasMale', 'IconCellMale', 'IconSmallFlag', 'DropSoundCat']),
}


def log(m):  unreal.log('[itemdb] ' + m)
def err(m):  unreal.log_error('[itemdb] ' + m)


def _gen():
    """Единственный переимпортёр item-БД `gen_itemdb_datatables` (★ дедуп №2 В2 Т9); импорт при вызове."""
    import sys
    here = os.path.dirname(os.path.abspath(__file__))
    if here not in sys.path:
        sys.path.append(here)
    import gen_itemdb_datatables
    return gen_itemdb_datatables


def reimport(info):
    """★ дедуп №2 В2 Т9: тело импорта — общее `gen_itemdb_datatables.import_table` (гейт слоя имён, `_data_hex`, маркер
    ASDA_WRITES_ASSET, сохранение только этого ассета). ★ Теперь и проверка «CSV есть» до импорта."""
    return _gen().import_table(info, log=log, warn=log, err=err)


def probe(table, dt):
    """Печать нескольких строк по ключу + сводка «сколько имён -»."""
    out = {}
    key_col, keys, cols = PROBES.get(table, (None, [], []))
    names = unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)
    out['rows'] = len(names)
    log('%s: строк %d' % (table, len(names)))
    if not key_col:
        return out

    if key_col == '_row':
        key_vals = [str(n) for n in names]
    else:
        key_vals = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, key_col)
    data = {c: unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, c) for c in cols}

    want = set(str(k) for k in keys)
    out['probes'] = {}
    for i, kv in enumerate(key_vals):
        if kv in want:
            row = {c: data[c][i] for c in cols}
            out['probes'][kv] = row
            log('  %s=%s -> %s' % (key_col, kv, row))

    if table == 'item_list':
        dash = sum(1 for v in data['ItemName'] if (v or '').strip() in ('-', ''))
        out['dash'] = dash
        log('  строк без имени («-»): %d' % dash)
    return out


def main():
    with io.open(MANIFEST, 'r', encoding='utf-8') as f:
        manifest = {i['table']: i for i in json.load(f)}

    report = {}
    for table in TABLES:
        info = manifest.get(table)
        if info is None:
            err('нет таблицы %s в манифесте' % table)
            continue
        log('переимпорт %s -> %s' % (info['csv'], info['dt_asset']))
        dt = reimport(info)
        if dt is None:
            continue
        report[table] = probe(table, dt)

    with io.open(REPORT, 'w', encoding='utf-8') as f:
        f.write(json.dumps(report, ensure_ascii=False, indent=1))
    log('отчёт: %s' % REPORT)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
