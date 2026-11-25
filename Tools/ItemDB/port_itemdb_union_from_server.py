#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
port_itemdb_union_from_server.py — ОБРАТНОЕ направление union: переносит в клиентские данные ПОРТА
то, что есть только на сервере.

★★★ УСТАРЕЛ 2026-09-25 (волна tablesA). Обе его правки переведены на ВЕДОМОСТИ, которые накладывает
    сам `Tools/decode_item_db.py` на каждом декоде (переживают перегенерацию, в отличие от правки CSV
    этим скриптом — она пропадала, как цены причёсок 31.08):
      • 316 коробок item_list  -> `RawAssets/ItemDB/_value_overlay/item_list.csv`  (ключ реестра value_ledger);
      • 166 строк гачи booster_list -> `RawAssets/ItemDB/_tail_append/booster_list.csv` (tail_ledger).
    Обе ведомости строит `Tools/build_itemdb_ledgers.py` (booster — без ключа, item_list — с `--values`).
    Запускать этот скрипт больше НЕ НУЖНО; оставлен как справка о происхождении и генератор ведомости
    `_server_union_overlay.csv` (список кодов коробок, из которого build_itemdb_ledgers берёт 316 ключей).

Правило пользователя: клиент авторитетен по НАПОЛНЕНИЮ коробок, но серверные записи, которых в
клиенте нет, не выбрасываются — они добавляются в клиентскую сторону, чтобы обе стороны пришли к
одному списку. Ничего не выдумывается: переносится только то, что физически лежит в серверных
таблицах.

Что переносится (снимок живого сервера в RE/dumps/server_tables_fix/live_orig):
  1) 316 предметов, которые СЕРВЕР считает коробками (item_table col16==11, col17 ∈ {200,201}),
     а клиентский item_list — нет. В порту им проставляются ItemType/ItemSubType/DurationOrEnchant
     из серверной таблицы (правится и колонка DataHex, чтобы запись оставалась 1:1 восстановимой).
  2) 166 строк гачи GachaItemRateTable.txt (id 4135..4300), которых нет в клиентском booster_list
     (у клиента 1..4134). Они нужны буквально: 5 клиентских предметов ссылаются на строки 4135..4138,
     то есть клиентская таблица неполна.

Строки ПАКЕТОВ переносить не нужно: серверная PackageItem_Table (0..4656) целиком вложена в
клиентскую packagetable (0..5227), строк «только на сервере» нет вовсе.

Файлы правятся ТОЛЬКО в данных порта:
  AstoryClient/RawAssets/Data/ItemDB/item_list.csv
  AstoryClient/RawAssets/Data/ItemDB/booster_list.csv
Оригинальные клиентские .bin (Donors/TRANSLATE_unpacked/tables_orig) НЕ трогаются.

Рядом пишется манифест _server_union_overlay.csv — перечень всех перенесённых записей, чтобы
следующая регенерация CSV из Bin.bbz (Tools/decode_item_db.py) не выбросила их молча: после
регенерации достаточно повторно запустить этот скрипт.

ПОСЛЕ правки CSV нужен переимпорт DataTable в UE (как и для остальных таблиц ItemDB):
  run_headless.bat AstoryClient\Tools\asda_reimport_item_list.py
Скрипт UE не запускает.

Запуск:
  python Tools/port_itemdb_union_from_server.py [--dry]
"""
import os, sys, csv, struct, argparse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import server_package_tables_sync as S

ROOT = S.ROOT
ITEMDB = os.path.join(ROOT, "AstoryClient", 'RawAssets', 'Data', 'ItemDB')
IL_CSV = os.path.join(ITEMDB, 'item_list.csv')
BL_CSV = os.path.join(ITEMDB, 'booster_list.csv')
# ★ Манифест кладём ВНЕ Content: .csv внутри Content движок принимает за таблицу и просит
#   структуру строки при импорте. Это ведомость переноса, а не игровые данные.
MANIFEST = os.path.join(ROOT, "AstoryClient", 'RawAssets', 'ItemDB', '_server_union_overlay.csv')

# смещения в расшифрованной записи item_list (248 б), они же — раскладка колонки DataHex
OFF_TYPE, OFF_SUB, OFF_DUR = 128, 132, 216


def collect():
    """Собирает то, что есть только на сервере. Возвращает (правки item_list, новые строки гачи)."""
    cl = S.client_item_list()
    _, sit = S.server_item_table()
    srv = {S.atol(r[S.COL_CODE]): r for r in sit}
    _, cboost = S.client_booster()
    _, sgacha = S.server_gacha()

    # 1) серверные коробки, которых клиент коробками не считает
    fixes = {}
    for code, r in srv.items():
        st, ss, sidx = S.atol(r[S.COL_TYPE]), S.atol(r[S.COL_SUB]), S.atol(r[S.COL_PKG])
        if not (st == 11 and ss in (200, 201)):
            continue
        v = cl.get(code)
        if v is None:
            continue  # кода нет в клиентском item_list — добавить запись нечем, см. отчёт
        if v['ItemType'] == 11 and v['ItemSubType'] in (200, 201):
            # обе стороны считают предмет коробкой -> клиент авторитетен по наполнению,
            # расхождение индекса уже устранено НА СЕРВЕРЕ (server_package_tables_sync.py).
            # Клиентскую запись здесь трогать нельзя — иначе затрём клиентский авторитет.
            continue
        fixes[code] = (st, ss, sidx, v['ItemType'], v['ItemSubType'], v['DurationOrEnchant'])

    # 2) строки гачи, которых нет у клиента
    new_gacha = {rid: pairs for rid, pairs in sgacha.items() if rid not in cboost}
    return fixes, new_gacha


def patch_item_list(fixes, dry):
    """Проставляет ItemType/ItemSubType/DurationOrEnchant и синхронно правит DataHex."""
    with open(IL_CSV, 'r', encoding='utf-8', newline='') as f:
        rows = list(csv.reader(f))
    head = rows[0]
    iCode, iT, iS, iD, iH = (head.index(x) for x in
                             ('ItemCode', 'ItemType', 'ItemSubType', 'DurationOrEnchant', 'DataHex'))
    done = 0
    for r in rows[1:]:
        code = int(r[iCode])
        if code not in fixes:
            continue
        st, ss, sidx, _ct, _cs, _cd = fixes[code]
        r[iT], r[iS], r[iD] = str(st), str(ss), str(sidx)
        b = bytearray.fromhex(r[iH])
        struct.pack_into('<i', b, OFF_TYPE, st)
        struct.pack_into('<i', b, OFF_SUB, ss)
        struct.pack_into('<i', b, OFF_DUR, sidx)
        r[iH] = b.hex()
        done += 1
    if not dry:
        with open(IL_CSV, 'w', encoding='utf-8', newline='') as f:
            csv.writer(f, lineterminator='\n').writerows(rows)
    return done, len(rows) - 1


def patch_booster(new_gacha, dry):
    """Дописывает недостающие строки гачи в booster_list.csv (с корректным DataHex 284 б)."""
    with open(BL_CSV, 'r', encoding='utf-8', newline='') as f:
        rows = list(csv.reader(f))
    head = rows[0]
    iRow, iCode, iH = head.index('_row'), head.index('Code'), head.index('DataHex')
    have = {int(r[iCode]) for r in rows[1:]}
    nrow = len(rows) - 1
    added = 0
    for rid in sorted(new_gacha):
        if rid in have:
            continue
        pairs = new_gacha[rid]
        b = bytearray(284)
        struct.pack_into('<i', b, 0, rid)
        for k, (c, w) in enumerate(pairs):
            struct.pack_into('<i', b, 4 + 8 * k, c)
            struct.pack_into('<i', b, 8 + 8 * k, w)
        rec = [''] * len(head)
        rec[iRow] = str(nrow)
        rec[iCode] = str(rid)
        for k, (c, w) in enumerate(pairs):
            rec[head.index('Slot%02dItemCode' % k)] = str(c)
            rec[head.index('Slot%02dWeight' % k)] = str(w)
        rec[iH] = b.hex()
        rows.append(rec)
        nrow += 1
        added += 1
    if not dry:
        with open(BL_CSV, 'w', encoding='utf-8', newline='') as f:
            csv.writer(f, lineterminator='\n').writerows(rows)
    return added, nrow


def write_manifest(fixes, new_gacha, dry):
    """Манифест перенесённого — чтобы регенерация CSV из клиента не потеряла эти записи молча."""
    if dry:
        return
    with open(MANIFEST, 'w', encoding='utf-8', newline='') as f:
        w = csv.writer(f, lineterminator='\n')
        w.writerow(['table', 'key', 'field', 'client_original', 'server_applied'])
        for code in sorted(fixes):
            st, ss, sidx, ct, cs, cd = fixes[code]
            w.writerow(['item_list', code, 'ItemType', ct, st])
            w.writerow(['item_list', code, 'ItemSubType', cs, ss])
            w.writerow(['item_list', code, 'DurationOrEnchant', cd, sidx])
        for rid in sorted(new_gacha):
            w.writerow(['booster_list', rid, 'ROW', '(отсутствовала)', 'GachaItemRateTable.txt'])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dry', action='store_true', help='ничего не писать, только показать числа')
    args = ap.parse_args()
    fixes, new_gacha = collect()
    n1, tot1 = patch_item_list(fixes, args.dry)
    n2, tot2 = patch_booster(new_gacha, args.dry)
    write_manifest(fixes, new_gacha, args.dry)
    print('item_list.csv    : исправлено %d записей из %d (серверные коробки, которых клиент не знал)'
          % (n1, tot1))
    print('booster_list.csv : добавлено %d строк, стало %d (строки гачи, которых у клиента не было)'
          % (n2, tot2))
    print('манифест         : %s' % MANIFEST)
    if args.dry:
        print('(--dry: файлы не изменены)')


if __name__ == '__main__':
    main()
