# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
_astory_titles_final.py — приведение всех званий в финальный вид (headless).

  1. `Astory.Titles.ApplyPatch`         — применить итоговый патч (тексты + числовые решения
                                          по конфликтам) через модель редактора;
  2. `Astory.Titles.ServerFinalExport`  — выгрузить ДВА варианта серверной таблицы:
                                          256 записей (класть сейчас) и 280 (после патчей);
  3. `Astory.Titles.SelfCheck`          — доказать, что после записи всё сходится побайтово.

Патч готовит `asda_title_finalize.py`. Живой сервер только читается.
Побочных эффектов при импорте нет.
"""
import unreal


def main():
    for cmd in ('Astory.Titles.ApplyPatch',
                'Astory.Titles.ServerFinalExport',
                'Astory.Titles.SelfCheck'):
        unreal.log('[astory-final] >>> ' + cmd)
        unreal.SystemLibrary.execute_console_command(None, cmd)
    unreal.log('[astory-final] готово')


if __name__ == '__main__':
    main()
