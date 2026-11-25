#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
sync_gacha_linkword_from_server.py — LinkWordCode семейств призов ПРЕМИУМ-ГАЧИ в данных порта := живой сервер
(волна DATA 13.10.2026, вопрос P67; отчёт `Reports/anticheat/wave_DATA_20261013.md` §2).

ЗАЧЕМ. Порт выбирает вариант приза (окно 68500 → 6581) по семейству `LinkWordCode` (+220) записи-коробки
(`AsdaPremiumGachaSubsystem::CollectVariantCandidates`, эталон `itemFindVariantBySlotOption@0xB71840`).
Сервер (P67) пропускает 6581 только если LinkWord варианта == LinkWord приза по СВОЕЙ `Item_Table.txt` (кол. 40, +65).
`item_list` порта = RU/Interlude-линия клиента (сверено: LinkWord порта == эталон Interlude == RU_MINE, кроме
221 правки ведомости), а `Item_Table.txt` сервера = арабская линия (LinkWord == Arabic `Bin.bbz` во всех 55 301
общих кодах). Это НЕ дефект экспорта порта (DataHex+220 == колонке во всех 58 878 записях; кол. 39 сервера ==
клиентскому +216), а разные линии данных. Авторитет — сервер ⇒ семейства призов гачи в порту приводятся к серверным.

ЧТО МЕНЯЕТСЯ (минимальный набор, ничего не выдумывается — только значения кол. 40 живой таблицы):
  V = {LinkWord сервера у призов-коробок PrimiumGaChaTable, ≠ 0}.
  • код с LinkWord СЕРВЕРА ∈ V                      → порт := сервер (входит в серверное семейство приза);
  • код с LinkWord ПОРТА ∈ V, а сервера ∉ V          → порт := сервер (выходит из чужого семейства приза);
  • приз-коробка с LinkWord сервера 0               → порт := 0 (у сервера семейства нет — окна выбора нет).
  Итог: для КАЖДОГО приза-коробки семейство порта == семейству сервера (проверяется после правки).

КУДА ПИШЕТ (только с --write; бэкап в @@ASTORY_DEV_ROOT@@\_backup\<дата>_data\<путь> с проверкой размера):
  • ведомость `AstoryClient/RawAssets/ItemDB/_value_overlay/item_list.csv` (дописываются строки column=LinkWordCode,
    декодер `Tools/decode_item_db.py` накладывает их на каждом декоде — правка переживает перегенерацию);
  • `RawAssets/Data/ItemDB/item_list.csv` и `.json`: ячейка LinkWordCode и байты DataHex[220:222] (i16 LE);
  • задание точечной заливки DT для `AstoryClient/Tools/asda_ml_dt_point.py` (allow_cols LinkWordCode+DataHex,
    allow_rows — только изменённые строки, csv_old — бэкап): `<бэкап>/dt_point_item_list_linkword.json`.
  Ассет DT_item_list этот скрипт НЕ трогает (редактор не запускается).

Серверные файлы — ТОЛЬКО ЧТЕНИЕ. Закон `tools-no-import-side-effects`: при импорте ничего не выполняется.

Запуск:
  python Tools/sync_gacha_linkword_from_server.py                 # отчёт (сухой прогон)
  python Tools/sync_gacha_linkword_from_server.py --write         # бэкап + запись ведомости/CSV/JSON + задание DT
  python Tools/sync_gacha_linkword_from_server.py --check         # код 0, если правок не требуется (после записи)
  --table-dir DIR — снимок живых item_table.txt / PrimiumGaChaTable.txt вместо шары.
"""
import argparse
import collections
import csv
import datetime
import hashlib
import io
import json
import os
import shutil
import sys

csv.field_size_limit(1 << 30)

ROOT = r'@@ASTORY_DEV_ROOT@@'
SHARE = '//@@ASTORY_SERVER_HOSTNAME@@/h(@@ASTORY_SERVER_HOSTNAME@@_ns)/ASDA/Working_ASDA/v2/GameServer/ItemTable_Info/'   # скобки в имени обязательны
ITEMDB = os.path.join(ROOT, "AstoryClient", 'RawAssets', 'Data', 'ItemDB')
CSV_P = os.path.join(ITEMDB, 'item_list.csv')
JSON_P = os.path.join(ITEMDB, 'item_list.json')
LEDGER_P = os.path.join(ROOT, "AstoryClient", 'RawAssets', 'ItemDB', '_value_overlay', 'item_list.csv')
DT_PATH = '/Game/AsdaAssets/Data/ItemDB/DT_item_list'
COL_SUB, COL_LW = 17, 40          # Item_Table.txt: кол. 17 = +27 ItemSubType, кол. 40 = +65 LinkWord (sub_4A96A0)
LW_OFF = 220                      # ItemRecord +0xDC, i16 (реестр decode_item_db: (220, 'i16', 'LinkWordCode'))
REASON = ('P67 6581: семейство приза премиум-гачи = серверное (Item_Table кол.40); авторитет — сервер; '
          'порт RU/Interlude-линия, сервер — арабская (wave_DATA 13.10.2026)')


def md5(b):
    """MD5 байтов (для отчёта о снимке)."""
    return hashlib.md5(b).hexdigest()


def read_server(table_dir):
    """Живые item_table.txt и PrimiumGaChaTable.txt -> ({код: [42 int]}, [коды призов], {файл: md5})."""
    base = (table_dir.rstrip('/\\') + '/') if table_dir else SHARE
    ib = open(base + 'item_table.txt', 'rb').read()
    gb = open(base + 'PrimiumGaChaTable.txt', 'rb').read()
    srv = {}
    for ln in ib.decode('latin-1').split('\n')[1:]:
        p = ln.split()
        if len(p) < 42 or not p[0].isdigit():
            continue
        srv[int(p[0])] = [int(x) if x.lstrip('-').isdigit() else 0 for x in p[:42]]
    prizes = sorted({int(p[2]) for p in (ln.split() for ln in gb.decode('latin-1').split('\n')[1:])
                     if len(p) >= 3 and p[2].lstrip('-').isdigit()})
    return srv, prizes, {'item_table.txt': md5(ib), 'PrimiumGaChaTable.txt': md5(gb)}


def read_port():
    """item_list.csv порта -> (заголовок, строки, {код: строка})."""
    text = open(CSV_P, 'rb').read().decode('utf-8')
    rd = csv.DictReader(io.StringIO(text, newline=''))
    rows = list(rd)
    return rd.fieldnames, rows, {int(r['ItemCode']): r for r in rows}


def plan(srv, prizes, by_code):
    """Минимальный набор правок {код: (row, было, стало)} + проверка семейств после правки."""
    plw = {c: int(r['LinkWordCode'] or 0) & 0xFFFF for c, r in by_code.items()}
    slw = {c: v[COL_LW] & 0xFFFF for c, v in srv.items()}
    isbox = lambda c: int(by_code[c]['ItemSubType'] or 0) in (200, 201)
    pb = [c for c in prizes if c in by_code and isbox(c)]
    V = {slw[b] for b in pb if slw.get(b)}
    new = dict(plw)
    for c in by_code:
        s = slw.get(c, 0)
        if s in V or plw[c] in V:
            new[c] = s
    for b in pb:
        if not slw.get(b):
            new[b] = 0
    chg = {c: (int(by_code[c]['_row']), plw[c], new[c]) for c in by_code if new[c] != plw[c]}
    # проверка: семейство порта == семейству сервера для каждого приза-коробки
    nf, sf = collections.defaultdict(set), collections.defaultdict(set)
    for c in by_code:
        if new[c]:
            nf[new[c]].add(c)
        if slw.get(c):
            sf[slw[c]].add(c)
    bad = [b for b in pb if (nf[new[b]] if new[b] else {b}) != (sf[slw[b]] if slw.get(b) else {b})]
    return chg, pb, bad


def backup(path, bdir):
    """Копия файла в bdir/<путь относительно @@ASTORY_DEV_ROOT_ESC@@> с проверкой размера."""
    rel = os.path.relpath(path, ROOT)
    dst = os.path.join(bdir, rel)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if os.path.exists(dst):
        raise SystemExit('бэкап уже есть, не перезаписываю: %s' % dst)
    shutil.copy2(path, dst)
    if os.path.getsize(dst) != os.path.getsize(path):
        raise SystemExit('бэкап не совпал по размеру: %s' % dst)
    return dst


def patch_hex(h, v):
    """DataHex с новым i16 LE по смещению 220."""
    b = bytearray(bytes.fromhex(h))
    b[LW_OFF:LW_OFF + 2] = int(v).to_bytes(2, 'little', signed=True)
    return b.hex()


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--write', action='store_true')
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--table-dir')
    ap.add_argument('--backup-dir', default=os.path.join(ROOT, '_backup', datetime.date.today().isoformat() + '_data'))
    a = ap.parse_args(argv)
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass
    srv, prizes, sums = read_server(a.table_dir)
    head, rows, by_code = read_port()
    chg, pb, bad = plan(srv, prizes, by_code)
    print('сервер: %s' % ', '.join('%s md5 %s' % kv for kv in sums.items()))
    print('призов %d, из них коробок в item_list %d; правок LinkWordCode: %d; семейств после правки ≠ серверу: %s'
          % (len(prizes), len(pb), len(chg), bad))
    for c, (row, was, now) in sorted(chg.items()):
        print('  %d (row %d, %s): %d -> %d' % (c, row, by_code[c]['ItemName'][:32], was, now))
    if bad:
        print('СТОП: после правки семейства не совпадают'); return 3
    if a.check:
        return 0 if not chg else 1
    if not a.write:
        print('сухой прогон: ничего не записано (--write для записи)'); return 0
    if not chg:
        print('правок нет'); return 0
    # ведомость: строки этой колонки для тех же row уже есть? — чужая правка, стоп
    lt = open(LEDGER_P, 'rb').read().decode('utf-8')
    lrd = csv.DictReader(io.StringIO(lt, newline=''))
    lrows = list(lrd)
    taken = {(r['row'], r['column']) for r in lrows}
    clash = [c for c, (row, _, _) in chg.items() if (str(row), 'LinkWordCode') in taken]
    if clash:
        print('СТОП: в ведомости уже есть LinkWordCode для кодов %s' % clash[:10]); return 4
    # бэкапы
    bks = {p: backup(p, a.backup_dir) for p in (CSV_P, JSON_P, LEDGER_P)}
    # 1) ведомость — дописать (формат и порядок прежних строк не меняются)
    out = io.StringIO(newline='')
    w = csv.DictWriter(out, fieldnames=lrd.fieldnames)
    for c, (row, was, now) in sorted(chg.items(), key=lambda kv: kv[1][0]):
        w.writerow({'row': row, 'column': 'LinkWordCode', 'was': was, 'now': now,
                    'source': 'item_table.txt кол.40 код %d = %d (живой сервер, md5 %s)' % (c, now, sums['item_table.txt'][:8]),
                    'reason': REASON})
    tail = out.getvalue()
    sep = '' if lt.endswith('\n') else '\r\n'
    open(LEDGER_P, 'wb').write((lt + sep + tail).encode('utf-8'))
    # 2) CSV
    for c, (row, was, now) in chg.items():
        r = by_code[c]
        r['LinkWordCode'] = str(now)
        r['DataHex'] = patch_hex(r['DataHex'], now)
    o = io.StringIO(newline='')
    w = csv.DictWriter(o, fieldnames=head)
    w.writeheader()
    for r in rows:
        w.writerow(r)
    open(CSV_P, 'wb').write(o.getvalue().encode('utf-8'))
    # 3) JSON (тот же формат, что пишет decode_item_db.write_outputs: indent=0, ensure_ascii=False, CRLF)
    j = json.loads(open(JSON_P, 'rb').read().decode('utf-8'))
    jby = {int(x['ItemCode']): x for x in j}
    for c, (row, was, now) in chg.items():
        x = jby[c]
        if int(x['LinkWordCode']) != was or str(x['Name']) != str(row):
            raise SystemExit('JSON расходится с CSV у кода %d' % c)
        x['LinkWordCode'] = now
        x['DataHex'] = patch_hex(x['DataHex'], now)
    open(JSON_P, 'wb').write(json.dumps(j, ensure_ascii=False, indent=0).replace('\n', '\r\n').encode('utf-8'))
    # 4) задание точечной заливки DT
    job = [{'dt': DT_PATH, 'csv_new': CSV_P, 'csv_old': bks[CSV_P],
            'allow_cols': ['LinkWordCode', 'DataHex'], 'allow_rows': sorted(str(row) for row, _, _ in chg.values())}]
    jp = os.path.join(a.backup_dir, 'dt_point_item_list_linkword.json')
    json.dump(job, open(jp, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    print('записано: ведомость +%d строк, CSV/JSON %d записей; бэкапы %s; задание DT %s'
          % (len(chg), len(chg), a.backup_dir, jp))
    return 0


if __name__ == '__main__':
    sys.exit(main())
