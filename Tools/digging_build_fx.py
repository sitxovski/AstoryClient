# Copyright (c) 2026 @sitxovski (Discord)
# digging_build_fx.py — КОПАНИЕ (10.10.2026): BP-заготовки эффектов копания BP_Fx_<jeb>.
#
# СПИСОК НЕ ВЫДУМАН — это ВСЕ строки .jeb копания в эталоне (поиск «shovel»/«c-suc»/«c-fail» по RE/etalon/*.c):
#   eff_0_char_99_c-shovel-a_01 — старт копания: свой (`CCharacter_SetAction` case 40 И 43, entity_char.c:17271/17367),
#                                 сосед 4025 110 (network.c:39960), появление в состоянии 110 (game_main.c:7429);
#   eff_0_char_99_c-shovel-b_01 — старт авто-лопаты у СОСЕДА: 4025 114 (network.c:39718), появление в 114 (game_main.c:7308);
#   eff_0_char_99_c-shovel-a_00 — удар (порог клипа, `CCharacter_UpdateActionState` case 7/10, entity_char.c:20392/20446);
#   eff_0_char_99_c-suc-a_00    — итог 5430 «удачно» (network.c:1568), он же — передача параметров (Recv_ExtractShop);
#   eff_0_char_99_c-fail-a_00   — итог 5430 «не удалась» (network.c:1584), он же — передача параметров.
#   Файлы — Data/Effect/Model/EffectModel.bnz (= Donors/TRANSLATE_unpacked/models_orig/Data/Effect/Model, `asda_build_char_fx.SRC_DIR`).
#
# Сборка каждого BP ПОВТОРНО ИСПОЛЬЗУЕТ `asda_build_char_fx.build_one` (путь навыков/состояний/рыбалки, общая точка
#   `asda_fx_common.build_channel`): endMode С ДИСКА (`ONE_SHOT`) + сид-узлы трека (`KF_SEED_FX`); расширение множеств-гейтов
#   живёт только в этом процессе ⇒ чужие каналы не меняются. Уже существующие BP НЕ пересобираются (skip_existing).
# Имя ассета = `BP_Fx_<jeb>` в /Game/AsdaAssets/Effects — ровно то, что ищет `AAsdaEffectActor::SpawnByJebName`.
#
# Запуск: MSYS_NO_PATHCONV=1 python Tools/run_headless_safe.py Tools/digging_build_fx.py --marker "[digfx] ГОТОВО"
#   ASDA_DIGFX_ONLY="eff_0_char_99_c-suc-a_00" — точечная сборка.
# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import asda_fx_common as FXC
import unreal

# Эффекты копания (база имени .jeb, нижний регистр)
JEBS = ["eff_0_char_99_c-shovel-a_00", "eff_0_char_99_c-shovel-a_01", "eff_0_char_99_c-shovel-b_01",
        "eff_0_char_99_c-suc-a_00", "eff_0_char_99_c-fail-a_00"]


def log(m):
    """Журнал канала эффектов копания."""
    unreal.log_warning("[digfx] %s" % m)


def main():
    """Собрать BP эффектов копания. Возвращает True, если построены (или уже были) все."""
    mods = FXC.import_fx_modules(log, with_char_fx=True)
    if mods is None:
        return False
    J, PE, CFX = mods
    FXC.point_pe_to(PE, CFX.OUT_PKG)
    jebs = FXC.only_filter(JEBS, "ASDA_DIGFX_ONLY")
    FXC.extend_channel_gates(CFX, jebs)
    ok, skipped, missing = FXC.build_channel(J, PE, CFX, jebs, log, skip_existing=True)
    # ★ 10.10.2026 (яма копания, `digging_hole.md`): режим смешивания по эталону — меш 0x1F непрозрачный, ModAdd для src = INVSRCCOLOR,
    #   текстура парной секции меша — из .jem. Точечная правка полей Material/MaterialB трёх BP (`digging_hole_fx.apply`).
    import digging_hole_fx as DH   # отложенный импорт: модуль тянет unreal-путь сборки
    hole_ok = DH.apply()
    log("яма (бленд по эталону): %s" % ("записано" if hole_ok else "ЕСТЬ ПРОПУСКИ"))
    log("ГОТОВО: построено %d, уже были %d, всего %d" % (ok, skipped, len(jebs)))
    if missing:
        log("НЕТ НА ДИСКЕ (%d): %s" % (len(missing), ", ".join(missing)))
    return ok + skipped == len(jebs) and hole_ok


if FXC.is_direct_run(globals()):
    main()
