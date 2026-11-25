# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
_verify_itemdb_union_lookup.py — headless UE5 Python: проверка, что ПЕРЕИМПОРТИРОВАННЫЕ ассеты
DataTable отдают новые записи ровно по той адресации, которую использует `UAsdaItemTableSubsystem`.

Повторяются 1:1 три пути из `Private/Items/AsdaItemTableSubsystem.cpp`:
  • `BuildItemCacheFromDataTables` — RowName -> индекс массива, ItemCode -> индекс (CodeToIndex);
  • `FindPackageRow` / `IsPackageItem` — subType 200/201 + строка `packagetable` по
    `DurationOrEnchant`, с самопроверкой `Row.Index == PackageIndex` (серверный отказ 9);
  • `FindBoosterRow` / `IsBoosterItem` — быстрый путь RowName == Code-1, резерв — линейный поиск.

Ничего не пишет в ассеты. Побочных эффектов при импорте модуля нет.
"""
import io
import json
import os
import csv

import unreal

CSV_DIR = r'@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Data\ItemDB'
# ★ Манифест вне Content (иначе редактор предлагает импортировать его как DataTable).
OVERLAY = r'@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\ItemDB\_server_union_overlay.csv'
OUT     = r'@@ASTORY_DEV_ROOT@@\Reports\dt_reimport_union_lookup.json'
BASE    = '/Game/AsdaAssets/Data/ItemDB/'


def log(m): unreal.log('[dt-lookup] ' + m)
def err(m): unreal.log_error('[dt-lookup] ' + m)


def cols(asset, names):
    dt = unreal.DataTable.cast(unreal.load_object(None, BASE + asset + '.' + asset))
    rows = [str(n) for n in unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)]
    out = {'_row': rows}
    for c in names:
        out[c] = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, c)
    return out


def main():
    it = cols('DT_item_list', ['ItemCode', 'ItemType', 'ItemSubType', 'DurationOrEnchant', 'ItemName'])
    bo = cols('DT_booster_list', ['Code', 'Slot00ItemCode', 'Slot05ItemCode', 'Slot34ItemCode',
                                  'Slot00Weight', 'Slot05Weight'])
    pk = cols('DT_packagetable', ['Index'])

    # ── как BuildItemCacheFromDataTables ─────────────────────────────────────
    items = {}                      # индекс строки -> запись
    code2idx = {}
    for i, rn in enumerate(it['_row']):
        idx = int(rn)
        rec = {'code': int(it['ItemCode'][i]), 'type': int(it['ItemType'][i]),
               'sub': int(it['ItemSubType'][i]), 'dur': int(it['DurationOrEnchant'][i]),
               'name': it['ItemName'][i]}
        items[idx] = rec
        code2idx[rec['code']] = idx

    # ── как FindBoosterRow ───────────────────────────────────────────────────
    boo_by_row = {int(rn): int(bo['Code'][i]) for i, rn in enumerate(bo['_row'])}
    boo_slots = {int(bo['Code'][i]): (bo['Slot00ItemCode'][i], bo['Slot05ItemCode'][i],
                                      bo['Slot34ItemCode'][i], bo['Slot00Weight'][i],
                                      bo['Slot05Weight'][i])
                 for i, rn in enumerate(bo['_row'])}

    def find_booster(code):
        if code <= 0:
            return None
        if boo_by_row.get(code - 1) == code:        # быстрый путь
            return code - 1
        for rn, c in boo_by_row.items():            # резерв — линейный
            if c == code:
                return rn
        return None

    # ── как FindPackageRow ───────────────────────────────────────────────────
    pkg_by_row = {int(rn): int(pk['Index'][i]) for i, rn in enumerate(pk['_row'])}

    def find_package(idx):
        return idx if (idx >= 0 and pkg_by_row.get(idx) == idx) else None

    res = {'items': len(items), 'booster_rows': len(boo_by_row), 'package_rows': len(pkg_by_row)}

    # ── 1. 316 предметов из оверлея: подсистема их видит и отдаёт серверные поля ──
    ov = []
    with io.open(OVERLAY, 'r', encoding='utf-8', newline='') as f:
        for r in csv.DictReader(f):
            if r['table'] == 'item_list':
                ov.append(r)
    keys = sorted({int(r['key']) for r in ov})
    miss, wrong, as_pkg, pkg_bad = [], [], 0, []
    for k in keys:
        idx = code2idx.get(k)
        if idx is None:
            miss.append(k)
            continue
        rec = items[idx]
        for r in ov:
            if int(r['key']) != k:
                continue
            fld = {'ItemType': 'type', 'ItemSubType': 'sub', 'DurationOrEnchant': 'dur'}[r['field']]
            if str(rec[fld]) != r['server_applied']:
                wrong.append('%d.%s=%s ждали %s' % (k, r['field'], rec[fld], r['server_applied']))
        if rec['sub'] in (200, 201):
            if find_package(rec['dur']) is not None:
                as_pkg += 1
            else:
                pkg_bad.append({'code': k, 'dur': rec['dur']})
    res['overlay_items'] = len(keys)
    res['overlay_items_missing'] = miss
    res['overlay_items_wrong'] = wrong[:20]
    res['overlay_items_wrong_total'] = len(wrong)
    res['overlay_items_resolve_as_package'] = as_pkg
    res['overlay_items_package_row_missing'] = pkg_bad[:10]
    res['overlay_items_package_row_missing_total'] = len(pkg_bad)
    res['overlay_sample'] = [{'code': k, **items[code2idx[k]]} for k in keys[:5]]

    # ── 2. Новые строки гачи 4135…4300: находятся и на них ссылаются предметы ──
    new_codes = list(range(4135, 4301))
    found = [c for c in new_codes if find_booster(c) is not None]
    res['new_gacha_codes'] = len(new_codes)
    res['new_gacha_found'] = len(found)
    res['new_gacha_missing'] = [c for c in new_codes if c not in found][:10]

    refs = []
    for idx, rec in items.items():
        if rec['sub'] in (200, 201):
            continue                                   # коробка адресует ДРУГУЮ таблицу
        if 4135 <= rec['dur'] <= 4300 and find_booster(rec['dur']) is not None:
            refs.append({'item': rec['code'], 'name': rec['name'], 'gacha': rec['dur'],
                         'slot00': boo_slots[rec['dur']][0], 'slot05': boo_slots[rec['dur']][1],
                         'w00': boo_slots[rec['dur']][3]})
    refs.sort(key=lambda r: r['item'])
    res['items_pointing_to_new_gacha'] = len(refs)
    res['items_pointing_to_new_gacha_sample'] = refs[:10]

    # раньше (до union) этих строк не было — фиксируем, что теперь IsBoosterItem = true
    res['was_broken_before'] = 'до переимпорта DT_booster_list обрывался на 4134 -> FindBoosterRow вернул бы nullptr'

    # ── 3. Границы: старые строки гачи по-прежнему находятся ─────────────────
    res['old_gacha_probe'] = {str(c): find_booster(c) for c in (1, 2, 4133, 4134)}

    with io.open(OUT, 'w', encoding='utf-8') as f:
        f.write(json.dumps(res, ensure_ascii=False, indent=1))
    log('items=%d booster=%d package=%d | оверлей: нет %d, неверных %d | новых кодов гачи найдено %d/%d | ссылок на них %d'
        % (len(items), len(boo_by_row), len(pkg_by_row), len(miss), len(wrong),
           len(found), len(new_codes), len(refs)))
    log('отчёт: %s' % OUT)


if __name__ == '__main__':
    main()
