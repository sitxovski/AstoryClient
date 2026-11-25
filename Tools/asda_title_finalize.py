# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
asda_title_finalize.py — ПРИВЕДЕНИЕ ВСЕХ ЗВАНИЙ В ФИНАЛЬНЫЙ ВИД.

Принимает решение по КАЖДОМУ расхождению клиент ↔ сервер и по каждому оставшемуся тексту,
после чего готовит один патч для применения через редактор (`Astory.Titles.ApplyPatch`).

ПРАВИЛА РЕШЕНИЙ (подтверждены владельцем, памятка `client-server-table-desync`):
  * числовые значения — авторитет СЕРВЕР;
  * тексты — приоритет русским клиентским; арабских имён званий не существует (доказано);
  * исключение допустимо, только если серверное значение явно битое — и должно быть обосновано.

РАЗБОР СЕРВЕРНОГО ФОРМАТА (по загрузчику `sub_4A18F0 @ 0x4A18F0`, запись 96 б):
    кол. 0  -> +0  WORD   Id
    кол. 1  -> +2  BYTE   ★ ФЛАГ: пишется как `atol(...) != 0`, то есть 1 и 2 неразличимы
    кол. 2  -> +3  BYTE
    кол. 3  -> +4  DWORD  ★ ЦЕЛЕВОЙ ПОРОГ: звание получено, когда прогресс >= этого числа
    кол. 4  -> +8  DWORD  параметр условия (уровень/пол/счётчик — зависит от ветки)
    кол. 5..9   -> +12+2j WORD  (5 шт.)
    кол. 10..11 -> +22+2k WORD  (2 шт.)
    кол. 12..41 -> +26+10m+{0,2,4,6,8} WORD (6 слотов по 5)
    кол. 42 -> +86 WORD   очки славы
    кол. 43 -> +88 BYTE   ★ РЕЖИМ ВТОРОГО ПОРОГА: 0 не используется, 1 абсолютный, 2 проценты
    кол. 44 -> +90 WORD   ★ ВТОРОЙ ПОРОГ (абсолютное значение либо проценты от кол. 3)
    кол. 45 -> +92 BYTE   ★ 1 = универсальный накопитель прогресса (без хардкода в EXE)
Проверка порога — `if (a4 >= rec+4)`, второй порог — `a4 >= rec+90` либо
`rec+90 <= a4*100/rec+4`. Это ровно две маски системы: «получено» и «найдено».

Побочных эффектов при импорте нет.
Запуск:  python asda_title_finalize.py
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


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    srv, _ = S.load_server()
    rows = {int(r['Id']): r for r in csv.DictReader(io.open(S.P_CSV, encoding='utf-8'))}
    common = sorted(set(srv) & set(rows))

    decisions, patch = [], []

    def decide(tid, field, client, server, winner, reason):
        decisions.append(dict(Id=tid, Name=rows[tid]['Name'], Field=field,
                              Client=client, Server=server, Decision=winner, Reason=reason))

    # ── 1. Enabled (кол. 1) ──
    for tid in common:
        cv, sv = int(rows[tid]['Enabled']), int(srv[tid][1])
        if cv == sv:
            continue
        if (cv != 0) == (sv != 0):
            # Сервер хранит поле как флаг `!= 0`, поэтому 1 и 2 для него одно и то же.
            #   Поведение не меняется; выравниваем к серверу ради точной симметрии 1:1.
            reason = ('не настоящий конфликт: сервер хранит кол.1 как флаг (atol != 0), '
                      'значения 1 и 2 для него неразличимы; выравнено к серверу ради симметрии')
        else:
            reason = ('авторитет сервер: сервер выключил звание (кол.1 = 0) — он никогда его '
                      'не выдаст, а клиент показывал его в списке')
        decide(tid, 'Enabled', cv, sv, 'сервер', reason)
        patch.append(dict(Id=tid, Field='Enabled', Value=sv, Origin='конфликт: авторитет сервер'))

    # ── 2. Point (кол. 42) ──
    for tid in common:
        cv, sv = int(rows[tid]['Point']), int(srv[tid][42].strip())
        if cv == sv:
            continue
        if sv == 0:
            # Проверено: у ВСЕХ таких записей сервер одновременно держит кол.1 = 0.
            #   Ноль очков у выключенного звания согласован, признаков порчи нет.
            assert int(srv[tid][1]) == 0
            reason = ('авторитет сервер: ноль согласован — у этой же записи сервер держит '
                      'кол.1 = 0 (звание выключено), значит 0 очков не порча, а следствие')
        else:
            reason = 'авторитет сервер: обе стороны ненулевые, расхождение данных'
        decide(tid, 'Point', cv, sv, 'сервер', reason)
        patch.append(dict(Id=tid, Field='Point', Value=sv, Origin='конфликт: авторитет сервер'))

    # ── 3. Записи, которых у сервера нет ──
    for tid in sorted(set(rows) - set(srv)):
        if tid < S.SERVER_MAX_ROWS_HINT:
            reason = 'закрывается достройкой серверной таблицы (влезает в предел загрузчика 256)'
            winner = 'достроено на сервере'
        else:
            reason = ('достраивается только после ДВУХ патчей сервера: маска 256->512 и '
                      'предел count<=256 в sub_4A18F0@0x4A18F0')
            winner = 'ждёт патчей сервера'
        decide(tid, '(вся строка)', 'есть', 'НЕТ', winner, reason)

    # ── 4. Тексты ──
    for tid, (nm, ds) in sorted(T.PLACEHOLDER.items()):
        patch.append(dict(Id=tid, Field='Name', Value=nm, Origin='переведено с английского'))
        patch.append(dict(Id=tid, Field='Desc', Value=ds, Origin='переведено с английского'))
    for tid, ds in sorted(T.RESTORED_DESC.items()):
        patch.append(dict(Id=tid, Field='Desc', Value=ds, Origin='достроено по английскому'))
    for tid, (nm, ds) in sorted(T.LATIN_SERIES.items()):
        patch.append(dict(Id=tid, Field='Name', Value=nm, Origin='переведено с английского'))
        if ds:
            patch.append(dict(Id=tid, Field='Desc', Value=ds, Origin='переведено с английского'))
    for tid, ds in sorted(T.PUNCT_FIX.items()):
        patch.append(dict(Id=tid, Field='Desc', Value=ds, Origin='дописана точка, смысл не менялся'))

    # ── запись ──
    with io.open(os.path.join(OUT_DIR, 'title_decisions.csv'), 'w',
                 encoding='utf-8-sig', newline='') as fh:
        w = csv.DictWriter(fh, fieldnames=['Id', 'Name', 'Field', 'Client', 'Server',
                                           'Decision', 'Reason'])
        w.writeheader()
        w.writerows(decisions)

    with io.open(os.path.join(OUT_DIR, 'title_patch.csv'), 'w',
                 encoding='utf-8', newline='') as fh:
        w = csv.DictWriter(fh, fieldnames=['Id', 'Field', 'Value', 'Origin'])
        w.writeheader()
        w.writerows(patch)

    # ── сводка ──
    n_en = sum(1 for d in decisions if d['Field'] == 'Enabled')
    n_pt = sum(1 for d in decisions if d['Field'] == 'Point')
    n_row = sum(1 for d in decisions if d['Field'] == '(вся строка)')
    n_fake = sum(1 for d in decisions if 'не настоящий конфликт' in d['Reason'])
    print('РЕШЕНИЯ ПО КОНФЛИКТАМ')
    print('  Enabled (кол.1)        : %d -> сервер (из них %d — не настоящий конфликт: флаг !=0)'
          % (n_en, n_fake))
    print('  Point   (кол.42)       : %d -> сервер' % n_pt)
    print('  нет строки у сервера   : %d (достройка)' % n_row)
    print('  решено в пользу клиента: 0  (исключений не нашлось — обоснование в отчёте)')
    print('  ВСЕГО решений          : %d' % len(decisions))
    print()
    print('ПАТЧ ТЕКСТОВ И ЗНАЧЕНИЙ')
    by = {}
    for p in patch:
        by[p['Origin']] = by.get(p['Origin'], 0) + 1
    for k, v in sorted(by.items(), key=lambda kv: -kv[1]):
        print('  %-42s %d' % (k, v))
    print('  ВСЕГО строк патча      : %d' % len(patch))
    print()
    print('файлы: %s' % OUT_DIR)


if __name__ == '__main__':
    main()
