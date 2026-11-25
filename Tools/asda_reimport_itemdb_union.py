# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
asda_reimport_itemdb_union.py — headless UE5 Python: переимпорт ДВУХ таблиц item-БД
(`DT_item_list`, `DT_booster_list`) из правленых CSV + полная сверка результата.

Зачем: после объединения клиентских и серверных таблиц коробок/гачи
(`Reports/server_package_tables_sync.md`) CSV порта изменились:
  • `item_list.csv`     — 316 записей получили серверные ItemType/ItemSubType/DurationOrEnchant
                          (+ синхронно пересчитанный DataHex), число строк НЕ изменилось;
  • `booster_list.csv`  — дописано 166 строк гачи (id 4135…4300): 4134 -> 4300.
Манифест перенесённого — `RawAssets/ItemDB/_server_union_overlay.csv`.

Что делает скрипт:
  1) снимает состояние ассетов ДО импорта (число строк + ключевые колонки);
  2) переимпортирует ОБА DataTable из CSV (тот же CSVImportFactory, что и в остальных
     скриптах пайплайна — ассеты не пересоздаются с нуля, схема строки прежняя);
  3) снимает состояние ПОСЛЕ и сверяет:
       • DT == CSV по ВСЕМ колонкам (включая сырой DataHex);
       • имена строк (RowName) == колонка `_row` CSV, без дыр 0…N-1;
       • дельта ДО→ПОСЛЕ совпадает с ожиданиями из `_server_union_overlay.csv`
         (ничего не изменилось сверх перечисленного);
       • повторяет ту же адресацию, что делает `UAsdaItemTableSubsystem`
         (RowName->индекс, ItemCode->индекс, быстрый путь гачи RowName == Code-1).

⚠ Колонка-ключ CSV называется `_row` и НЕ переименовывается: импортёр UE берёт первую
   колонку как имя строки независимо от её заголовка. Переименовывается только `_data_hex`
   -> `DataHex` (как во всех скриптах пайплайна) — это ОТДЕЛЬНАЯ колонка данных.

Побочных эффектов при импорте модуля нет (`tools-no-import-side-effects`).

Запуск (без run_headless.bat — там pause):
  UnrealEditor-Cmd.exe AsdaReborn.uproject -ExecutePythonScript="...\asda_reimport_itemdb_union.py"
    -unattended -nopause -nosplash -nosound -NullRHI -stdout -abslog=...
"""
import io
import json
import os
import csv
import sys

import unreal

PROJECT  = r'@@ASTORY_DEV_ROOT@@\AstoryClient'
MANIFEST = os.path.join(PROJECT, 'Tools', '_out', 'itemdb_manifest.json')
CSV_DIR  = os.path.join(PROJECT, 'RawAssets', 'Data', 'ItemDB')
TMP_DIR  = os.path.join(PROJECT, 'Tools', '_out', 'dt_csv')
DST_DIR  = '/Game/AsdaAssets/Data/ItemDB'
# ★ Манифест лежит ВНЕ Content: любой .csv внутри Content движок предлагает импортировать как
#   DataTable и просит структуру строки — а это не таблица игры, а наша ведомость переноса.
OVERLAY  = os.path.join(PROJECT, 'RawAssets', 'ItemDB', '_server_union_overlay.csv')
OUT_JSON = r'@@ASTORY_DEV_ROOT@@\Reports\dt_reimport_union.json'

TABLES = ['item_list', 'booster_list']

# Колонки, по которым снимается снимок «до» (диффом с «после» ловим лишние правки).
SNAP_COLS = {
    'item_list':    ['ItemCode', 'ItemType', 'ItemSubType', 'DurationOrEnchant', 'DataHex'],
    'booster_list': ['Code', 'DataHex'],
}


def log(m):  unreal.log('[dt-union] ' + m)
def warn(m): unreal.log_warning('[dt-union] ' + m)
def err(m):  unreal.log_error('[dt-union] ' + m)


def _gen():
    """Единственный переимпортёр item-БД `gen_itemdb_datatables` (★ дедуп №2 В2 Т9); импорт при вызове."""
    import sys
    here = os.path.dirname(os.path.abspath(__file__))
    if here not in sys.path:
        sys.path.append(here)
    import gen_itemdb_datatables
    return gen_itemdb_datatables


def read_csv_rows(path):
    """CSV -> (заголовок, {_row: {колонка: значение}}). Порядок колонок сохраняется."""
    csv.field_size_limit(1 << 30)
    with io.open(path, 'r', encoding='utf-8', newline='') as f:
        rd = csv.reader(f)
        head = next(rd)
        out = {}
        for row in rd:
            out[row[0]] = dict(zip(head[1:], row[1:]))
    return head, out


def snapshot(dt, cols):
    """{RowName: {колонка: строковое значение}} по ассету DataTable."""
    names = [str(n) for n in unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)]
    data = {}
    for c in cols:
        vals = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, c)
        if len(vals) != len(names):
            err('колонка %s: длина %d != числа строк %d' % (c, len(vals), len(names)))
            return names, None
        for n, v in zip(names, vals):
            data.setdefault(n, {})[c] = v
    return names, data


def import_table(info, at):
    """★ дедуп №2 В2 Т9: тело импорта — общее `gen_itemdb_datatables.import_table` (гейт слоя имён, CSV есть,
    `_data_hex`, маркер ASDA_WRITES_ASSET, сохранение только этого ассета); полная сверка «ДО/ПОСЛЕ» ниже — своя."""
    return _gen().import_table(info, at, log=log, warn=warn, err=err)


def read_overlay():
    """_server_union_overlay.csv -> {table: {key: {field: (было, стало)}}}"""
    out = {}
    if not os.path.isfile(OVERLAY):
        warn('нет манифеста оверлея %s' % OVERLAY)
        return out
    with io.open(OVERLAY, 'r', encoding='utf-8', newline='') as f:
        for r in csv.DictReader(f):
            out.setdefault(r['table'], {}).setdefault(r['key'], {})[r['field']] = \
                (r['client_original'], r['server_applied'])
    return out


def main():
    with io.open(MANIFEST, 'r', encoding='utf-8') as f:
        by_name = {i['table']: i for i in json.load(f)}
    at = unreal.AssetToolsHelpers.get_asset_tools()
    overlay = read_overlay()
    report = {'tables': {}, 'overlay_rows': {k: len(v) for k, v in overlay.items()}}

    for tname in TABLES:
        info = by_name[tname]
        res = {'table': tname, 'csv': info['csv'], 'dt': info['dt_asset'], 'problems': []}
        P = res['problems'].append

        # ── ДО ────────────────────────────────────────────────────────────────
        obj = unreal.load_object(None, info['dt_path'] + '.' + info['dt_asset'])
        before = None
        if obj is None:
            P('ассет %s не найден ДО импорта' % info['dt_path'])
            res['rows_before'] = None
        else:
            bnames, before = snapshot(unreal.DataTable.cast(obj), SNAP_COLS[tname])
            res['rows_before'] = len(bnames)
        log('%s: строк ДО = %s' % (tname, res['rows_before']))

        # ── ИМПОРТ ────────────────────────────────────────────────────────────
        dt = import_table(info, at)
        if dt is None:
            P('импорт не выполнен')
            report['tables'][tname] = res
            continue

        # ── ПОСЛЕ: сверка с CSV по ВСЕМ колонкам ──────────────────────────────
        head, csv_rows = read_csv_rows(os.path.join(CSV_DIR, info['csv']))
        res['csv_rows'] = len(csv_rows)
        res['csv_key_column'] = head[0]
        if head[0] != '_row':
            P('первая колонка CSV = %r, ожидалась _row' % head[0])

        cols = [c for c in head[1:]]
        anames = [str(n) for n in unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)]
        res['rows_after'] = len(anames)
        log('%s: строк ПОСЛЕ = %d, строк в CSV = %d' % (tname, len(anames), len(csv_rows)))

        if len(anames) != len(csv_rows):
            P('число строк DT (%d) != CSV (%d)' % (len(anames), len(csv_rows)))

        # имена строк: множество и «плотность» 0..N-1
        set_dt, set_csv = set(anames), set(csv_rows)
        res['rownames_missing_in_dt'] = sorted(set_csv - set_dt)[:20]
        res['rownames_extra_in_dt'] = sorted(set_dt - set_csv)[:20]
        if set_dt != set_csv:
            P('имена строк DT != ключам CSV (нет в DT: %d, лишних в DT: %d)'
              % (len(set_csv - set_dt), len(set_dt - set_csv)))
        try:
            ints = sorted(int(n) for n in anames)
            res['rowname_range'] = [ints[0], ints[-1]]
            if ints != list(range(len(ints))):
                P('имена строк не образуют сплошной 0..N-1 (это ломает индексацию подсистемы)')
        except ValueError:
            P('среди имён строк есть нечисловые — подсистема их пропустит')

        # значения по всем колонкам
        diff_by_col = {}
        samples = {}
        for c in cols:
            vals = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, c)
            if len(vals) != len(anames):
                P('колонка %s: длина %d != %d' % (c, len(vals), len(anames)))
                continue
            bad = 0
            for n, v in zip(anames, vals):
                ref = csv_rows.get(n, {}).get(c)
                if ref is None or ref != v:
                    bad += 1
                    if c not in samples:
                        samples[c] = {'row': n, 'dt': v[:80], 'csv': (ref or '')[:80]}
            if bad:
                diff_by_col[c] = bad
        res['csv_mismatch_by_column'] = diff_by_col
        res['csv_mismatch_samples'] = samples
        if diff_by_col:
            P('DT != CSV: %s' % json.dumps(diff_by_col, ensure_ascii=False))

        # ── дельта ДО→ПОСЛЕ ───────────────────────────────────────────────────
        _, after = snapshot(dt, SNAP_COLS[tname])
        if before is not None and after is not None:
            added = sorted(set(after) - set(before), key=lambda s: int(s) if s.isdigit() else 0)
            removed = sorted(set(before) - set(after))
            changed = {}
            for n in set(before) & set(after):
                d = {c: [before[n][c], after[n][c]]
                     for c in SNAP_COLS[tname] if before[n][c] != after[n][c]}
                if d:
                    changed[n] = d
            res['rows_added'] = len(added)
            res['rows_added_first_last'] = [added[0], added[-1]] if added else []
            res['rows_removed'] = len(removed)
            res['rows_changed'] = len(changed)

            # сверка с манифестом оверлея
            ov = overlay.get(tname, {})
            if tname == 'item_list':
                # ключ оверлея — ItemCode; строим ItemCode -> RowName
                code2row = {}
                for n in after:
                    code2row.setdefault(after[n]['ItemCode'], n)
                expected_rows = set()
                unexpected = []
                for key, fields in ov.items():
                    rn = code2row.get(key)
                    if rn is None:
                        unexpected.append('ItemCode %s из оверлея НЕ найден в DT' % key)
                        continue
                    expected_rows.add(rn)
                    for fld, (was, now) in fields.items():
                        got = after[rn].get(fld)
                        if got != now:
                            unexpected.append('ItemCode %s.%s: в DT %s, оверлей ждёт %s'
                                              % (key, fld, got, now))
                        wasdt = before.get(rn, {}).get(fld)
                        if wasdt is not None and wasdt != was:
                            unexpected.append('ItemCode %s.%s: ДО импорта было %s, оверлей писал %s'
                                              % (key, fld, wasdt, was))
                res['overlay_keys'] = len(ov)
                res['overlay_problems'] = unexpected[:40]
                res['overlay_problems_total'] = len(unexpected)
                extra = sorted(set(changed) - expected_rows)
                res['changed_outside_overlay'] = len(extra)
                res['changed_outside_overlay_sample'] = [
                    {'row': n, 'diff': changed[n]} for n in extra[:10]]
                if extra:
                    P('изменились строки, которых нет в оверлее: %d' % len(extra))
                if unexpected:
                    P('расхождения с манифестом оверлея: %d' % len(unexpected))
                if added or removed:
                    P('item_list: число строк должно было остаться прежним (добавлено %d, удалено %d)'
                      % (len(added), len(removed)))
            else:
                # booster_list: старые строки обязаны остаться нетронутыми
                if changed:
                    P('booster_list: изменились существующие строки (%d), ожидалось только добавление'
                      % len(changed))
                    res['changed_sample'] = {n: changed[n] for n in list(changed)[:10]}
                if removed:
                    P('booster_list: пропали строки (%d)' % len(removed))
                if len(added) != len(ov):
                    P('booster_list: добавлено %d строк, оверлей описывает %d'
                      % (len(added), len(ov)))
                # коды добавленных строк должны совпасть с ключами оверлея
                added_codes = sorted(int(after[n]['Code']) for n in added) if added else []
                ov_codes = sorted(int(k) for k in ov) if ov else []
                res['added_codes_first_last'] = [added_codes[0], added_codes[-1]] if added_codes else []
                if added_codes != ov_codes:
                    P('booster_list: коды добавленных строк != ключам оверлея')

        # ── адресация как в UAsdaItemTableSubsystem ───────────────────────────
        if tname == 'item_list':
            codes = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, 'ItemCode')
            code2idx = {}
            dup = 0
            for n, c in zip(anames, codes):
                ci = int(c)
                if ci in code2idx:
                    dup += 1
                code2idx[ci] = int(n)
            res['subsystem_codes'] = len(code2idx)
            res['subsystem_dup_codes'] = dup
            res['subsystem_code_first_last'] = [
                int(codes[anames.index('0')]),
                int(codes[anames.index(str(len(anames) - 1))])]
            probe = {}
            for key in sorted(overlay.get('item_list', {}), key=lambda s: int(s))[:8]:
                idx = code2idx.get(int(key))
                if idx is None or after is None:
                    probe[key] = 'НЕТ ЗАПИСИ'
                else:
                    rn = str(idx)
                    probe[key] = {'row': rn,
                                  'ItemType': after[rn]['ItemType'],
                                  'ItemSubType': after[rn]['ItemSubType'],
                                  'DurationOrEnchant': after[rn]['DurationOrEnchant']}
            res['subsystem_probe_items'] = probe
        else:
            # быстрый путь FindBoosterRow: RowName == Code-1
            codes = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, 'Code')
            bad_fast = 0
            for n, c in zip(anames, codes):
                if int(c) - 1 != int(n):
                    bad_fast += 1
            res['booster_fastpath_bad'] = bad_fast
            if bad_fast:
                P('быстрый путь FindBoosterRow (RowName == Code-1) не выполняется у %d строк'
                  % bad_fast)
            n2c = dict(zip(anames, codes))
            res['booster_probe'] = {c: ('есть строка _row=%d' % (int(c) - 1))
                                    if n2c.get(str(int(c) - 1)) == c else 'НЕ НАЙДЕНА'
                                    for c in ['1', '4134', '4135', '4200', '4300']}

        report['tables'][tname] = res
        log('%s: проблем %d' % (tname, len(res['problems'])))
        for p in res['problems']:
            err('%s: %s' % (tname, p))

    with io.open(OUT_JSON, 'w', encoding='utf-8') as f:
        f.write(json.dumps(report, ensure_ascii=False, indent=1))
    log('отчёт: %s' % OUT_JSON)
    total = sum(len(t.get('problems', [])) for t in report['tables'].values())
    log('ИТОГ: проблем всего %d' % total)


if __name__ == '__main__':
    main()
