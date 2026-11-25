# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
asda_name_layer_gate.py — ОБЩИЙ гейт «слой-рецепт имён применён полностью» для импортёров item-БД.

ЗАЧЕМ (27.09.2026, доп.задача координатора по `items_consistency_fix_REVIEW.md` СРЕДН-1).
  25.09 таблицу предметов перегенерировали устаревшим `asda_fill_item_names.py` вместо слоя-рецепта
  и импортировали — 1 559 имён откатились, и никто не заметил. Гейт `_import_one_itemdb_table.py`
  ловит это, НО в порту ещё 4 импортёра item-таблиц без гейта (`gen_itemdb_datatables.py`,
  `asda_reimport_item_tables.py`, `asda_reimport_item_list.py`, `asda_reimport_itemdb_union.py`).
  Здесь гейт вынесен в ОДНУ точку (не копия), чтобы все пять импортёров звали его перед импортом
  таблиц, покрытых рецептом (item_list, item_tooltip, sowell_list, sowell_tooltip, special_itemlist,
  specialitem_tooltiplist).

БЕЗ САЙД-ЭФФЕКТОВ НА ИМПОРТЕ (`tools-no-import-side-effects`): при импорте модуль ничего не делает.
  Проверка выполняется только при вызове `name_layer_gate(...)`. `unreal` не импортируется — логи идут
  через переданные колбэки `log/warn/err` (импортёр даёт свои, поверх `unreal.log*`).

Осознанный обход (только с разрешения владельца): ASDA_NAME_LAYER_GATE=0.
"""
import os
import sys


def gate_enabled():
    """Гейт включён, если ASDA_NAME_LAYER_GATE != '0'."""
    return os.environ.get('ASDA_NAME_LAYER_GATE', '1').strip() != '0'


def name_layer_gate(table, csv_dir, log=None, warn=None, err=None):
    """True — слой-рецепт имён для `table` применён полностью к CSV в `csv_dir`
    (или таблица рецептом не покрыта / гейт выключен). False — импорт ЭТОЙ таблицы отменить.

    csv_dir  — каталог боевых CSV, которые будут импортированы (тот же, что читает импортёр).
    log/warn/err — колбэки (по одному строковому аргументу). None -> print.
    """
    log = log or (lambda m: print(m))
    warn = warn or log
    err = err or log
    if not gate_enabled():
        warn('%s: гейт слоя имён ВЫКЛЮЧЕН (ASDA_NAME_LAYER_GATE=0)' % table)
        return True
    here = os.path.dirname(os.path.abspath(__file__))
    if here not in sys.path:
        sys.path.append(here)
    import asda_apply_name_overlay as NO          # без сайд-эффектов на импорте
    if table not in NO.KEY_COLUMN:
        return True
    if not os.path.isfile(NO.OVERLAY):
        err('%s: нет рецепта имён %s — импорт отменён' % (table, NO.OVERLAY))
        return False
    ov, _n = NO.load_overlay(NO.OVERLAY, {table})
    if table not in ov:
        return True
    # ★ 27.09 (itemsdatafix, МЕЛК-4 ревизии items_data): каталог модуля наложения подменяется ТОЛЬКО на
    #   время проверки и возвращается в finally — иначе последующие вызовы asda_apply_name_overlay в том же
    #   процессе молча работали бы с чужим каталогом.
    prev_itemdb = NO.ITEMDB
    NO.ITEMDB = csv_dir                            # проверяем ровно тот CSV, который будет импортирован
    try:
        st, _applied = NO.apply_csv(table, ov[table], True)
    finally:
        NO.ITEMDB = prev_itemdb
    # ★ 24.10.2026 [ИСПРАВЛЕНИЕ] (fable_shop): критерий — ТОТ ЖЕ, что у самого наложения (`asda_apply_name_overlay.main`,
    #   MELK-1): «дубль ключа: записей сверх первой» — информационный счётчик (с 14.10 правка ключа-дубля ложится на
    #   КАЖДУЮ его запись), а не конфликт. Гейт этого не знал и с 14.10 отбивал любой импорт item_tooltip (5 кодов
    #   56311/56316/56326/56331/56336 по две записи). КОНФЛИКТ / НЕТ КЛЮЧА / НЕТ КОЛОНКИ / «ещё не применено» ловятся по-прежнему.
    bad = {k: v for k, v in st.items() if k != 'уже применено' and not k.startswith('дубль ключа')}
    if bad:
        err('%s: слой имён применён НЕ полностью %s — импорт отменён. Запусти '
            'AstoryClient\\Tools\\asda_apply_name_overlay.py и разбери конфликты.' % (table, bad))
        return False
    log('%s: гейт слоя имён пройден (применено %d из %d)'
        % (table, st.get('уже применено', 0), len(ov[table])))
    return True
