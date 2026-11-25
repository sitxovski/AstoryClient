# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
asda_fill_item_names.py — ★★★ ЗАПРЕЩЁН (2026-09-27, itemsfix): при запуске СРАЗУ выходит с кодом 3.
  25.09.2026 финальная синхронизация прогнала его по устаревшей шапке decode_item_db.py и откатила
  1 559 названий предметов (восстановлены 27.09: 1 398 + 161). Имена применяет ТОЛЬКО
  AstoryClient\Tools\asda_apply_name_overlay.py. Код ниже сохранён как история, не вызывается.
УСТАРЕЛ (2026-08-09). Заменён парой
    Tools\asda_build_name_overlay.py  (генератор рецепта)
  + Tools\asda_apply_name_overlay.py  (применение рецепта ко ВСЕМ item-таблицам)
Новый слой покрывает не только item_list, но и item_tooltip / sowell_list / sowell_tooltip /
special_itemlist / specialitem_tooltiplist, и чинит не только пустые имена, но и текст,
принадлежащий другой записи. Запускать этот скрипт после нового слоя НЕ НАДО.

⚠ В старом источнике item_names_recovered.csv у 141 кода «именем» записано `NONE` —
это плейсхолдер, а не имя. Фильтр плейсхолдеров ниже добавлен, чтобы такой мусор
не попадал в таблицу (раньше попадал бы).

заполнение пустых имён предметов («-») в item_list.csv
из восстановленной выгрузки по языкам (приоритет ru > en > ar > ko).

ЗАЧЕМ. В RU-клиенте 43221 из 58878 предметов имеют имя «-» (русских названий для них
не существует ни в одном из 6 проверенных источников — см. RE/reports/item_names_recovered).
Имена найдены в EN/AR/KO-клиентах и в GameServer\ItemTable_Info\ItemName_Table.txt.

ЧТО ДЕЛАЕТ.
  * читает оверлей RawAssets/Data/ItemDB/item_names_recovered.csv
    (itemId,name,lang,source — UTF-8 BOM, языковой приоритет уже применён: каждый id ровно один раз);
  * потоково переписывает RawAssets/Data/ItemDB/item_list.csv,
    подставляя имя ТОЛЬКО там, где ItemName == '-' или пусто;
  * существующие имена (в т.ч. русские) НЕ трогает;
  * колонку DataHex НЕ трогает — это сырая запись оригинала 1:1 (ground truth),
    в ней имя так и остаётся «-»; ItemName — производная колонка отображения.

ИДЕМПОТЕНТЕН. Запускать ПОВТОРНО после любой регенерации таблиц
(Tools/decode_item_db.py перезаписывает item_list.csv из Bin.bbz и возвращает «-»),
затем переимпортировать DataTable: Tools/gen_itemdb_datatables.py (headless UE).

Запуск:
  python Tools\asda_fill_item_names.py            # применить
  python Tools\asda_fill_item_names.py --dry-run  # только статистика
"""
import argparse
import csv
import datetime
import io
import json
import os
import shutil
import sys

PROJECT   = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ITEMDB    = os.path.join(PROJECT, 'RawAssets', 'Data', 'ItemDB')
LIST_CSV  = os.path.join(ITEMDB, 'item_list.csv')
OVERLAY   = os.path.join(ITEMDB, 'item_names_recovered.csv')

csv.field_size_limit(1 << 30)   # DataHex — длинная строка (496 симв.)


def load_overlay(path):
    """itemId -> (name, lang, source). Языковой приоритет уже зашит в файл."""
    out = {}
    with io.open(path, 'r', encoding='utf-8-sig', newline='') as f:
        for row in csv.DictReader(f):
            iid = (row.get('itemId') or '').strip()
            if not iid.lstrip('-').isdigit():
                continue
            name = (row.get('name') or '').strip()
            # плейсхолдеры — не имена; список синхронизирован с asda_build_name_overlay.py
            if name.lower() in ('', '-', '-~', 'нет', 'none', '0', '_'):
                continue
            out[int(iid)] = (name, (row.get('lang') or '').strip(), (row.get('source') or '').strip())
    return out


def fill(list_csv, overlay, dry_run=False):
    stats = {'rows': 0, 'kept': 0, 'filled': 0, 'still_empty': 0}
    per_lang = {}
    tmp = list_csv + '.tmp'

    with io.open(list_csv, 'r', encoding='utf-8', newline='') as fi:
        rd = csv.reader(fi)
        header = next(rd)
        i_code = header.index('ItemCode')
        i_name = header.index('ItemName')

        fo = None if dry_run else io.open(tmp, 'w', encoding='utf-8', newline='')
        # CRLF — как пишет csv.DictWriter в Tools/decode_item_db.py (побайтовая совместимость)
        wr = None if dry_run else csv.writer(fo, lineterminator='\r\n')
        if wr:
            wr.writerow(header)

        for row in rd:
            stats['rows'] += 1
            name = (row[i_name] or '').strip()
            if name and name != '-':
                stats['kept'] += 1
            else:
                rec = overlay.get(int(row[i_code]))
                if rec:
                    row[i_name] = rec[0]
                    stats['filled'] += 1
                    per_lang[rec[1]] = per_lang.get(rec[1], 0) + 1
                else:
                    stats['still_empty'] += 1
            if wr:
                wr.writerow(row)

        if fo:
            fo.close()

    if not dry_run:
        os.replace(tmp, list_csv)
    return stats, per_lang


def fill_json(json_path, overlay):
    """То же для item_list.json (sibling-выгрузка decode_item_db.py, indent=0 — по строке на поле).

    Строка вида  "ItemCode": 20625,   идёт ПЕРЕД  "ItemName": "-",  — хватает простого автомата.
    """
    tmp = json_path + '.tmp'
    filled = 0
    cur_id = None
    with io.open(json_path, 'r', encoding='utf-8', newline='') as fi, \
         io.open(tmp, 'w', encoding='utf-8', newline='') as fo:
        for line in fi:
            s = line.lstrip()
            if s.startswith('"ItemCode":'):
                cur_id = int(s.split(':', 1)[1].strip().rstrip(','))
            elif s.startswith('"ItemName":') and cur_id is not None:
                val = s.split(':', 1)[1].strip().rstrip(',')
                if val in ('"-"', '""'):
                    rec = overlay.get(cur_id)
                    if rec:
                        # CRLF — json.dump в decode_item_db.py пишет через текстовый режим Windows
                        line = '"ItemName": %s,\r\n' % json.dumps(rec[0], ensure_ascii=False)
                        filled += 1
            fo.write(line)
    os.replace(tmp, json_path)
    return filled


# ★★★ 2026-09-27: единственная точка входа — отказ. Импорт модуля побочных эффектов не имеет.
REFUSAL = ('asda_fill_item_names.py ЗАПРЕЩЁН (27.09.2026): он откатывает названия предметов к старому слою\n'
           'item_names_recovered.csv (регресс 25.09: 1 559 имён). Имена предметов применять ТОЛЬКО так:\n'
           '  python AstoryClient\\Tools\\asda_apply_name_overlay.py [--dry-run] [--tables item_list,...]\n'
           '(после Tools\\decode_item_db.py в боевой каталог он накладывается автоматически).\n')


def main(argv=None):
    sys.stderr.write(REFUSAL)
    return 3


def _legacy_main_do_not_call(argv=None):
    """Прежняя логика (01.08). НЕ вызывать — см. REFUSAL."""
    ap = argparse.ArgumentParser()
    ap.add_argument('--dry-run', action='store_true', help='только статистика, файл не менять')
    ap.add_argument('--no-backup', action='store_true', help='не делать .bak-<дата>')
    ap.add_argument('--no-json', action='store_true', help='не обновлять item_list.json')
    args = ap.parse_args(argv)

    if not os.path.isfile(OVERLAY):
        sys.stderr.write('НЕТ оверлея: %s\n' % OVERLAY)
        return 2
    if not os.path.isfile(LIST_CSV):
        sys.stderr.write('НЕТ item_list.csv: %s\n' % LIST_CSV)
        return 2

    ov = load_overlay(OVERLAY)
    print('оверлей: %d имён' % len(ov))

    if not args.dry_run and not args.no_backup:
        bak = '%s.bak-%s' % (LIST_CSV, datetime.date.today().isoformat())
        if not os.path.exists(bak):
            shutil.copy2(LIST_CSV, bak)
            print('бэкап: %s' % bak)

    stats, per_lang = fill(LIST_CSV, ov, args.dry_run)
    print('строк: %(rows)d | уже с именем: %(kept)d | заполнено: %(filled)d | осталось «-»: %(still_empty)d' % stats)
    for lang in sorted(per_lang):
        print('  %s: %d' % (lang or '?', per_lang[lang]))

    json_path = os.path.join(ITEMDB, 'item_list.json')
    if not args.dry_run and not args.no_json and os.path.isfile(json_path):
        if not args.no_backup:
            bakj = '%s.bak-%s' % (json_path, datetime.date.today().isoformat())
            if not os.path.exists(bakj):
                shutil.copy2(json_path, bakj)
                print('бэкап: %s' % bakj)
        print('item_list.json: заполнено %d' % fill_json(json_path, ov))
    return 0


if __name__ == '__main__':
    sys.exit(main())
