# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
asda_apply_name_overlay.py — ПРИМЕНЕНИЕ слоя-рецепта имён/описаний к игровым таблицам.

Пишет ПЕРМАНЕНТНО в RawAssets/Data/ItemDB/<table>.csv (+ .json).
Никаких подстановок в рантайме: порт читает уже исправленные данные.

ПОРЯДОК РАБОТ (после любой регенерации таблиц из Bin.bbz):
  1) python Tools\decode_item_db.py --out <папка> --only <т>.bin   # ★ дедуп №2 В2 Т9: ТОЛЬКО --out (полный декод в
     боевой каталог запрещён — 27.09 он стёр гачу/звания/опыт; сам декодер его отклоняет), в боевой каталог —
     лишь нужные таблицы; переимпорт DT — ASDA_TABLES=<т> Tools/gen_itemdb_datatables.py
     (или его обёртка Tools/_import_one_itemdb_table.py)
  2) python Tools\asda_build_name_overlay.py             # пересобрать рецепт (по желанию)
  3) python Tools\asda_apply_name_overlay.py             # наложить рецепт  <-- этот скрипт
  4) $env:ASDA_TABLES='item_list,item_tooltip,…'; headless-импорт DataTable

ИДЕМПОТЕНТНОСТЬ. Для каждой ячейки в рецепте записаны и старое, и новое значение:
    текущее == new_value  -> уже применено, пропуск (счётчик «уже»)
    текущее == old_value  -> применяем
    иначе                 -> КОНФЛИКТ: не трогаем, печатаем (кто-то правил файл руками
                             или изменился клиент). Повторный прогон ничего не портит.

ЗАЩИТА ОТ РЕГРЕССА. После правки построчно сверяются ВСЕ колонки: измениться имеют право
только те ячейки, которые перечислены в рецепте. Любое иное различие -> откат и ошибка.
(Ровно на этом обжигались раньше: смотрели только имена и не заметили обнулённые числа.)

Запуск:
  python AstoryClient\Tools\asda_apply_name_overlay.py
  python ... --dry-run                 # ничего не писать
  python ... --tables item_list,…      # только указанные таблицы
  python ... --reasons ru_missing      # только указанные причины
"""
import argparse
import collections
import csv
import datetime
import io
import json
import os
import shutil
import sys

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ITEMDB = os.path.join(PROJECT, 'RawAssets', 'Data', 'ItemDB')
# ⚠ Рецепт — ИНСТРУМЕНТАЛЬНЫЙ файл, а не игровой ассет: в `Content/` движок подхватывает его
#   как импортируемый и показывает в браузере контента (`/Game/AsdaAssets/Data/ItemDB/…`).
#   Держим в `Tools/_out/`, рядом с прочими промежуточными данными.
OVERLAY = os.path.join(PROJECT, 'Tools', '_out', 'itemdb_overlay', 'item_name_overlay.csv')

csv.field_size_limit(1 << 30)          # DataHex — длинная строка

# таблица -> имя колонки первичного ключа в CSV
KEY_COLUMN = {
    'item_list':               'ItemCode',
    'item_tooltip':            'ItemCode',
    'sowell_list':             'Id',
    'sowell_tooltip':          'Id',
    'special_itemlist':        'Code',
    'specialitem_tooltiplist': 'Id',
    # ★ 2026-10-14 (names11, этап 2): таблицы с обрезанными лимитом поля именами (skilltooltip/stateinfotip: char[32]) и
    #   с корейским текстом в RU-сборке (названия бонусов званий, баффов, событий, навыков Друга).
    'skilltooltip':            'Id',
    'stateinfotip':            'Id',
    'title_option':            'Id',
    'specialitem_buftooltiplist': 'Id',
    'event_item_list':         'Idx',
    'soulmateskill':           'Id',
    # ★ 2026-10-14 (names11, этап 4): английские форматы бонусов питомцев и латинская буква-двойник в названии навыка.
    'petoptiontable':          'Id',
    'skill_info':              'SkillCode',
}


def _ledger():
    """Общий модуль ведомостей `asda_ledger` (★ дедуп №2 В2 Т8); импорт при первом обращении."""
    here = os.path.dirname(os.path.abspath(__file__))
    if here not in sys.path:
        sys.path.insert(0, here)
    import asda_ledger
    return asda_ledger


def load_overlay(path, only_tables=None, only_reasons=None):
    """-> {table: {(key, column): row}} + список строк в исходном порядке"""
    out = collections.OrderedDict()
    n = 0
    for r in _ledger().name_overlay_entries(path):     # ★ дедуп №2 В2 Т8: общий читатель (utf-8-sig, все колонки)
        t = (r.get('table') or '').strip()
        if not t or (only_tables and t not in only_tables):
            continue
        if only_reasons and (r.get('reason') or '').strip() not in only_reasons:
            continue
        k = (r.get('key') or '').strip()
        c = (r.get('column') or '').strip()
        if not k or not c:
            continue
        out.setdefault(t, collections.OrderedDict())[(k, c)] = r
        n += 1
    return out, n


def apply_csv(table, edits, dry_run):
    """Возвращает (stats, {ключ: (column, new)}) — что реально изменено."""
    path = os.path.join(ITEMDB, table + '.csv')
    if not os.path.isfile(path):
        return {'НЕТ ФАЙЛА': 1}, {}
    keycol = KEY_COLUMN[table]

    with io.open(path, 'r', encoding='utf-8', newline='') as f:
        rows = list(csv.reader(f))
    header, body = rows[0], rows[1:]
    ik = header.index(keycol)
    col_idx = {c: i for i, c in enumerate(header)}

    # ★ 14.10.2026 (этап 5 «дефекты данных», п. 5): ключ -> ВСЕ строки с этим ключом. В item_tooltip у 5 ItemCode
    #   (56311/56316/56326/56331/56336) по две записи (_row 35503… и 35760…; в .bin это сбитые слоты штанов Вуд Форк).
    #   Раньше словарь оставлял последнюю строку, и правка рецепта молча ложилась только на неё, а первую закрывала
    #   отдельная строка ведомости значений. Теперь правка ключа-дубля ложится на каждую запись (решение — по каждой
    #   ячейке отдельно, `classify`), число таких записей видно в счётчике «дубль ключа». Игра (и оригинал, и порт)
    #   показывает ПОСЛЕДНЮЮ запись кода (`AsdaItemTableSubsystem::BuildKeyIndex` — последняя строка; эталон
    #   `itemTemplateFindByCode@0xB733F0` — слот code-20551), но переведены и переживают повторный декод обе.
    by_key = collections.OrderedDict()
    for i, row in enumerate(body):
        by_key.setdefault(row[ik].strip(), []).append(i)

    st = collections.Counter()
    applied = {}
    changed_cells = set()          # (строка, колонка), которым РАЗРЕШЕНО измениться
    for (key, column), e in edits.items():
        if column not in col_idx:
            st['НЕТ КОЛОНКИ ' + column] += 1
            continue
        ris = by_key.get(key)
        if not ris:
            st['НЕТ КЛЮЧА'] += 1
            continue
        if len(ris) > 1:
            st['дубль ключа: записей сверх первой'] += len(ris) - 1
        ci = col_idx[column]
        new = e['new_value']
        old = e['old_value']
        for ri in ris:
            cur = body[ri][ci]
            # ★ дедуп №2 В2 Т8: решение по ячейке — общее `asda_ledger.classify` (та же семантика: уже / было / иначе конфликт).
            cls = _ledger().classify(cur, new, old)
            if cls == _ledger().ALREADY:
                st['уже применено'] += 1
                continue
            if cls != _ledger().APPLY:
                st['КОНФЛИКТ'] += 1
                print('  КОНФЛИКТ %s[%s].%s (строка %d): в файле %r, в рецепте ожидалось %r'
                      % (table, key, column, ri, cur[:60], old[:60]))
                continue
            body[ri][ci] = new
            changed_cells.add((ri, ci))
            applied.setdefault(key, []).append((column, new))
            st['применено/' + e.get('reason', '?') + '/' + e.get('source', '?')] += 1

    # --- сверка по ВСЕМ колонкам: изменились только разрешённые ячейки ---
    with io.open(path, 'r', encoding='utf-8', newline='') as f:
        ref = list(csv.reader(f))[1:]
    if len(ref) != len(body):
        raise RuntimeError('%s: изменилось число строк (%d -> %d)' % (table, len(ref), len(body)))
    bad = 0
    for ri, (a, b) in enumerate(zip(ref, body)):
        if len(a) != len(b):
            raise RuntimeError('%s: строка %d изменила число колонок' % (table, ri))
        for ci, (x, y) in enumerate(zip(a, b)):
            if x != y and (ri, ci) not in changed_cells:
                bad += 1
                if bad <= 5:
                    print('  РЕГРЕСС %s строка %d колонка %s: %r -> %r'
                          % (table, ri, header[ci], x[:40], y[:40]))
    if bad:
        raise RuntimeError('%s: %d ячеек изменились вне рецепта — запись отменена' % (table, bad))

    if not dry_run and changed_cells:
        stamp = datetime.datetime.now().strftime('%Y%m%d')
        bak = '%s.bak_names_%s' % (path, stamp)
        if not os.path.exists(bak):
            shutil.copy2(path, bak)
        tmp = path + '.tmp'
        with io.open(tmp, 'w', encoding='utf-8', newline='') as f:
            w = csv.writer(f, lineterminator='\r\n')
            w.writerow(header)
            w.writerows(body)
        os.replace(tmp, path)
    return st, applied


CRLF = chr(13) + chr(10)      # перевод строки Windows
LF = chr(10)


def apply_json(table, applied, dry_run):
    """Тот же набор правок в sibling-выгрузке .json (decode_item_db пишет indent=0 - ровно по строке на поле).

    ★ 2026-10-14 (names11): прежний потоковый разбор считал, что колонка-ключ идёт в записи ПЕРВОЙ; у sowell_list,
      specialitem_buftooltiplist, soulmateskill первым идёт Name, и правка ложилась в имя СЛЕДУЮЩЕЙ записи (сдвиг на одну
      запись; у sowell_list.json так испорчено 1 253 имени ещё до этой правки). Теперь файл читается как JSON, правится по
      значению ключа и пишется в том же виде (indent=0, CRLF). Если «круг» без правок не даёт исходный текст - файл не трогается."""
    path = os.path.join(ITEMDB, table + '.json')
    if not os.path.isfile(path) or not applied or dry_run:
        return 0
    keycol = KEY_COLUMN[table]
    stamp = datetime.datetime.now().strftime('%Y%m%d')
    bak = '%s.bak_names_%s' % (path, stamp)
    if not os.path.exists(bak):
        shutil.copy2(path, bak)
    with io.open(path, 'r', encoding='utf-8', newline='') as f:
        text = f.read()
    data = json.loads(text)
    nl = CRLF if CRLF in text[:200] else LF
    if json.dumps(data, ensure_ascii=False, indent=0).replace(LF, nl) != text:
        print('  ! %s.json: круг json.dumps(indent=0) не даёт исходный текст - файл НЕ тронут' % table)
        return 0
    by = collections.defaultdict(list)          # ★ 14.10.2026 (этап 5, п. 5): ключ -> все записи (дубли ItemCode)
    for r in data:
        by[str(r.get(keycol)).strip()].append(r)
    n = 0
    for key, edits in applied.items():
        for r in by.get(str(key).strip(), []):
            for col, new in edits:
                if col in r:
                    r[col] = new
                    n += 1
    tmp = path + '.tmp'
    with io.open(tmp, 'w', encoding='utf-8', newline='') as fo:
        fo.write(json.dumps(data, ensure_ascii=False, indent=0).replace(LF, nl))
    os.replace(tmp, path)
    return n


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--overlay', default=OVERLAY)
    ap.add_argument('--tables', default='')
    ap.add_argument('--reasons', default='')
    ap.add_argument('--no-json', action='store_true')
    # ★★ 2026-09-27 (itemsfix). Каталог таблиц: по умолчанию боевой RawAssets/Data/ItemDB. Отдельный
    #   каталог нужен, чтобы прогнать штатный конвейер «декод в ОТДЕЛЬНУЮ папку (--out) → рецепт» и
    #   сравнить результат с боевыми таблицами, не трогая их (так восстановлены 161 ячейка регресса 25.09).
    ap.add_argument('--itemdb', default='', help='каталог CSV/JSON таблиц (по умолчанию боевой ItemDB порта)')
    a = ap.parse_args(argv)
    if a.itemdb:
        global ITEMDB
        ITEMDB = os.path.abspath(a.itemdb)
        print('каталог таблиц: %s' % ITEMDB)

    if not os.path.isfile(a.overlay):
        sys.stderr.write('нет рецепта: %s\n' % a.overlay)
        return 2
    only_t = {x.strip() for x in a.tables.split(',') if x.strip()} or None
    only_r = {x.strip() for x in a.reasons.split(',') if x.strip()} or None
    ov, n = load_overlay(a.overlay, only_t, only_r)
    print('рецепт: %s — %d правок, таблиц %d%s' %
          (a.overlay, n, len(ov), ' (ПРОБНЫЙ ПРОГОН)' if a.dry_run else ''))

    total = collections.Counter()
    for table in ov:
        if table not in KEY_COLUMN:
            print('  ПРОПУСК %s: ключ неизвестен' % table)
            continue
        st, applied = apply_csv(table, ov[table], a.dry_run)
        jn = 0 if a.no_json else apply_json(table, applied, a.dry_run)
        print('\n%s: %s%s' % (table, dict(st), ('  json-полей: %d' % jn) if jn else ''))
        for k, v in st.items():
            total[k] += v

    print('\n=== ИТОГО ===')
    for k in sorted(total):
        print('  %-46s %6d' % (k, total[k]))
    # ★ 2026-09-27 (MELK-1): ненулевой код возврата, если рецепт применён НЕ полностью —
    #   КОНФЛИКТ / НЕТ КЛЮЧА / НЕТ КОЛОНКИ / НЕТ ФАЙЛА. Иначе декодер (`if rc:`) не заметит
    #   конфликт, и его ловил бы только гейт импортёра. «уже применено» и «применено/…» — норма.
    bad = {k: v for k, v in total.items()
           if not (k == 'уже применено' or k.startswith('применено') or k.startswith('дубль ключа'))}
    if bad:
        sys.stderr.write('слой имён применён НЕ полностью: %s\n' % dict(bad))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
