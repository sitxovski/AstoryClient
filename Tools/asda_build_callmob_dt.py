# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_callmob_dt.py — DT_CallMob ← Data/Mob/CallMobTable.BIN (магия "CMT", запись 32 б).
# Загрузчик оригинала CONFIRMED: mobLoadCallMobTable @0xF76D10; поиск записи —
#   objListFindByTwoKeys @0xF77000 (ключи: dword[1]=NpcId и dword[5]=MapId).
# Спавн призванного моба серверный — таблица нужна клиенту как каталог.
# Раскладка/парсер: Tools/asda_effect_mob_tables.py. RowStruct: FAsdaCallMobRow
#   (Source/AsdaReborn/Public/Data/AsdaEffectMobTableRows.h) — ТРЕБУЕТ сборки C++.
# Выход: Tools/_out/CallMob.csv (ПРОМЕЖУТОЧНЫЙ, вне Content!) + /Game/AsdaAssets/Data/DT_CallMob
# Запуск: Tools/run_headless.bat Tools\asda_build_callmob_dt.py
# ★ ПРАВИЛО tools-no-import-side-effects: НИКАКИХ действий при импорте модуля — только через main().
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_effect_mob_tables as T  # noqa: E402
from asda_dt_common import build_table  # noqa: E402


def main():
    build_table(unreal, T.TABLES["CallMob"], tag="callmob")


if __name__ == "__main__":
    main()
