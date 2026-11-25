# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_effect_list_dt.py — DT_EffectList ← Data/Effect/effect list.bin (магия "EFF").
# ★ ЧЕСТНО: загрузчика этого файла в клиенте НЕТ (см. Reports/effect_list_tables.md) —
#   таблица импортируется как справочник данных оригинала 1:1, рантайм на неё НЕ переводится.
# Раскладка/парсер: Tools/asda_effect_mob_tables.py. RowStruct: FAsdaEffectListRow
#   (Source/AsdaReborn/Public/Data/AsdaEffectMobTableRows.h) — ТРЕБУЕТ сборки C++.
# Выход: Tools/_out/EffectList.csv (ПРОМЕЖУТОЧНЫЙ, вне Content!) + /Game/AsdaAssets/Data/DT_EffectList
# Запуск: Tools/run_headless.bat Tools\asda_build_effect_list_dt.py
# ★ ПРАВИЛО tools-no-import-side-effects: НИКАКИХ действий при импорте модуля — только через main().
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_effect_mob_tables as T  # noqa: E402
from asda_dt_common import build_table  # noqa: E402


def main():
    build_table(unreal, T.TABLES["EffectList"], tag="effect-list")


if __name__ == "__main__":
    main()
