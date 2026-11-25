# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
_astory_titles_apply_patch.py — headless-применение патча имён и описаний званий.

Порядок:
  1. `Astory.Titles.ApplyPatch` — применяет `Reports/merged_titles/title_patch.csv`
     через модель редактора: SetStr -> DataHex -> валидация -> резервная копия ->
     CSV/JSON -> переимпорт DT_title_list -> title_list.bin с побайтовой самопроверкой;
  2. `Astory.Titles.SelfCheck`  — доказывает, что после записи всё сходится побайтово.

Патч готовит `asda_title_merge.py`. Побочных эффектов при импорте нет.

Запуск:  Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\_astory_titles_apply_patch.py
"""
import unreal


def main():
    for cmd in ('Astory.Titles.ApplyPatch',
                'Astory.Titles.SelfCheck'):
        unreal.log('[astory-patch] >>> ' + cmd)
        unreal.SystemLibrary.execute_console_command(None, cmd)
    # ★ 27.09.2026 (itemsdatafix, КРИТ-1): ApplyPatch пишет title_list.csv напрямую, а декод Bin.bbz такие
    #   правки стирает (27.09 откатились 463 ячейки). Переносим результат в ведомость `_value_overlay/title_list.csv`.
    import os, sys
    sys.path.insert(0, r'@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\ItemDB')
    import build_itemdb_poststep_ledgers as _L
    rc = _L.main(['--table', 'title_list', '--write', '--replace'])
    if rc:
        unreal.log_error('[astory-patch] ведомость title_list НЕ обновлена — разобрать до любого декода')
    unreal.log('[astory-patch] готово')


if __name__ == '__main__':
    main()
