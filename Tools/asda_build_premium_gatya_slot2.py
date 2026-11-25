# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
asda_build_premium_gatya_slot2.py — сборка данных ВТОРОГО автомата премиум-гачи (слот 1).

★ УСТАРЕЛ (2026-08-18). Обобщён на произвольное число автоматов в
  `asda_build_premium_gatya_slots.py` (`--slots N --copy-from S`), которым и сделано
  расширение до пяти — отчёт `Reports/gacha_slots5.md`. Этот файл оставлен как
  документ того, ОТКУДА взялся слот 1: его пул построен по разметке `ViewSlot == 1`
  оригинального `premium_gatya.bin`, а не копией другого автомата. Повторно не запускать:
  он перезапишет `premium_gatya.csv` обратно на два автомата.

ЗАЧЕМ. Владелец: «гача №1 работает, гача №2 нет». Причина доказана логами живого сервера
(`Log_CRTJIN\CRTJIN_20260818.txt`: две строки `Recv_UserPrimiumGaCha(3)` в 11:24:16/11:24:20):
серверный гейт `Recv_UserPrimiumGaCha@0x4F6F10` требует `slot < word_7E037C`, где `word_7E037C`
= число строк файла `.\ItemTable_Info\PrimiumGaChaPriceTable.txt`. На живом сервере в файле
объявлен ОДИН слот, поэтому запрос со слотом 1 отбрасывается ещё до GP7, и клиенту уходит
6578 со статусом 0 — ни анимации, ни приза. Второй барьер: в `PrimiumGaChaTable.txt` у всех
строк `iSlotNumber = 0`, то есть у второго автомата нет пула призов, и наш GP7-агент
(`gp7-server\server.js`, `selectLuckyBoxReward(boxType)`) вернул бы `missing-luckybox-pool`.

ОТКУДА БЕРУТСЯ ДАННЫЕ (ничего не выдумано, закон `never-invent-only-original`):
  * цены — из ОРИГИНАЛЬНОГО `Data/premium_gatya_data.bin`: в файле ДВЕ записи `{0,690}` и
    `{1,690}`, а счётчик в заголовке @4 равен 1 — дефект данных оригинала, из-за которого
    оригинал видит только первый автомат (закон `fix-original-defects`);
  * пул призов слота 1 — строки ОРИГИНАЛЬНОЙ таблицы `premium_gatya.bin`, у которых поле
    `ViewSlot` (+12) равно 1, то есть сам оригинал относит эти призы к ВИТРИНЕ второго
    автомата (эталон `uiRefreshItemGridPage@0xB6EE70` фильтрует сетку 68300 по этому полю).
    Проверено: все 20 уникальных кодов уже присутствуют в живом пуле слота 0, то есть
    заведомо выдаются этим сервером.

ЧТО ДЕЛАЕТ СКРИПТ:
  1. `--csv`   — пересобирает `RawAssets/Data/ItemDB/premium_gatya.csv`:
                 существующие строки получают `ViewSlot` по оригиналу, в конец дописываются
                 строки второго автомата (`SlotNumber = 1`, `ViewSlot = 1`).
  2. `--srv`   — кладёт в `Reports/gacha_slot2_fix/` ДВА готовых серверных файла
                 (`PrimiumGaChaPriceTable.txt`, `PrimiumGaChaTable.txt`). НА ЖИВОЙ СЕРВЕР
                 НИЧЕГО НЕ ПИШЕТ: копирует их туда владелец, сервер читает таблицы только
                 при старте.

Побочных эффектов при импорте нет (закон `tools-no-import-side-effects`).

Запуск:  python asda_build_premium_gatya_slot2.py --csv --srv
"""
import os
import struct
import sys
import io

AREA        = r'@@ASTORY_DEV_ROOT@@'
PROJECT     = os.path.join(AREA, "AstoryClient")
# Оригинальная таблица призов клиента (та же, что серверная PrimiumGaChaTable.txt).
ORIG_BIN    = os.path.join(AREA, 'Donors', 'TRANSLATE_unpacked', 'tables_orig', 'Data', 'Character', 'premium_gatya.bin')
# Оригинальная таблица цен клиента (та же, что серверная PrimiumGaChaPriceTable.txt).
ORIG_PRICE  = os.path.join(AREA, 'Donors', 'AsdaChroniclesInterlude', 'Data', 'premium_gatya_data.bin')
# Живой пул сервера — ТОЛЬКО ЧТЕНИЕ, база для дописывания слота 1.
LIVE_TABLE  = r'\\@@ASTORY_SERVER_HOSTNAME@@\H(@@ASTORY_SERVER_HOSTNAME@@_ns)\ASDA\Working_ASDA\v2\GameServer\ItemTable_Info\PrimiumGaChaTable.txt'
CSV_PATH    = os.path.join(PROJECT, 'RawAssets', 'Data', 'ItemDB', 'premium_gatya.csv')
OUT_DIR     = os.path.join(AREA, 'Reports', 'gacha_slot2_fix')

CSV_HEADER  = '_row,Index,SlotNumber,ItemCode,ViewSlot,Quantity'


def read_orig_rows(path):
    """Записи `premium_gatya.bin`: заголовок 20 б (счётчик @4), затем по 20 б
       {Index +0, SlotNumber +4, ItemCode +8, ViewSlot +12, Quantity +16}."""
    with open(path, 'rb') as f:
        data = f.read()
    count = struct.unpack_from('<I', data, 4)[0]
    total = (len(data) - 20) // 20
    rows = [struct.unpack_from('<5i', data, 20 + 20 * i) for i in range(total)]
    return count, rows


def read_orig_prices(path):
    """Записи `premium_gatya_data.bin`: заголовок 20 б, затем по 8 б {Slot i32, Price i32}."""
    with open(path, 'rb') as f:
        data = f.read()
    count = struct.unpack_from('<I', data, 4)[0]
    total = (len(data) - 20) // 8
    rows = [struct.unpack_from('<2i', data, 20 + 8 * i) for i in range(total)]
    return count, rows


def slot1_rows(rows):
    """Строки оригинала, отнесённые к витрине ВТОРОГО автомата (ViewSlot == 1)."""
    return [r for r in rows if r[3] == 1]


def slot1_codes(rows):
    """Уникальные коды витрины второго автомата, в порядке появления в оригинале."""
    out = []
    for r in slot1_rows(rows):
        if r[2] not in out:
            out.append(r[2])
    return out


def build_csv():
    hdr_count, orig = read_orig_rows(ORIG_BIN)
    codes = slot1_codes(orig)
    # ViewSlot по коду из оригинала: код может стоять в витринах обоих автоматов.
    view = {}
    for r in orig:
        view.setdefault(r[2], set()).add(r[3])

    with io.open(CSV_PATH, 'r', encoding='utf-8') as f:
        lines = [l.rstrip('\r\n') for l in f if l.strip()]
    old_header, old_rows = lines[0], lines[1:]
    if old_header.count(',') != CSV_HEADER.count(','):
        raise SystemExit('премиум-гача: у %s неожиданный заголовок %r' % (CSV_PATH, old_header))

    out, row_no, kept = [CSV_HEADER], 0, 0
    for line in old_rows:
        _row, index, slot, code, vslot, qty = line.split(',')
        code_i = int(code)
        # Витрина ПЕРВОГО автомата: код остаётся, если оригинал не относил его
        # исключительно к витрине второго (у 74532 в оригинале только ViewSlot=1).
        if 0 in view.get(code_i, {0}):
            out.append('%d,%d,0,%d,0,%s' % (row_no, row_no, code_i, qty))
            row_no += 1
            kept += 1
    added = 0
    for code in codes:
        out.append('%d,%d,1,%d,1,1' % (row_no, row_no, code))
        row_no += 1
        added += 1

    with io.open(CSV_PATH, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(out) + '\n')
    print('[premium_gatya] CSV: строк первого автомата %d, дописано второму %d, всего %d -> %s'
          % (kept, added, row_no, CSV_PATH))


def build_server_files():
    _, orig = read_orig_rows(ORIG_BIN)
    pcount, prices = read_orig_prices(ORIG_PRICE)
    add = slot1_rows(orig)

    with io.open(LIVE_TABLE, 'r', encoding='latin-1') as f:
        live = [l.strip() for l in f if l.strip()]
    live_count, live_rows = int(live[0]), live[1:]
    if live_count != len(live_rows):
        raise SystemExit('живая PrimiumGaChaTable.txt: заголовок %d, строк %d' % (live_count, len(live_rows)))

    os.makedirs(OUT_DIR, exist_ok=True)

    # Цены: ВСЕ записи оригинального premium_gatya_data.bin (счётчик в заголовке файла врёт).
    lines = ['%d' % len(prices)] + ['%d\t%d' % (s, p) for s, p in prices]
    path = os.path.join(OUT_DIR, 'PrimiumGaChaPriceTable.txt')
    with io.open(path, 'w', encoding='ascii', newline='\r\n') as f:
        f.write('\n'.join(lines) + '\n')
    print('[premium_gatya] сервер: %s — слотов %d %s' % (path, len(prices), [s for s, _ in prices]))

    # Пул: живые строки + строки слота 1. Индексы обязаны идти строго +1 (иначе загрузчик
    # sub_4BCAD0 покажет "PrimiumGaChaTable.txt Sync Error" и сервер не поднимется).
    idx = live_count
    tail = []
    for r in add:
        tail.append('%d\t1\t%d\t%d' % (idx, r[2], r[4]))
        idx += 1
    out = ['%d' % (live_count + len(tail))] + live_rows + tail
    path = os.path.join(OUT_DIR, 'PrimiumGaChaTable.txt')
    with io.open(path, 'w', encoding='ascii', newline='\r\n') as f:
        f.write('\n'.join(out) + '\n')
    print('[premium_gatya] сервер: %s — было %d строк, слоту 1 добавлено %d, стало %d'
          % (path, live_count, len(tail), live_count + len(tail)))


def main():
    args = set(sys.argv[1:]) or {'--csv', '--srv'}
    if '--csv' in args:
        build_csv()
    if '--srv' in args:
        build_server_files()


if __name__ == '__main__':
    main()
