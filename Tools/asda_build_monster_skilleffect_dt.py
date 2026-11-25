# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_monster_skilleffect_dt.py — DT_MonsterSkillEffect ← Data/Mob/Monster_SkillEffect.BIN
#   (магия "MSE", запись 0x2D8 = 728 б, 91 запись).
# Загрузчик оригинала CONFIRMED: Mob_LoadSkillEffects @0xF76770; раскладка полей выведена из
#   skillCloneEntryByIndex @0xF76900 (5 слотов: .jeb + кость + тип + параметр), дефолты — sub_F76BA0.
# Раскладка/парсер: Tools/asda_effect_mob_tables.py. RowStruct: FAsdaMonsterSkillEffectRow
#   (Source/AsdaReborn/Public/Data/AsdaEffectMobTableRows.h) — ТРЕБУЕТ сборки C++.
# Выход: Tools/_out/MonsterSkillEffect.csv (ПРОМЕЖУТОЧНЫЙ, вне Content!) + /Game/AsdaAssets/Data/DT_MonsterSkillEffect
# Запуск: Tools/run_headless.bat Tools\asda_build_monster_skilleffect_dt.py
# ★ ПРАВИЛО tools-no-import-side-effects: НИКАКИХ действий при импорте модуля — только через main().
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_effect_mob_tables as T  # noqa: E402
from asda_dt_common import build_table  # noqa: E402


def main():
    build_table(unreal, T.TABLES["MonsterSkillEffect"], tag="mob-skillfx")


if __name__ == "__main__":
    main()
