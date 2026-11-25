# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
asda_build_premium_gatya_slots.py — сборка данных премиум-гачи на ПРОИЗВОЛЬНОЕ число автоматов
(слотов 0..N-1). Обобщение прежнего `asda_build_premium_gatya_slot2.py` (жёстко N = 2).

★★★ ЭТО ОСОЗНАННОЕ РАСШИРЕНИЕ СВЕРХ ОРИГИНАЛА (закон `fix-original-defects` + прямое
    указание владельца «раз игра поддерживает 5 гач — сделай пять»).
    ЧТО БЫЛО В ОРИГИНАЛЕ:
      · `Data/premium_gatya_data.bin` — ДВЕ записи цен `{0,690}` и `{1,690}`, но счётчик
        в заголовке @4 равен 1 ⇒ оригинальный клиент строит ОДИН автомат (дефект данных);
      · `premium_gatya.bin` — 357 записей пула, поле `ViewSlot` принимает только 0 и 1;
      · движок и данные при этом рассчитаны ровно на ПЯТЬ: пять полос 68010+10i, пять кнопок
        витрины 68011+10i, пять кнопок «крутить» 68061+i, пять моделей `md_gachya_{a..e}_00`,
        пять углов веера и пять цифр-скинов `GOLD_roullete_FONT3_{1..5}` в атласе `NE_PvP2.tga`.
    ЧТО ДОБАВЛЕНО НАМИ: слоты 2, 3, 4 — цена и пул призов КОПИЕЙ существующего автомата
    (`--copy-from`, по умолчанию 0). Ничего не выдумано: и цена, и каждая строка пула взяты
    из уже работающих данных живого сервера (закон `never-invent-only-original`).

ПОЧЕМУ ПРАВЯТСЯ ОБЕ СТОРОНЫ (сервер И клиент):
  1. `Recv_UserPrimiumGaCha@0x4F6F10` пропускает запрос только при `slot < word_7E037C`, где
     `word_7E037C` = число строк `GameServer\ItemTable_Info\PrimiumGaChaPriceTable.txt`;
  2. загрузчик пула `sub_4BC200` требует `0 <= iSlotNumber <= <число строк price-таблицы>`,
     иначе MessageBox + exit(1) при старте сервера;
  3. загрузчик `sub_4BCAD0` требует, чтобы первая колонка (индекс) шла строго +1, иначе
     «PrimiumGaChaTable.txt Sync Error»;
  4. наш агент GP7 (`gp7-server\server.js::loadLuckyBoxPools`) требует
     `declaredCount == число валидных строк` и КЭШИРУЕТ таблицу до перезапуска;
  5. клиент строит столько автоматов, сколько строк в `DT_premium_gatya_data`
     (`UAsdaPremiumGachaSubsystem::EnsureSceneActor`), а витрину 68300 фильтрует по
     `ViewSlot == выбранный автомат` (эталон `uiRefreshItemGridPage@0xB6EE70`).

ЧТО ДЕЛАЕТ СКРИПТ:
  `--srv`  — кладёт в `Reports/gacha_slots<N>/` два готовых СЕРВЕРНЫХ файла
             (`PrimiumGaChaPriceTable.txt`, `PrimiumGaChaTable.txt`). На живую шару НЕ ПИШЕТ
             (она только на чтение), копирует их владелец, сервер читает таблицы при старте.
  `--csv`  — пересобирает клиентские `premium_gatya_data.csv` (цены по слотам) и
             `premium_gatya.csv` (витрина). Витрина НОВЫХ слотов строится из их СОБСТВЕННОГО
             серверного пула (уникальные коды в порядке появления) — то есть показывает ровно
             то, что этот автомат реально может выдать. Слоты, которые уже были (0 и 1),
             не трогаются: их разметка остаётся оригинальной.

Побочных эффектов при импорте нет (закон `tools-no-import-side-effects`).

Запуск:  python asda_build_premium_gatya_slots.py --slots 5 --copy-from 0 --csv --srv
"""
import argparse
import io
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_bintable as _BT     # noqa: E402  ★ дедуп №2 В1 Т5: общий счётчик/обход записей .bin
import asda_server_txt as _ST   # noqa: E402  ★ дедуп №2 В2 Т7: путь к живому серверу и чтение .txt как у сервера

import asda_paths as _P    # noqa: E402  ★ дедуп №2 В1 Т20: корень диска (ASDA_ROOT)
AREA       = _P.ROOT
PROJECT    = os.path.join(AREA, "AstoryClient")
# Оригинальная таблица цен клиента (та же, что серверная PrimiumGaChaPriceTable.txt).
ORIG_PRICE = os.path.join(AREA, 'Donors', 'AsdaChroniclesInterlude', 'Data', 'premium_gatya_data.bin')
# Живые серверные таблицы — ТОЛЬКО ЧТЕНИЕ, они же база для расширения.
LIVE_DIR   = _ST.LIVE_ITEMTABLE_INFO     # ★ дедуп №2 В2 Т7: один путь к живому серверу (ASDA_SERVER_V2)
LIVE_TABLE = os.path.join(LIVE_DIR, 'PrimiumGaChaTable.txt')
LIVE_PRICE = os.path.join(LIVE_DIR, 'PrimiumGaChaPriceTable.txt')
CSV_DIR    = os.path.join(PROJECT, 'RawAssets', 'Data', 'ItemDB')
CSV_POOL   = os.path.join(CSV_DIR, 'premium_gatya.csv')
CSV_PRICE  = os.path.join(CSV_DIR, 'premium_gatya_data.csv')

POOL_HEADER  = '_row,Index,SlotNumber,ItemCode,ViewSlot,Quantity'
PRICE_HEADER = '_row,Slot,Price'


# ───────────────────────────── чтение источников ─────────────────────────────

def read_orig_prices(path):
    """`premium_gatya_data.bin`: заголовок 20 б (счётчик @4 ВРЁТ — там 1 при двух записях),
       далее записи по 8 б {Slot i32, Price i32}."""
    with open(path, 'rb') as f:
        data = f.read()
    declared = _BT.record_count(data, signed=False)
    total = (len(data) - _BT.HDR) // 8
    rows = [struct.unpack_from('<2i', rec, 0) for _, rec in _BT.iter_records(data, 8, count=total)]
    return declared, rows


def _read_live_counted(path, nwords, name):
    """★ ИСПРАВЛЕНИЕ дедуп №2 В2 Т7: таблица гачи читается ТАК, КАК ЕЁ ЧИТАЕТ СЕРВЕР (`GameServer.c:75358-75399`,
    `:75436-75470`): число записей, затем записи по `nwords` слов потоком `fscanf("%s")` + `atol`.
    Сверка «заголовок == число записей» оставлена (её требует агент GP7 — п. 4 шапки): слов после счётчика обязано
    быть ровно `declared × nwords`. Было: разбор ПО СТРОКАМ и `int()` (падал на «1.5»/«12abc», сервер берёт префикс)."""
    toks = _ST.read_tokens(path)
    declared, recs, ts = _ST.read_counted_records(path, nwords, tokens=toks)
    have = len(toks) - 1
    if ts.short or have != declared * nwords:
        raise SystemExit('живая %s: заголовок %d, а записей по %d слов — %s' % (
            name, declared, nwords, ('%d' % (have // nwords)) if have % nwords == 0 else '%d слов' % have))
    if ts.lossy or any(not _ST.is_full_int(w) for r in recs for w in r):
        raise SystemExit('живая %s: есть слова, которые atol сервера усекает: %s' % (
            name, [w for r in recs for w in r if not _ST.is_full_int(w)][:10]))
    return recs


def read_live_pool(path):
    """Живая `PrimiumGaChaTable.txt`: счётчик, далее записи `index  iSlotNumber  itemCode  count`
       (+0 WORD, +2 BYTE, +3 DWORD, +7 WORD — ширины полей загрузчика сервера)."""
    return [(_ST.as_i16(_ST.crt_atol(r[0])), _ST.as_u8(_ST.crt_atol(r[1])), _ST.crt_atol(r[2]),
             _ST.as_i16(_ST.crt_atol(r[3]))) for r in _read_live_counted(path, 4, 'PrimiumGaChaTable.txt')]


def read_live_prices(path):
    """Живая `PrimiumGaChaPriceTable.txt`: счётчик + записи `slot  price` (+0 WORD, +2 DWORD)."""
    return [(_ST.as_i16(_ST.crt_atol(r[0])), _ST.crt_atol(r[1]))
            for r in _read_live_counted(path, 2, 'PrimiumGaChaPriceTable.txt')]


# ───────────────────────────── сборка ─────────────────────────────

def build_pool(live_rows, slots, copy_from):
    """Пул на `slots` автоматов: существующие слоты как есть, недостающие — копия слота
       `copy_from` (порядок и повторы сохраняются ⇒ вероятности выпадения те же)."""
    have = sorted({r[1] for r in live_rows})
    src = [r for r in live_rows if r[1] == copy_from]
    if not src:
        raise SystemExit('в живом пуле нет строк слота %d — копировать нечего' % copy_from)

    out = list(live_rows)
    added = {}
    for slot in range(slots):
        if slot in have:
            continue
        for r in src:
            out.append((0, slot, r[2], r[3]))     # индекс проставим сплошной нумерацией ниже
        added[slot] = len(src)

    # Индексы ОБЯЗАНЫ идти строго +1 от нуля (иначе «PrimiumGaChaTable.txt Sync Error»).
    out = [(i, r[1], r[2], r[3]) for i, r in enumerate(out)]
    return out, have, added


def build_prices(live_prices, orig_prices, slots, copy_from):
    """Цены на `slots` автоматов. Существующие берутся из живой таблицы (она уже сверена с
       оригиналом), недостающие — цена слота `copy_from`."""
    by_slot = dict(live_prices)
    orig_by_slot = dict(orig_prices)
    if copy_from not in by_slot:
        raise SystemExit('в живой price-таблице нет слота %d' % copy_from)
    price = by_slot[copy_from]
    out = []
    for slot in range(slots):
        if slot in by_slot:
            out.append((slot, by_slot[slot]))
            # Сверка с оригиналом там, где он что-то говорит (закон `always-verify-against-etalon`).
            if slot in orig_by_slot and orig_by_slot[slot] != by_slot[slot]:
                print('[премиум-гача] ВНИМАНИЕ: слот %d — цена сервера %d, оригинала %d'
                      % (slot, by_slot[slot], orig_by_slot[slot]))
        else:
            out.append((slot, price))
    return out


def write_server_files(out_dir, prices, pool):
    os.makedirs(out_dir, exist_ok=True)

    path = os.path.join(out_dir, 'PrimiumGaChaPriceTable.txt')
    lines = ['%d' % len(prices)] + ['%d\t%d' % (s, p) for s, p in prices]
    with io.open(path, 'w', encoding='ascii', newline='\r\n') as f:
        f.write('\n'.join(lines) + '\n')
    print('[премиум-гача] сервер: %s — слотов %d %s' % (path, len(prices), [s for s, _ in prices]))

    path = os.path.join(out_dir, 'PrimiumGaChaTable.txt')
    lines = ['%d' % len(pool)] + ['%d\t%d\t%d\t%d' % r for r in pool]
    with io.open(path, 'w', encoding='ascii', newline='\r\n') as f:
        f.write('\n'.join(lines) + '\n')
    print('[премиум-гача] сервер: %s — строк пула %d' % (path, len(pool)))


def verify_server_files(prices, pool):
    """Три серверные проверки + проверка GP7 — те самые, на которых сервер падает или молча
       отбрасывает запрос. Считаем ЗДЕСЬ, чтобы владелец не ловил это на живом сервере."""
    errs = []
    # 1. Индексы строго +1 от нуля (sub_4BCAD0).
    for i, r in enumerate(pool):
        if r[0] != i:
            errs.append('индекс строки %d = %d (ожидался %d) → «Sync Error»' % (i, r[0], i))
            break
    # 2. 0 <= iSlotNumber <= число строк price-таблицы (sub_4BC200).
    bad = sorted({r[1] for r in pool if r[1] < 0 or r[1] > len(prices)})
    if bad:
        errs.append('iSlotNumber вне 0..%d: %s' % (len(prices), bad))
    # 3. Каждому объявленному слоту нужен НЕПУСТОЙ пул, иначе GP7 вернёт missing-luckybox-pool.
    have = {r[1] for r in pool if r[2] > 0 and r[3] > 0}
    for s, _ in prices:
        if s not in have:
            errs.append('у слота %d нет ни одной строки пула' % s)
    # 4. GP7: declaredCount == число ВАЛИДНЫХ строк (itemCode > 0 и count > 0).
    valid = sum(1 for r in pool if r[2] > 0 and r[3] > 0)
    if valid != len(pool):
        errs.append('GP7: объявлено %d, валидных %d (строки с нулевым кодом/количеством)'
                    % (len(pool), valid))
    # 5. Слоты price-таблицы идут 0..N-1 (гейт сравнивает НОМЕР, а не ищет строку).
    if [s for s, _ in prices] != list(range(len(prices))):
        errs.append('слоты price-таблицы не подряд: %s' % [s for s, _ in prices])

    for e in errs:
        print('[премиум-гача] ★ ПРОВЕРКА НЕ ПРОЙДЕНА: ' + e)
    if not errs:
        print('[премиум-гача] проверки сервера пройдены: индексы 0..%d, слоты %s, строк %d'
              % (len(pool) - 1, sorted({r[1] for r in pool}), len(pool)))
    return not errs


def build_client_csv(pool, prices):
    """Клиентские CSV. Витрина существующих слотов сохраняется как была (оригинальная
       разметка `ViewSlot`), новым слотам витрина строится из их собственного пула."""
    # ── цены ──
    lines = [PRICE_HEADER] + ['%d,%d,%d' % (i, s, p) for i, (s, p) in enumerate(prices)]
    with io.open(CSV_PRICE, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    print('[премиум-гача] клиент: %s — %d строк цен' % (CSV_PRICE, len(prices)))

    # ── витрина ──
    with io.open(CSV_POOL, 'r', encoding='utf-8') as f:
        old = [l.rstrip('\r\n') for l in f if l.strip()]
    if old[0].count(',') != POOL_HEADER.count(','):
        raise SystemExit('премиум-гача: у %s неожиданный заголовок %r' % (CSV_POOL, old[0]))

    kept, seen_slots = [], set()
    for line in old[1:]:
        _row, index, slot, code, vslot, qty = line.split(',')
        kept.append((int(slot), int(code), int(vslot), int(qty)))
        seen_slots.add(int(vslot))

    # Уникальные коды пула по слотам — в порядке появления, количество от первой встречи.
    per_slot = {}
    for _, slot, code, cnt in pool:
        if code <= 0 or cnt <= 0:
            continue
        per_slot.setdefault(slot, [])
        if code not in [c for c, _ in per_slot[slot]]:
            per_slot[slot].append((code, cnt))

    rows = list(kept)
    added = {}
    for s, _ in prices:
        if s in seen_slots:
            continue
        for code, cnt in per_slot.get(s, []):
            rows.append((s, code, s, cnt))
        added[s] = len(per_slot.get(s, []))

    out = [POOL_HEADER]
    for i, (slot, code, vslot, qty) in enumerate(rows):
        out.append('%d,%d,%d,%d,%d,%d' % (i, i, slot, code, vslot, qty))
    with io.open(CSV_POOL, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(out) + '\n')

    by_view = {}
    for slot, code, vslot, qty in rows:
        by_view[vslot] = by_view.get(vslot, 0) + 1
    print('[премиум-гача] клиент: %s — всего %d строк, по витринам %s (дописано %s)'
          % (CSV_POOL, len(rows), dict(sorted(by_view.items())), added))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--slots', type=int, default=5, help='сколько автоматов должно быть (0..N-1)')
    ap.add_argument('--copy-from', type=int, default=0, help='у какого автомата копировать цену и пул')
    ap.add_argument('--out', default=None, help='куда класть серверные файлы')
    ap.add_argument('--csv', action='store_true', help='пересобрать клиентские CSV')
    ap.add_argument('--srv', action='store_true', help='собрать серверные txt')
    a = ap.parse_args()
    if not a.csv and not a.srv:
        a.csv = a.srv = True
    if a.slots < 1 or a.slots > 5:
        raise SystemExit('автоматов может быть 1..5: столько моделей `md_gachya_{a..e}_00`, '
                         'полос 68010+10i и цифр-скинов `GOLD_roullete_FONT3_{1..5}`')

    out_dir = a.out or os.path.join(AREA, '_re', 'reports', 'gacha_slots%d' % a.slots)
    live_pool = read_live_pool(LIVE_TABLE)
    live_prices = read_live_prices(LIVE_PRICE)
    orig_declared, orig_prices = read_orig_prices(ORIG_PRICE)
    print('[премиум-гача] оригинал `premium_gatya_data.bin`: записей %d, счётчик в заголовке %d '
          '(дефект оригинала) — цены %s' % (len(orig_prices), orig_declared, orig_prices))
    print('[премиум-гача] живой сервер: слотов цен %d, строк пула %d, слоты пула %s'
          % (len(live_prices), len(live_pool), sorted({r[1] for r in live_pool})))

    pool, have, added = build_pool(live_pool, a.slots, a.copy_from)
    prices = build_prices(live_prices, orig_prices, a.slots, a.copy_from)
    print('[премиум-гача] было слотов %s; добавлены копией слота %d: %s'
          % (have, a.copy_from, {k: '%d строк' % v for k, v in sorted(added.items())}))

    if not verify_server_files(prices, pool):
        raise SystemExit('премиум-гача: файлы НЕ записаны — сначала исправить проверки')

    if a.srv:
        write_server_files(out_dir, prices, pool)
    if a.csv:
        build_client_csv(pool, prices)
        # ★ 27.09.2026 (itemsdatafix, КРИТ-1): витрина жила только в premium_gatya.csv, и полный декод
        #   Bin.bbz 27.09 откатил её к 356 строкам архива. Сразу переносим результат в ведомости декодера
        #   (_value_overlay + _tail_append/premium_gatya.csv). premium_gatya_data.csv декодер не пишет.
        import sys as _sys
        _sys.path.insert(0, os.path.join(AREA, "AstoryClient", "Tools", "ItemDB"))
        import build_itemdb_poststep_ledgers as _L
        if _L.main(['--table', 'premium_gatya', '--write', '--replace']):
            raise SystemExit('премиум-гача: ведомость НЕ обновлена — разобрать до любого декода/переимпорта')


if __name__ == '__main__':
    main()
