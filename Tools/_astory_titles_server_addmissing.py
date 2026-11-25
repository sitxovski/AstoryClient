# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
_astory_titles_server_addmissing.py — достройка серверной FameInfo.txt недостающими записями
и выгрузка результата в обе серверные ветки.

Команда `Astory.Titles.ServerAddMissing`:
  1. пересобирает файл БЕЗ достройки и требует побайтового совпадения с живым;
  2. достраивает недостающие записи (клиентские id, которых у сервера нет);
  3. доказывает, что существующие строки не тронуты (правится только счётчик в первой строке);
  4. сверяет счётчик с фактическим числом строк;
  5. отдельно перечисляет добавленные id >= 256 — они заработают лишь после расширения маски;
  6. раскладывает выгрузку зеркалом серверных путей и пишет README.txt.

Живой сервер только ЧИТАЕТСЯ. Побочных эффектов при импорте нет.

Запуск:  Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\_astory_titles_server_addmissing.py
"""
import unreal


def main():
    unreal.log('[astory-addmissing] >>> Astory.Titles.ServerAddMissing')
    unreal.SystemLibrary.execute_console_command(None, 'Astory.Titles.ServerAddMissing')
    unreal.log('[astory-addmissing] готово')


if __name__ == '__main__':
    main()
