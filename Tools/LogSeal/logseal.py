# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""logseal.py — точка входа инструмента запечатанных журналов в AstoryClient/Tools/LogSeal (★ 19.10.2026).

Тонкая обёртка: вся логика (формат AsdaLogSeal, ключи DPAPI, расшифровка) живёт в ОДНОМ месте —
AstoryClient/Tools/asda_logseal.py. Его оттуда не переносим: от его расположения зависят пути открытого ключа
(Tools/data/logseal), заголовок клиента (Source/AsdaNet/.../AsdaLogSealKey.generated.h) и импорты сборки релиза
(gen_build_crypto.py, asda_update_format.py, eac_package.py → gen_build_crypto). Здесь — только загрузка модуля по пути.

ЗАПУСК
  python logseal.py decrypt <файл|папка> [-o папка]   — .alog/.acrash → <имя>.txt (+ <имя>.d\\ для пакета падения)
  python logseal.py check | selftest | import-backup <файл> | …  — любые команды asda_logseal.py (см. его шапку)
  Decrypt.bat — то же для перетаскивания файлов/папок; FetchAndDecrypt.bat — забрать с сервера и расшифровать.

Закрытый ключ — только в хранилище DPAPI владельца (%APPDATA%\\AsdaReborn\\LogSealKeys); сюда не копируется.
Побочных эффектов при импорте нет (закон tools-no-import-side-effects).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ASDA_TOOLS = os.path.dirname(HERE)                                          # <проект>\Tools — где живёт asda_logseal.py (единственная копия)


def load():
    """Модуль asda_logseal из AstoryClient/Tools (тот же объект, что импортирует сборка релиза)."""
    if ASDA_TOOLS not in sys.path:
        sys.path.insert(0, ASDA_TOOLS)
    import asda_logseal
    return asda_logseal


def main(argv=None):
    """Передать команду asda_logseal.main как есть."""
    return load().main(argv)


if __name__ == "__main__":
    sys.exit(main())
