# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
gen_itemdb_datatables.py — headless UE5 Python: ЕДИНСТВЕННЫЙ переимпортёр CSV item-БД → DataTable
/Game/AsdaAssets/Data/ItemDB/DT_<table> (RowStruct из манифеста Tools/_out/itemdb_manifest.json,
его создаёт gen_itemdb_structs.py).

★ Дедуп №2, волна В2, пункт Т9 (`Reports/dedup_audit2/04_tools_python.md` §Т9). До него переимпорт ItemDB жил в
  ПЯТИ инструментах от 27.09 (этот, `_import_one_itemdb_table.py`, `asda_reimport_item_list.py`,
  `asda_reimport_item_tables.py`, `asda_reimport_itemdb_union.py`) с четырьмя точными копиями `rewrite_header_hex` и
  разошедшимися правилами: маркер ASDA_WRITES_ASSET печатал только один из пяти; этот файл по умолчанию перезаливал ВСЕ
  52 таблицы и затем ПЕРЕСОХРАНЯЛ весь каталог ItemDB (`save_directory(..., only_if_is_dirty=False)` — задевал чужие
  ассеты, закон owner-asset-edits-never-lose). Теперь тело импорта ОДНО — `import_table()` ниже; остальные четыре —
  тонкие обёртки (свои проверки «после» у них остались), `rewrite_header_hex` — в `asda_dt_common`.

ВЫБОР ТАБЛИЦ (аргументов у -ExecutePythonScript нет — переменная окружения, как у `_import_one_itemdb_table.py`):
  ASDA_TABLES=item_list,item_img_table   — только перечисленные (имена `table` из манифеста);
  ASDA_TABLES=*                          — все таблицы манифеста (массовый переимпорт — только осознанно, с разрешения
                                           владельца: перезаливает ассеты целиком);
  не задано                              — ★ ИСПРАВЛЕНИЕ: ОТКАЗ (раньше — молча все 52 таблицы + пересохранение каталога).
  ASDA_DIFF_COLS=колонки через запятую   — снимок этих колонок ДО/ПОСЛЕ и сверка «после» с CSV (бывший гейт `_import_one`).
Для каждой таблицы: гейт слоя имён (`asda_name_layer_gate`) → RowStruct → CSV (`_data_hex` → `DataHex` во временной копии)
  → строка «ASDA_WRITES_ASSET: <путь>» (её проверяет `run_headless_safe.py --expect-asset` по дате файла) → импорт
  (`AssetImportTask` + `CSVImportFactory`, replace_existing — схема ассета прежняя) → сохранение ТОЛЬКО этого ассета.
Каталог целиком больше не пересохраняется.

Декод CSV — только `Tools/decode_item_db.py --out <папка>` (полный декод в боевой каталог запрещён, его отклоняет сам
  декодер); правки ItemDB — в ведомостях `_tail_append` / `_value_overlay` (законы itemdb-ledger-overlays,
  itemdb-full-decode-into-live-dir).

Запуск:
  $env:ASDA_TABLES='item_list'; python Tools\run_headless_safe.py Tools\gen_itemdb_datatables.py --marker "ИТОГО: импортировано"
Побочных эффектов при импорте модуля нет (закон tools-no-import-side-effects): работа — только в main().
"""
import io
import json
import os
import sys

import unreal

HERE        = os.path.dirname(os.path.abspath(__file__))
PROJECT     = os.path.dirname(HERE)
MANIFEST    = os.path.join(PROJECT, 'Tools', '_out', 'itemdb_manifest.json')
CSV_DIR     = os.path.join(PROJECT, 'RawAssets', 'Data', 'ItemDB')
TMP_DIR     = os.path.join(PROJECT, 'Tools', '_out', 'dt_csv')
DST_DIR     = '/Game/AsdaAssets/Data/ItemDB'


def log(m): unreal.log('[itemdb-dt] ' + m)
def warn(m): unreal.log_warning('[itemdb-dt] ' + m)
def err(m): unreal.log_error('[itemdb-dt] ' + m)


def _tools_on_path():
    """Каталог Tools в sys.path (при вызове, не при импорте модуля)."""
    if HERE not in sys.path:
        sys.path.append(HERE)


def name_gate(table, log=log, warn=warn, err=err):
    """★ 2026-09-27 (СРЕДН-1): общий гейт «слой имён применён полностью» перед импортом таблиц рецепта."""
    _tools_on_path()
    import asda_name_layer_gate as GATE          # без сайд-эффектов на импорте
    return GATE.name_layer_gate(table, CSV_DIR, log=log, warn=warn, err=err)


def rewrite_header_hex(src_csv, dst_csv):
    """Копия CSV с `_data_hex` → `DataHex` в первой строке — общая `asda_dt_common.rewrite_header_hex`."""
    _tools_on_path()
    import asda_dt_common as _DTC
    return _DTC.rewrite_header_hex(src_csv, dst_csv)


def load_manifest():
    """Манифест item-БД: {table: info} (info: table, csv, has_hex, script_path, struct, dt_asset, dt_path)."""
    with io.open(MANIFEST, 'r', encoding='utf-8') as f:
        return {i['table']: i for i in json.load(f)}


def selected_tables(manifest, default=None):
    """Имена таблиц из ASDA_TABLES ('*' — все); не задано → `default` (None — пусто, вызывающий отказывает)."""
    raw = os.environ.get('ASDA_TABLES', '')
    if raw.strip() == '*':
        return list(manifest)
    names = [t.strip() for t in raw.split(',') if t.strip()]
    if not names and default:
        names = list(default)
    return names


def snapshot(dt, cols):
    """{имя строки: (значения колонок cols как строки)} у DataTable."""
    names = [str(n) for n in unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)]
    per_col = [unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, c) for c in cols]
    return {nm: tuple(str(col[i]) for col in per_col) for i, nm in enumerate(names)}


def csv_rows(path, cols):
    """{_row: (значения cols)} из CSV порта — эталон для сверки «после импорта»."""
    import csv
    csv.field_size_limit(1 << 30)
    out = {}
    with io.open(path, 'r', encoding='utf-8', newline='') as f:
        for r in csv.DictReader(f):
            out[r['_row']] = tuple(r.get(c, '') for c in cols)
    return out


def import_table(info, at=None, diff_cols=(), log=log, warn=warn, err=err, check_src=True):
    """Переимпорт ОДНОЙ таблицы item-БД из её CSV. Возврат: DataTable или None (отказ/ошибка — в лог).

    diff_cols — снимок колонок ДО/ПОСЛЕ + сверка «после» с CSV (печать; ничего не отменяет — так было в `_import_one`).
    check_src — проверять наличие CSV до импорта (у `asda_reimport_item_tables` проверки не было; пропуск файла теперь
      даёт отказ с понятной строкой вместо исключения движка)."""
    table = info['table']
    if not name_gate(table, log=log, warn=warn, err=err):
        return None
    struct = unreal.load_object(None, info['script_path'])
    if struct is None:
        err('%s: RowStruct %s не найден (модуль не собран?)' % (table, info['script_path']))
        return None
    src = os.path.join(CSV_DIR, info['csv'])
    if check_src and not os.path.isfile(src):
        err('%s: нет CSV %s' % (table, src))
        return None
    csv_path = src
    if info['has_hex']:
        csv_path = os.path.join(TMP_DIR, info['csv'])
        rewrite_header_hex(src, csv_path)

    before = None
    if diff_cols:
        prev = unreal.load_object(None, info['dt_path'] + '.' + info['dt_asset'])
        if prev is not None:
            before = snapshot(unreal.DataTable.cast(prev), diff_cols)
            log('%s: снимок ДО: строк=%d колонки=%s' % (table, len(before), ','.join(diff_cols)))
        else:
            warn('%s: DataTable до импорта не найден — диффа ДО/ПОСЛЕ не будет' % table)

    factory = unreal.CSVImportFactory()
    settings = factory.get_editor_property('automated_import_settings')
    settings.set_editor_property('import_type', unreal.CSVImportType.ECSV_DATA_TABLE)
    settings.set_editor_property('import_row_struct', struct)

    # Объявление записываемого ассета: run_headless_safe.py проверит, что дата .uasset обновилась за прогон
    #   (сохранение молча не проходит, когда файл держит соседний -game — закон asset-save-blocked-by-running-game).
    log('ASDA_WRITES_ASSET: %s' % info['dt_path'])
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', csv_path)
    task.set_editor_property('destination_path', DST_DIR)
    task.set_editor_property('destination_name', info['dt_asset'])
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('automated', True)
    task.set_editor_property('save', True)
    task.set_editor_property('factory', factory)
    (at or unreal.AssetToolsHelpers.get_asset_tools()).import_asset_tasks([task])

    obj = unreal.load_object(None, info['dt_path'] + '.' + info['dt_asset'])
    if obj is None:
        err('%s: DataTable НЕ создан (%s)' % (table, info['dt_path']))
        return None
    dt = unreal.DataTable.cast(obj)
    # Сохраняется ТОЛЬКО этот ассет (задача уже сохранила его при save=True; повтор — без условия «грязный», как было у
    #   трёх обёрток). ★ Каталог целиком больше не пересохраняется (прежний `save_directory(DST, False, True)`).
    unreal.EditorAssetLibrary.save_loaded_asset(dt, False)
    nrows = len(unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)) if dt else 0
    log('%s: OK -> %s  rows=%d  struct=%s' % (table, info['dt_asset'], nrows, info['struct']))
    if diff_cols and dt:
        after = snapshot(dt, diff_cols)
        if before is not None:
            changed = [nm for nm in after if before.get(nm) != after[nm]]
            gone = [nm for nm in before if nm not in after]
            for nm in changed[:200]:
                log('  ДИФФ %s строка %s: %s -> %s' % (table, nm, before.get(nm), after[nm]))
            log('ДИФФ %s: изменено строк %d, исчезло %d, стало %d (было %d)' % (
                table, len(changed), len(gone), len(after), len(before)))
        ref = csv_rows(src, diff_cols)
        mism = [nm for nm in after if ref.get(nm) != after[nm]]
        log('СВЕРКА %s DT==CSV по %s: расхождений %d из %d' % (table, ','.join(diff_cols), len(mism), len(after)))
        for nm in mism[:20]:
            warn('  расхождение строка %s: DT=%s CSV=%s' % (nm, after[nm], ref.get(nm)))
    return dt


def run(tables, manifest=None, diff_cols=(), log=log, warn=warn, err=err):
    """Переимпорт списка таблиц. Возврат: (успешно, всего)."""
    manifest = manifest or load_manifest()
    at = unreal.AssetToolsHelpers.get_asset_tools()
    ok = 0
    for name in tables:
        info = manifest.get(name)
        if info is None:
            err('таблицы %s нет в манифесте' % name)
            continue
        try:
            if import_table(info, at, diff_cols=diff_cols, log=log, warn=warn, err=err):
                ok += 1
        except Exception as e:
            err('%s: EXCEPTION %s' % (name, e))
    return ok, len(tables)


def main():
    manifest = load_manifest()
    tables = selected_tables(manifest)
    if not tables:
        err('ОТКАЗ: не задан ASDA_TABLES (имена таблиц через запятую или * — все). Массовый переимпорт всех таблиц '
            'по умолчанию отключён (дедуп №2 В2 Т9): он перезаливает ассеты целиком.')
        return
    diff_cols = [c.strip() for c in os.environ.get('ASDA_DIFF_COLS', '').split(',') if c.strip()]
    ok, total = run(tables, manifest, diff_cols)
    log(f'ИТОГО: импортировано {ok} / {total}  (ошибок {total - ok})')


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
