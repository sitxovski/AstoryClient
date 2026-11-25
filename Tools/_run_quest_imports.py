# Copyright (c) 2026 @sitxovski (Discord)
# Разовый прогон импортёра таблиц журнала заданий (QDT / QCT / BOOK) в headless-редакторе.
#   Таблицы NPC (в т.ч. расширенная строка Episode_Table) заливает asda_import_npc_tables.py —
#   его прогоняем этим же скриптом, чтобы порядок был воспроизводим.
import os as _os
import sys as _sys

_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))


def main():
    import asda_import_npc_tables as A
    import asda_import_quest_tables as B
    A.main(csv_only=False)
    B.main(csv_only=False)


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
