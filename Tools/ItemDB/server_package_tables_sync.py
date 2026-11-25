#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
server_package_tables_sync.py — согласование СЕРВЕРНЫХ таблиц коробок/гачи с КЛИЕНТСКИМИ.

Клиент — авторитет (решение пользователя). Скрипт:
  1) читает клиентские ОРИГИНАЛЬНЫЕ бинари (Donors/TRANSLATE_unpacked/tables_orig/Data/Character):
        packagetable.bin  — состав коробок (5 пар «код/количество»)
        item_list.bin     — шаблоны предметов (ItemType/ItemSubType/DurationOrEnchant/LinkCode)
        booster_list.bin  — таблица призов гачи (35 пар «код/вес»)
  2) читает СЕРВЕРНЫЕ текстовые таблицы (снимок живого сервера в live_orig/):
        PackageItem_Table.txt   — состав коробок (5 пар), запись 34 б
        item_table.txt          — 42 колонки, запись 71 б
        GachaItemRateTable.txt  — 35 пар, запись 284 б
  3) считает расхождения (режим --report);
  4) строит согласованные серверные таблицы в out/ (режим --build).

ФОРМАТЫ (подтверждено декомпилом, не гипотеза):
  PackageItem_Table.txt : GameServer.c sub_4B0460 @0x4B0460
        строка 1: N; далее N строк: idx  (код i32, кол-во i16) × 5, разделитель TAB, перевод CRLF
        память: 34 б = id(4) + 5×(код i32 @6j+4, кол-во i16 @6j+8)
  item_table.txt        : GameServer.c sub_4A96A0 @0x4A96A0
        строка 1: N; далее N строк по 42 колонки. Коды ДОЛЖНЫ идти подряд +1 (Sync Error).
        колонка 16 -> +26 itemType (u8), 17 -> +27 itemSubType (u8), 39 -> +63 индекс пакета/гачи (i16)
  GachaItemRateTable.txt: GameServer.c sub_4B46B0 @0x4B46B0
        строка 1: N; далее N строк: id  (код i32, вес i32) × 35 — В ФАЙЛЕ ИНТЕРЛИВЕД
  packagetable.bin      : клиент CCharData_LoadPackAgeTable @0xDB1590 (file_vfs.c:1227)
        header 20 б + 5228 × 56 б: +0 idx, далее ПАРАМИ (+4 код0,+8 кол0 … +36 код4,+40 кол4);
        +44/+48/+52 не читает ни один потребитель клиента и на сервере не существуют.
  booster_list.bin      : клиент CItemManager_LoadBoosterList @0xD1EDF0 (item_inventory.c:42936)
        header 20 б + 4134 × 284 б: +0 id, далее 35 ИНТЕРЛИВЕД пар (код i32, вес i32)
        — та же раскладка, что в тексте GachaItemRateTable.txt.

ОГРАНИЧЕНИЯ СЕРВЕРА, которые обязан соблюсти результат (GameServer.c sub_4BC*, строки 75186-75245):
  * item_table: коды строго подряд, число строк == числу строк SowelCombine.txt -> МЕНЯТЬ ЧИСЛО
    СТРОК item_table.txt НЕЛЬЗЯ (иначе MessageBox «item_table.txt != SowelCombine.txt» и отказ старта);
  * для предметов с колонкой 17 (subType) ∈ {200,201}: колонка 39 <= число строк PackageItem_Table;
  * для ОСТАЛЬНЫХ предметов:                          колонка 39 <= число строк GachaItemRateTable;
  * внутри одной строки PackageItem_Table коды не должны повторяться, если предмет существует и его
    байт +35 (колонка 23) == 1 — иначе exit(1) при старте.

Запуск:
  python Tools/server_package_tables_sync.py --report
  python Tools/server_package_tables_sync.py --build
"""
import os, sys, struct, argparse, hashlib, io

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))
CLIENT_DIR = os.path.join(ROOT, 'Donors', 'TRANSLATE_unpacked', 'tables_orig', 'Data', 'Character')
WORK = os.path.join(ROOT, 'RE', 'dumps', 'server_tables_fix')
LIVE = os.path.join(WORK, 'live_orig')
OUT = os.path.join(WORK, 'out')

HDR = 20  # заголовок клиентских .bin: float ver / int count / int extra / char[4] tag / int pad


# ---------------------------------------------------------------------------
# Клиентские бинари
# ---------------------------------------------------------------------------
def read_bin(path, stride):
    """Читает клиентскую таблицу: заголовок 20 б + count × stride. Возвращает (count, [bytes])."""
    d = open(path, 'rb').read()
    count = struct.unpack_from('<i', d, 4)[0]
    body = d[HDR:]
    got = len(body) // stride
    if got != count:
        raise RuntimeError('%s: заголовок %d, фактически %d записей' % (path, count, got))
    return count, [body[i * stride:(i + 1) * stride] for i in range(count)]


def client_packagetable():
    """packagetable.bin -> {idx: [(код, кол-во) × 5]} (только 5 пар, +44/+48/+52 игнорируются)."""
    _, recs = read_bin(os.path.join(CLIENT_DIR, 'packagetable.bin'), 56)
    out = {}
    order = []
    for r in recs:
        idx = struct.unpack_from('<i', r, 0)[0]
        pairs = [(struct.unpack_from('<i', r, 4 + 8 * k)[0],
                  struct.unpack_from('<i', r, 8 + 8 * k)[0]) for k in range(5)]
        out[idx] = pairs
        order.append(idx)
    return order, out


def client_booster():
    """booster_list.bin -> {id: [(код, вес) × 35]} (интерливед, как в файле)."""
    _, recs = read_bin(os.path.join(CLIENT_DIR, 'booster_list.bin'), 284)
    out, order = {}, []
    for r in recs:
        rid = struct.unpack_from('<i', r, 0)[0]
        pairs = [(struct.unpack_from('<i', r, 4 + 8 * k)[0],
                  struct.unpack_from('<i', r, 8 + 8 * k)[0]) for k in range(35)]
        out[rid] = pairs
        order.append(rid)
    return order, out


def client_item_list():
    """item_list.bin (шифрован: MD5 16 б + вычитание 35 из DWORD/WORD) -> {код: dict полей}.

    Расшифровываются только нужные целочисленные поля; строки не трогаем.
    Смещения — в записи ПОСЛЕ снятия 16-байтового MD5-префикса (stride 264 = 16 + 248).
    """
    _, recs = read_bin(os.path.join(CLIENT_DIR, 'item_list.bin'), 264)
    # индексы DWORD-полей (в единицах по 4 байта), которые шифрованы вычитанием 35
    need = {'ItemCode': 0, 'LinkCode': 96, 'ItemType': 128, 'ItemSubType': 132,
            'CategoryFlag': 156, 'TradableFlag': 152, 'DurationOrEnchant': 216}
    out = {}
    for rec in recs:
        b = rec[16:]  # снять MD5
        v = {}
        for name, off in need.items():
            raw = struct.unpack_from('<I', b, off)[0]
            v[name] = (raw - 35) & 0xFFFFFFFF
            if v[name] >= 0x80000000:
                v[name] -= 0x100000000
        out[v['ItemCode']] = v
    return out


# ---------------------------------------------------------------------------
# Серверные текстовые таблицы
# ---------------------------------------------------------------------------
def atol(s):
    """Эмуляция C-шной atol: знак + цифры до первого нецифрового символа, иначе 0.

    Нужна буквально: в живом GachaItemRateTable.txt строка 1740 содержит токен «(1)»,
    который сервер (fscanf «%s» + atol) читает как 0. Питоновский int() тут упал бы.
    """
    i, n = 0, len(s)
    while i < n and s[i] in ' \t\r\n':
        i += 1
    sign = 1
    if i < n and s[i] in '+-':
        if s[i] == '-':
            sign = -1
        i += 1
    j = i
    while j < n and s[j].isdigit():
        j += 1
    return sign * int(s[i:j]) if j > i else 0


def read_server_table(path, ncols):
    """Читает серверную таблицу «N + N строк по ncols колонок». Возвращает (N, [[str]*ncols])."""
    with open(path, 'r', encoding='latin-1', newline='') as f:
        txt = f.read()
    toks = txt.split()
    n = int(toks[0])
    body = toks[1:]
    if len(body) != n * ncols:
        raise RuntimeError('%s: ожидалось %d токенов, получено %d' % (path, n * ncols, len(body)))
    return n, [body[i * ncols:(i + 1) * ncols] for i in range(n)]


def server_package():
    """PackageItem_Table.txt -> (N, {idx: [(код, кол-во) × 5]})."""
    n, rows = read_server_table(os.path.join(LIVE, 'PackageItem_Table.txt'), 11)
    out = {}
    for r in rows:
        idx = atol(r[0])
        out[idx] = [(atol(r[1 + 2 * k]), atol(r[2 + 2 * k])) for k in range(5)]
    return n, out


def server_gacha():
    """GachaItemRateTable.txt -> (N, {id: [(код, вес) × 35]})."""
    n, rows = read_server_table(os.path.join(LIVE, 'GachaItemRateTable.txt'), 71)
    out = {}
    for r in rows:
        rid = atol(r[0])
        out[rid] = [(atol(r[1 + 2 * k]), atol(r[2 + 2 * k])) for k in range(35)]
    return n, out


def server_item_table():
    """item_table.txt -> (N, [ [42 строки-колонки] ]) в исходном порядке."""
    return read_server_table(os.path.join(LIVE, 'item_table.txt'), 42)


# колонки item_table, которые нас касаются
COL_CODE, COL_TYPE, COL_SUB, COL_PKG = 0, 16, 17, 39
COL_FLAG35 = 23  # байт +35 — участвует в проверке дублей PackageItem_Table


def md5f(path):
    h = hashlib.md5()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


# ---------------------------------------------------------------------------
# АНАЛИЗ РАСХОЖДЕНИЙ
# ---------------------------------------------------------------------------
def analyze():
    """Полная картина расхождений «клиент против ЖИВОГО сервера». Возвращает словарь фактов."""
    cl = client_item_list()
    _, cpkg = client_packagetable()
    _, cboost = client_booster()
    spkg_n, spkg = server_package()
    sg_n, sgacha = server_gacha()
    sit_n, sit = server_item_table()
    srv = {atol(r[COL_CODE]): r for r in sit}

    boxes = {c: v for c, v in cl.items()
             if v['ItemType'] == 11 and v['ItemSubType'] in (200, 201)}

    st = {'ok': [], 'missing_code': [], 'wrong_type': [], 'idx_out': [], 'idx_diff': [],
          'client_idx_missing': []}
    for c, v in sorted(boxes.items()):
        ci = v['DurationOrEnchant']
        if ci not in cpkg:
            st['client_idx_missing'].append(c)
            continue
        r = srv.get(c)
        if r is None:
            st['missing_code'].append(c)
            continue
        stype, ssub, sidx = atol(r[COL_TYPE]), atol(r[COL_SUB]), atol(r[COL_PKG])
        if stype != 11 or ssub not in (200, 201):
            st['wrong_type'].append((c, stype, ssub))
        elif sidx >= spkg_n or sidx < 0:
            st['idx_out'].append((c, sidx))
        elif sidx != ci:
            st['idx_diff'].append((c, ci, sidx))
        else:
            # индекс совпал — сверяем ещё и СОСТАВ строки
            if [p for p in cpkg[ci]] != [p for p in spkg.get(sidx, [])]:
                st['idx_diff'].append((c, ci, sidx))  # тот же индекс, но разный состав
            else:
                st['ok'].append(c)

    # состав строк пакетов: сколько общих индексов различаются
    common_idx = sorted(set(cpkg) & set(spkg))
    diff_rows = [i for i in common_idx if cpkg[i] != spkg[i]]

    # бустеры/гача
    common_b = sorted(set(cboost) & set(sgacha))
    b_diff = [i for i in common_b if cboost[i] != sgacha[i]]
    b_codes_diff = [i for i in common_b
                    if [p[0] for p in cboost[i]] != [p[0] for p in sgacha[i]]]

    # предметы-гача (не коробки, но с ненулевым индексом гачи)
    gacha_items = {c: v for c, v in cl.items()
                   if not (v['ItemType'] == 11 and v['ItemSubType'] in (200, 201))
                   and v['DurationOrEnchant'] > 0}
    gacha_touch = set()
    for c, v in gacha_items.items():
        if v['DurationOrEnchant'] in b_diff:
            gacha_touch.add(c)

    return dict(cl=cl, cpkg=cpkg, cboost=cboost, spkg=spkg, spkg_n=spkg_n, sgacha=sgacha,
                sg_n=sg_n, sit=sit, sit_n=sit_n, srv=srv, boxes=boxes, st=st,
                diff_rows=diff_rows, common_idx=common_idx, b_diff=b_diff,
                b_codes_diff=b_codes_diff, gacha_items=gacha_items, gacha_touch=gacha_touch)


def report():
    a = analyze()
    st = a['st']
    P = print
    P('=== ФОРМАТ ===')
    P('клиент packagetable : %d строк, 5 пар (+44/+48/+52 не читаются)' % len(a['cpkg']))
    P('сервер PackageItem  : %d строк, 5 пар' % a['spkg_n'])
    P('клиент booster_list : %d строк, 35 пар (интерливед)' % len(a['cboost']))
    P('сервер GachaRate    : %d строк, 35 пар (интерливед)' % a['sg_n'])
    P('сервер item_table   : %d строк, 42 колонки' % a['sit_n'])
    P('клиент item_list    : %d записей' % len(a['cl']))
    P('')
    P('=== КОРОБКИ КЛИЕНТА (itemType 11, subType 200/201) : %d ===' % len(a['boxes']))
    P('  согласовано полностью (код есть, тип совпал, индекс совпал, СОСТАВ совпал): %d' % len(st['ok']))
    P('  кода нет в серверной item_table (ответ 4)      : %d' % len(st['missing_code']))
    P('  сервер не считает коробкой (ответ 5)           : %d' % len(st['wrong_type']))
    P('  индекс вне серверной PackageItem_Table (отв. 8): %d' % len(st['idx_out']))
    P('  индекс/состав расходятся (превью != выдача)    : %d' % len(st['idx_diff']))
    P('  индекса нет даже в клиентской таблице          : %d' % len(st['client_idx_missing']))
    P('')
    P('=== СТРОКИ ПАКЕТОВ ===')
    P('  общих индексов %d, из них с разным составом: %d' % (len(a['common_idx']), len(a['diff_rows'])))
    P('  клиентских строк вне серверной таблицы: %d (индексы %d..%d)'
      % (len(a['cpkg']) - a['spkg_n'], a['spkg_n'], len(a['cpkg']) - 1))
    P('')
    P('=== ГАЧА / БУСТЕР ===')
    P('  общих строк %d; различаются целиком: %d, из них по КОДАМ призов: %d'
      % (len(set(a['cboost']) & set(a['sgacha'])), len(a['b_diff']), len(a['b_codes_diff'])))
    P('  серверных строк без клиентского аналога: %d' % (a['sg_n'] - len(set(a['cboost']) & set(a['sgacha']))))
    P('  клиентских предметов-гача, попадающих в различающиеся строки: %d' % len(a['gacha_touch']))
    return a




# ---------------------------------------------------------------------------
# ПОСТРОЕНИЕ СОГЛАСОВАННЫХ ТАБЛИЦ (принцип UNION, минимальная дельта)
# ---------------------------------------------------------------------------
import re

TOKEN_RE = re.compile(r'\S+')


def read_lines(path):
    """Читает серверную таблицу построчно, сохраняя разделители 1:1 (latin-1 + newline='')."""
    with open(path, 'r', encoding='latin-1', newline='') as f:
        return f.read().split('\r\n')


def patch_tokens(line, repl):
    """Заменяет в строке токены с номерами из repl={idx: 'новое'}, НЕ трогая разделители."""
    ms = list(TOKEN_RE.finditer(line))
    out, prev = [], 0
    for i, m in enumerate(ms):
        out.append(line[prev:m.start()])
        out.append(repl.get(i, m.group(0)))
        prev = m.end()
    out.append(line[prev:])
    return ''.join(out)


def fmt_pkg_row(idx, pairs):
    """Строка PackageItem_Table.txt в формате живого файла: idx TAB (код TAB кол-во) × 5."""
    parts = [str(idx)]
    for code, qty in pairs:
        parts += [str(code), str(qty)]
    return '\t'.join(parts)


def fmt_gacha_row(rid, pairs):
    """Строка GachaItemRateTable.txt: id TAB (код TAB вес) × 35."""
    parts = [str(rid)]
    for code, w in pairs:
        parts += [str(code), str(w)]
    return '\t'.join(parts)


def build():
    a = analyze()
    cl, cpkg, cboost = a['cl'], a['cpkg'], a['cboost']
    spkg, spkg_n, sgacha, sg_n = a['spkg'], a['spkg_n'], a['sgacha'], a['sg_n']
    srv = a['srv']
    os.makedirs(OUT, exist_ok=True)
    log = []

    # ---------------- 1. PackageItem_Table.txt ----------------
    src = read_lines(os.path.join(LIVE, 'PackageItem_Table.txt'))
    assert atol(src[0]) == spkg_n
    body = src[1:1 + spkg_n]
    changed_rows = []
    for i in range(spkg_n):
        if i in cpkg and cpkg[i] != spkg[i]:
            body[i] = fmt_pkg_row(i, cpkg[i])
            changed_rows.append(i)
    added_rows = []
    for i in range(spkg_n, len(cpkg)):
        body.append(fmt_pkg_row(i, cpkg[i]))
        added_rows.append(i)
    new_n = len(body)
    out_pkg = os.path.join(OUT, 'PackageItem_Table.txt')
    with open(out_pkg, 'w', encoding='latin-1', newline='') as f:
        f.write(str(new_n) + '\r\n')
        for ln in body:
            f.write(ln + '\r\n')
    log.append(('PackageItem_Table.txt', spkg_n, new_n, len(changed_rows), len(added_rows)))

    # ---------------- 2. GachaItemRateTable.txt ----------------
    src = read_lines(os.path.join(LIVE, 'GachaItemRateTable.txt'))
    assert atol(src[0]) == sg_n
    gbody = src[1:1 + sg_n]
    g_changed = []
    for pos in range(sg_n):
        rid = atol(TOKEN_RE.search(gbody[pos]).group(0))
        if rid in cboost and cboost[rid] != sgacha[rid]:
            gbody[pos] = fmt_gacha_row(rid, cboost[rid])
            g_changed.append(rid)
    # клиентских строк вне серверной таблицы нет (проверяется ниже в verify)
    out_g = os.path.join(OUT, 'GachaItemRateTable.txt')
    with open(out_g, 'w', encoding='latin-1', newline='') as f:
        f.write(str(len(gbody)) + '\r\n')
        for ln in gbody:
            f.write(ln + '\r\n')
    log.append(('GachaItemRateTable.txt', sg_n, len(gbody), len(g_changed), 0))

    # ---------------- 3. item_table.txt ----------------
    src = read_lines(os.path.join(LIVE, 'item_table.txt'))
    n_it = atol(src[0])
    ibody = src[1:1 + n_it]
    fixed = {'box_idx': [], 'box_type': [], 'gacha_idx': []}
    for pos in range(n_it):
        ms = list(TOKEN_RE.finditer(ibody[pos]))
        code = atol(ms[COL_CODE].group(0))
        v = cl.get(code)
        if v is None:
            continue  # кода нет у клиента — серверную запись сохраняем как есть (union)
        st, ss, sidx = atol(ms[COL_TYPE].group(0)), atol(ms[COL_SUB].group(0)), atol(ms[COL_PKG].group(0))
        ct, cs, cidx = v['ItemType'], v['ItemSubType'], v['DurationOrEnchant']
        client_box = (ct == 11 and cs in (200, 201))
        server_box = (st == 11 and ss in (200, 201))
        repl = {}
        if client_box and not server_box:
            # клиент считает коробкой, сервер — нет: приводим тип/подтип/индекс к клиенту
            repl[COL_TYPE] = str(ct)
            repl[COL_SUB] = str(cs)
            repl[COL_PKG] = str(cidx)
            fixed['box_type'].append(code)
        elif client_box and server_box and sidx != cidx:
            repl[COL_PKG] = str(cidx)
            fixed['box_idx'].append(code)
        elif (not client_box) and (not server_box) and sidx != cidx:
            # индекс гачи: клиент авторитет; серверные-only коробки (server_box) НЕ трогаем
            repl[COL_PKG] = str(cidx)
            fixed['gacha_idx'].append(code)
        if repl:
            ibody[pos] = patch_tokens(ibody[pos], repl)
    out_it = os.path.join(OUT, 'item_table.txt')
    with open(out_it, 'w', encoding='latin-1', newline='') as f:
        f.write(str(n_it) + '\r\n')
        for ln in ibody:
            f.write(ln + '\r\n')
    log.append(('item_table.txt', n_it, n_it,
                len(fixed['box_idx']) + len(fixed['box_type']) + len(fixed['gacha_idx']), 0))

    for name, was, now, chg, add in log:
        print('%-24s строк %6d -> %6d   изменено %5d   добавлено %5d' % (name, was, now, chg, add))
    print('item_table: индекс коробки %d, тип+индекс коробки %d, индекс гачи %d'
          % (len(fixed['box_idx']), len(fixed['box_type']), len(fixed['gacha_idx'])))
    return a, fixed, dict(pkg_changed=changed_rows, pkg_added=added_rows, gacha_changed=g_changed)




# ---------------------------------------------------------------------------
# ПРОВЕРКА ПОСТРОЕННЫХ ТАБЛИЦ (обязательна ДО записи на сервер)
# ---------------------------------------------------------------------------
def verify():
    """Сверяет out/ с клиентом и с ограничениями сервера. Возвращает (ok, [строки отчёта])."""
    cl = client_item_list()
    _, cpkg = client_packagetable()
    _, cboost = client_booster()
    spkg_n, spkg_old = server_package()
    sg_n, sg_old = server_gacha()

    R, ok = [], True

    def chk(cond, text):
        nonlocal ok
        R.append(('OK  ' if cond else 'СБОЙ') + '  ' + text)
        if not cond:
            ok = False

    # --- новые таблицы теми же ридерами, что и серверные
    def read_pkg(path):
        n, rows = read_server_table(path, 11)
        return n, {atol(r[0]): [(atol(r[1 + 2 * k]), atol(r[2 + 2 * k])) for k in range(5)] for r in rows}

    def read_gacha(path):
        n, rows = read_server_table(path, 71)
        return n, {atol(r[0]): [(atol(r[1 + 2 * k]), atol(r[2 + 2 * k])) for k in range(35)] for r in rows}

    npkg, npkg_d = read_pkg(os.path.join(OUT, 'PackageItem_Table.txt'))
    ngac, ngac_d = read_gacha(os.path.join(OUT, 'GachaItemRateTable.txt'))
    nit, nit_rows = read_server_table(os.path.join(OUT, 'item_table.txt'), 42)
    nsrv = {atol(r[COL_CODE]): r for r in nit_rows}

    # --- 1. структура
    chk(npkg == len(cpkg), 'PackageItem_Table: %d строк == клиентских %d' % (npkg, len(cpkg)))
    chk(all(npkg_d.get(i) is not None for i in range(npkg)), 'PackageItem_Table: индексы 0..N-1 без дыр')
    chk(all(atol(TOKEN_RE.search(r[0]).group(0) if False else r[0]) == i
            for i, r in enumerate(read_server_table(os.path.join(OUT, 'PackageItem_Table.txt'), 11)[1])),
        'PackageItem_Table: колонка id == номеру строки (самопроверка сервера, отказ 9)')
    chk(nit == 55920, 'item_table: число строк не изменилось (%d) — SowelCombine.txt (55920) не тронут' % nit)
    codes = [atol(r[COL_CODE]) for r in nit_rows]
    chk(codes == list(range(codes[0], codes[0] + nit)), 'item_table: коды идут подряд (иначе «Sync Error»)')
    chk(ngac == sg_n, 'GachaItemRateTable: число строк не изменилось (%d)' % ngac)

    # --- 2. ограничения валидатора сервера
    bad_pkg = [c for c, r in nsrv.items()
               if atol(r[COL_SUB]) in (200, 201) and atol(r[COL_TYPE]) == 11 and atol(r[COL_PKG]) > npkg]
    chk(not bad_pkg, 'ни одна коробка не ссылается за PackageItem_Table (нарушений %d)' % len(bad_pkg))
    bad_g = [c for c, r in nsrv.items()
             if not (atol(r[COL_TYPE]) == 11 and atol(r[COL_SUB]) in (200, 201)) and atol(r[COL_PKG]) > ngac]
    chk(not bad_g, 'ни один НЕ-пакет не ссылается за GachaItemRateTable (нарушений %d)' % len(bad_g))
    # i16-переполнение поля +63
    bad_i16 = [c for c, r in nsrv.items() if not (-32768 <= atol(r[COL_PKG]) <= 32767)]
    chk(not bad_i16, 'колонка 39 помещается в i16 (нарушений %d)' % len(bad_i16))
    # дубли кодов внутри строки пакета -> exit(1) при старте
    dups = []
    for i in range(npkg):
        seen = []
        for code, _q in npkg_d[i]:
            r = nsrv.get(code)
            if r is not None and atol(r[COL_FLAG35]) == 1:
                if code in seen:
                    dups.append((i, code))
                seen.append(code)
    chk(not dups, 'нет дублей кодов внутри строки пакета (иначе exit(1) при старте): %d' % len(dups))

    # --- 3. цель задачи: коробки клиента резолвятся
    boxes = {c: v for c, v in cl.items() if v['ItemType'] == 11 and v['ItemSubType'] in (200, 201)}
    okbox, nocode, mism = 0, [], []
    for c, v in boxes.items():
        r = nsrv.get(c)
        if r is None:
            nocode.append(c)
            continue
        if atol(r[COL_TYPE]) == 11 and atol(r[COL_SUB]) in (200, 201) \
                and atol(r[COL_PKG]) == v['DurationOrEnchant'] \
                and npkg_d.get(atol(r[COL_PKG])) == cpkg[v['DurationOrEnchant']]:
            okbox += 1
        else:
            mism.append(c)
    R.append('---- коробки клиента: всего %d, согласовано %d, кода нет на сервере %d, прочих расхождений %d'
             % (len(boxes), okbox, len(nocode), len(mism)))
    chk(not mism, 'среди коробок с существующим кодом расхождений не осталось')

    # --- 4. union: серверное не потеряно
    lost_pkg = [i for i in range(spkg_n) if i not in npkg_d]
    chk(not lost_pkg, 'ни одна серверная строка пакета не потеряна')
    sboxes_old = {c for c, r in {atol(r[COL_CODE]): r for r in server_item_table()[1]}.items()
                  if atol(r[COL_TYPE]) == 11 and atol(r[COL_SUB]) in (200, 201)}
    still = {c for c in sboxes_old
             if atol(nsrv[c][COL_TYPE]) == 11 and atol(nsrv[c][COL_SUB]) in (200, 201)}
    chk(len(still) == len(sboxes_old),
        'все %d серверных коробок остались коробками (union, ничего не затёрто)' % len(sboxes_old))
    lost_g = [i for i in sg_old if i not in ngac_d]
    chk(not lost_g, 'ни одна серверная строка гачи не потеряна')
    keep_g = [i for i in sg_old if i not in cboost]
    chk(all(ngac_d[i] == sg_old[i] for i in keep_g),
        'серверные-only строки гачи (%d шт, id %d..%d) сохранены без изменений'
        % (len(keep_g), min(keep_g), max(keep_g)) if keep_g else 'серверных-only строк гачи нет')
    chk(all(ngac_d[i] == cboost[i] for i in cboost),
        'все %d клиентских строк гачи применены' % len(cboost))
    chk(all(npkg_d[i] == cpkg[i] for i in cpkg),
        'все %d клиентских строк пакетов применены' % len(cpkg))

    for line in R:
        print(line)
    return ok, R


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--report', action='store_true')
    ap.add_argument('--build', action='store_true')
    ap.add_argument('--verify', action='store_true')
    args = ap.parse_args()
    if args.build:
        build()
    if args.verify:
        good, _ = verify()
        sys.exit(0 if good else 1)
    if args.report or not (args.build or args.verify):
        report()
