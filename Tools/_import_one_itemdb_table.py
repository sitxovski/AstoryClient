# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
_import_one_itemdb_table.py — headless UE5 Python: импорт ОДНОЙ (или нескольких перечисленных)
таблиц item-БД из CSV в DataTable /Game/AsdaAssets/Data/ItemDB/DT_<table>.

Зачем отдельный скрипт: `gen_itemdb_datatables.py` переимпортирует ВСЕ 52 таблицы (~12 мин и
перезапись чужих ассетов). Когда в реестр добавлена одна новая таблица, нужно тронуть только её.

Список таблиц — константа TABLES ниже (имена как в Tools/_out/itemdb_manifest.json).
Побочных эффектов при импорте модуля нет (закон `tools-no-import-side-effects`): работа
выполняется только при прямом запуске.

Запуск:  Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\_import_one_itemdb_table.py
"""
import unreal, os

# Какие таблицы импортировать (имя 'table' из манифеста).
#   Задаются переменной окружения ASDA_TABLES через запятую, например:
#     $env:ASDA_TABLES = 'special_itemlist,premiumleveltable'
#   ⚠ Раньше список был зашит константой, и скрипт МОЛЧА импортировал не то, что просили.
#   ★ дедуп №2 В2 Т9: умолчания больше нет — без ASDA_TABLES скрипт отказывает (см. main()).
# ★ 2026-09-06 ГЕЙТ «изменились только ожидаемые строки». ASDA_DIFF_COLS=колонки через запятую:
#   перед импортом снимается снимок этих колонок у СУЩЕСТВУЮЩЕГО DataTable, после — второй снимок,
#   в лог идут все изменившиеся строки (имя строки + было→стало) и их число, плюс сверка «после» с CSV
#   по тем же колонкам. Пусто = гейт выключен (поведение прежнее). Придумано для точечной правки
#   item_img_table (дефект оригинала 41827…, `Tools/decode_item_db.py` → ORIGINAL_DEFECT_FIXES).
DIFF_COLS = [c.strip() for c in os.environ.get('ASDA_DIFF_COLS', '').split(',') if c.strip()]
# ★★★ 2026-09-27 (itemsfix, СРЕДН-2 ревизии items_consistency). ГЕЙТ СЛОЯ ИМЁН. 25.09 таблицу предметов
#   перегенерировали устаревшим asda_fill_item_names.py вместо слоя-рецепта и импортировали — 1 559 имён
#   откатились, и никто этого не заметил. Теперь перед импортом таблиц, которые покрывает рецепт
#   (item_list, item_tooltip, sowell_list, sowell_tooltip, special_itemlist, specialitem_tooltiplist),
#   рецепт прогоняется «вхолостую» (`asda_apply_name_overlay.apply_csv(..., dry_run=True)`, тот же код):
#   если хоть одна ячейка «ещё не применена» или КОНФЛИКТ / НЕТ КЛЮЧА — импорт ЭТОЙ таблицы отменяется
#   с ошибкой в логе. Лечение: `python AstoryClient\Tools\asda_apply_name_overlay.py` и разбор конфликтов.
#   Осознанный обход (только с разрешения владельца): ASDA_NAME_LAYER_GATE=0 (читает общий модуль
#   `asda_name_layer_gate`, куда 2026-09-27 вынесена логика гейта — зовёт `gen_itemdb_datatables.name_gate`).


def log(m):  unreal.log('[itemdb-dt-one] ' + m)
def warn(m): unreal.log_warning('[itemdb-dt-one] ' + m)
def err(m):  unreal.log_error('[itemdb-dt-one] ' + m)


def _gen():
    """Единственный переимпортёр item-БД `gen_itemdb_datatables` (★ дедуп №2 В2 Т9); импорт при вызове."""
    import sys
    here = os.path.dirname(os.path.abspath(__file__))
    if here not in sys.path:
        sys.path.append(here)
    import gen_itemdb_datatables
    return gen_itemdb_datatables


def main():
    """★ дедуп №2 В2 Т9: тонкая обёртка — тело импорта (гейт слоя имён, `_data_hex`→`DataHex`, снимок ASDA_DIFF_COLS,
    маркер ASDA_WRITES_ASSET, сохранение одного ассета) — общее `gen_itemdb_datatables.import_table`.
    ★ ИСПРАВЛЕНИЕ: без ASDA_TABLES — ОТКАЗ (раньше молча импортировалась `premiumleveltable`); каталог ItemDB больше не
    пересохраняется целиком (`save_directory`) — сохраняется только импортированный ассет."""
    G = _gen()
    manifest = G.load_manifest()
    tables = G.selected_tables(manifest)
    if not tables:
        err('ОТКАЗ: не задан ASDA_TABLES (имена таблиц через запятую) — что импортировать, неизвестно')
        return
    ok, total = G.run(tables, manifest, DIFF_COLS, log=log, warn=warn, err=err)
    log('ИТОГО: импортировано %d / %d' % (ok, total))


if __name__ == '__main__':
    main()
