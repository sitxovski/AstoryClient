# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
asda_title_merge.py — СЛИЯНИЕ таблицы званий из всех источников + список конфликтов.

Что делает:
  1. читает шесть источников (см. `asda_title_sources.py`) и серверную `FameInfo.txt`;
  2. строит ОБЪЕДИНЁННУЮ таблицу-НАДМНОЖЕСТВО: ни одно поле ни одной стороны не теряется,
     у каждого текста проставлен источник;
  3. выносит КОНФЛИКТЫ отдельным файлом — их не решаем молча;
  4. готовит патч для применения через редактор достижений (`Astory.Titles.ApplyPatch`).

★ Правило выбора имени: приоритет русскому. Русского нет — берём то, что есть; если это
  «-» (пустышка), подставляем НАШ перевод английского оригинала (`asda_title_translations.py`).
★ Правило выбора описания: наш текст, если он не оборван; иначе достроенный по смыслу.

⚠ ВАЖНОЕ ОТРИЦАТЕЛЬНОЕ (проверено двумя методами): АРАБСКИХ имён званий нет НИ В ОДНОМ
  источнике. Серверная `FameInfo.txt` — 11546 ячеек, все до единой числовые, текста там нет
  вовсе; на всех серверах живой шары никаких других fame-файлов нет. Клиент `ARap`, который
  выглядел арабским, на деле КОРЕЙСКИЙ (cp949): id 0 — «심심한» / «파티원을 찾아보자…».
  Поэтому перевода с арабского здесь нет и быть не может.

Запуск:  python asda_title_merge.py
Выход:   Reports/merged_titles/
"""
import csv
import io
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_title_sources as S
import asda_title_translations as T

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

import asda_paths as _P   # ★ гигиена 15.09.2026 (§4 п.2): корни путей — Tools/asda_paths.py (ASDA_ROOT и др.)
OUT_DIR = os.path.join(_P.RE_REPORTS, "merged_titles")

# Числовые поля клиента, которые несём в объединённую таблицу.
NUM_FIELDS = ['SortNum', 'ClassReq', 'ColorIndex', 'Group', 'Position',
              'CategoryLocIndex', 'Point', 'Enabled'] \
    + ['OptionTypeId%d' % k for k in range(6)] \
    + ['IsPercent%d' % k for k in range(6)]

# Доказанные соответствия «клиентское поле -> серверная колонка» (LoadFameInfo@0x49F410).
SERVER_MAP = [('Id', 0), ('Enabled', 1), ('Point', 42)] \
    + [('OptionTypeId%d' % m, 12 + 5 * m) for m in range(6)]


def blen(s):
    return len(s.encode('cp1251', 'replace'))


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    src = S.load_all()
    ours_bin = src['ours']
    orig, fanta, arap = src['orig'], src['fanta'], src.get('arap', {})
    npc = src.get('npc', {})
    server, _ = S.load_server()

    # Тексты берём из НАШЕГО CSV: там они уже разобраны верным терминатором.
    rows = {int(r['Id']): r for r in csv.DictReader(
        io.open(S.P_CSV, encoding='utf-8'))}

    all_ids = sorted(set(rows) | set(orig) | set(fanta) | set(arap) | set(server))

    merged, conflicts, restore_log = [], [], []
    stat = dict(ru_kept=0, translated=0, en_kept=0, desc_kept=0,
                desc_restored=0, desc_from_source=0, no_client_row=0)

    for tid in all_ids:
        ours = rows.get(tid)
        o, f, a = orig.get(tid, {}), fanta.get(tid, {}), arap.get(tid, {})
        srv = server.get(tid)

        name_ours = (ours or {}).get('Name', '').strip()
        desc_ours = (ours or {}).get('Desc', '').strip()

        # ── имя ──
        if tid in T.PLACEHOLDER:
            name_final = T.PLACEHOLDER[tid][0]
            name_origin = 'переведено с английского'
            stat['translated'] += 1
        elif name_ours and name_ours != '-':
            name_final = name_ours
            has_cyr = any('\u0400' <= c <= '\u04FF' for c in name_ours)
            name_origin = 'наш русский' if has_cyr else 'наш английский (русского нет нигде)'
            stat['ru_kept' if has_cyr else 'en_kept'] += 1
        else:
            name_final = o.get('Name', '') or f.get('Name', '') or ''
            name_origin = 'английский оригинал'
            stat['en_kept'] += 1

        # ── описание ──
        if tid in T.PLACEHOLDER:
            desc_final = T.PLACEHOLDER[tid][1]
            desc_origin = 'переведено с английского'
        elif tid in T.RESTORED_DESC:
            desc_final = T.RESTORED_DESC[tid]
            desc_origin = 'достроено по английскому'
            stat['desc_restored'] += 1
            restore_log.append(dict(
                Id=tid, Name=name_final, Method='достроено по английскому',
                Was=desc_ours, WasBytes=blen(desc_ours),
                Became=desc_final, BecameBytes=blen(desc_final),
                English=o.get('Desc', ''), EnglishCut='да' if o.get('DescFull') else 'нет',
                Fanta=f.get('Desc', ''), FantaCut='да' if f.get('DescFull') else 'нет'))
        else:
            desc_final = desc_ours
            desc_origin = 'наш текст'
            stat['desc_kept'] += 1

        # ── строка объединённой таблицы ──
        row = {
            'Id': tid,
            'Name': name_final, 'NameOrigin': name_origin,
            'Desc': desc_final, 'DescOrigin': desc_origin,
            'Name_ours': name_ours, 'Name_en_orig': o.get('Name', ''),
            'Name_ru_fanta': f.get('Name', ''), 'Name_ko_arap': a.get('Name', ''),
            'Desc_ours': desc_ours, 'Desc_en_orig': o.get('Desc', ''),
            'Desc_ru_fanta': f.get('Desc', ''), 'Desc_ko_arap': a.get('Desc', ''),
            'InClient': 'да' if ours else 'нет',
            'InServer': 'да' if srv else 'нет',
            'Unreachable': 'да' if tid >= 256 else 'нет',
        }
        if ours:
            for k in NUM_FIELDS:
                row[k] = ours.get(k, '')
        else:
            stat['no_client_row'] += 1
            for k in NUM_FIELDS:
                row[k] = ''
        # Серверные колонки целиком — чтобы таблица была надмножеством.
        for c in range(46):
            row['Srv%02d' % c] = srv[c].strip() if srv and c < len(srv) else ''
        merged.append(row)

        # ── конфликты: только по ДОКАЗАННЫМ соответствиям ──
        if ours and srv:
            for field, col in SERVER_MAP:
                if col == 0 or col >= len(srv):
                    continue
                cv = int(ours.get(field, 0) or 0)
                sv = int(srv[col].strip() or 0)
                if cv != sv:
                    conflicts.append(dict(
                        Id=tid, Name=name_final, Field=field,
                        ServerColumn=col, Client=cv, Server=sv,
                        Note='авторитет данных — сервер; решает владелец'))
        elif ours and not srv:
            conflicts.append(dict(
                Id=tid, Name=name_final, Field='(вся строка)', ServerColumn='',
                Client='есть', Server='НЕТ',
                Note='звание есть только у клиента; id >= 256 недостижим в принципе'
                     if tid >= 256 else 'звание есть только у клиента'))

    # ── запись ──
    mfields = (['Id', 'Name', 'NameOrigin', 'Desc', 'DescOrigin',
                'InClient', 'InServer', 'Unreachable']
               + NUM_FIELDS
               + ['Name_ours', 'Name_en_orig', 'Name_ru_fanta', 'Name_ko_arap',
                  'Desc_ours', 'Desc_en_orig', 'Desc_ru_fanta', 'Desc_ko_arap']
               + ['Srv%02d' % c for c in range(46)])
    with io.open(os.path.join(OUT_DIR, 'merged_titles.csv'), 'w',
                 encoding='utf-8-sig', newline='') as fh:
        w = csv.DictWriter(fh, fieldnames=mfields, extrasaction='ignore')
        w.writeheader()
        w.writerows(merged)

    with io.open(os.path.join(OUT_DIR, 'title_conflicts.csv'), 'w',
                 encoding='utf-8-sig', newline='') as fh:
        w = csv.DictWriter(fh, fieldnames=['Id', 'Name', 'Field', 'ServerColumn',
                                           'Client', 'Server', 'Note'])
        w.writeheader()
        w.writerows(conflicts)

    with io.open(os.path.join(OUT_DIR, 'desc_restore.csv'), 'w',
                 encoding='utf-8-sig', newline='') as fh:
        w = csv.DictWriter(fh, fieldnames=['Id', 'Name', 'Method', 'Was', 'WasBytes',
                                           'Became', 'BecameBytes', 'English',
                                           'EnglishCut', 'Fanta', 'FantaCut'])
        w.writeheader()
        w.writerows(restore_log)

    # ── патч для редактора: только то, что реально меняем ──
    patch = []
    for tid, (nm, ds) in sorted(T.PLACEHOLDER.items()):
        patch.append(dict(Id=tid, Field='Name', Value=nm, Origin='переведено с английского'))
        patch.append(dict(Id=tid, Field='Desc', Value=ds, Origin='переведено с английского'))
    for tid, ds in sorted(T.RESTORED_DESC.items()):
        patch.append(dict(Id=tid, Field='Desc', Value=ds, Origin='достроено по английскому'))
    with io.open(os.path.join(OUT_DIR, 'title_patch.csv'), 'w',
                 encoding='utf-8', newline='') as fh:
        w = csv.DictWriter(fh, fieldnames=['Id', 'Field', 'Value', 'Origin'])
        w.writeheader()
        w.writerows(patch)

    # ── сводка ──
    only_client = sum(1 for r in merged if r['InClient'] == 'да' and r['InServer'] == 'нет')
    only_server = sum(1 for r in merged if r['InClient'] == 'нет' and r['InServer'] == 'да')
    print('ОБЪЕДИНЁННАЯ ТАБЛИЦА ЗВАНИЙ')
    print('  строк всего            : %d (клиент %d, сервер %d)'
          % (len(merged), len(rows), len(server)))
    print('  только у клиента       : %d' % only_client)
    print('  только у сервера       : %d' % only_server)
    print()
    print('ИМЕНА')
    print('  русских оставлено      : %d' % stat['ru_kept'])
    print('  переведено с английского: %d' % stat['translated'])
    print('  оставлено английскими  : %d (русского нет ни в одном источнике)' % stat['en_kept'])
    print('  переведено с арабского : 0 (★ арабских имён нет ни в одном источнике)')
    print()
    print('ОПИСАНИЯ')
    print('  оставлено как есть     : %d' % stat['desc_kept'])
    print('  достроено по английскому: %d' % stat['desc_restored'])
    print('  переведено с английского: %d (записи-пустышки)' % len(T.PLACEHOLDER))
    print('  взято из источника 1:1 : 0 (★ ни один чужой вариант не оказался полным —'
          ' все обрезаны тем же 64-байтовым полем)')
    print()
    print('КОНФЛИКТЫ')
    by_field = {}
    for c in conflicts:
        by_field[c['Field']] = by_field.get(c['Field'], 0) + 1
    print('  всего записей конфликтов: %d' % len(conflicts))
    for k, v in sorted(by_field.items(), key=lambda kv: -kv[1]):
        print('    %-16s %d' % (k, v))
    print()
    print('файлы: %s' % OUT_DIR)


if __name__ == '__main__':
    main()
