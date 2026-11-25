# Copyright (c) 2026 @sitxovski (Discord)
# fishing_build_fx.py — РЫБАЛКА, волна В5а: BP-заготовки эффектов результата лова BP_Fx_fish_end_01 / _02.
#
# СПИСОК НЕ ВЫДУМАН: это ВСЕ строки .jeb рыбалки в game.c (world_anim.md §2.5, исчерпывающе):
#   fish_end_01.jeb — успех, fish_end_02.jeb — провал. Спавн `Effect_SpawnByName(g_pEffectMgr, имя, 1,
#   x, y+130, z, 0, char->posVec, 0, 0, 0, 1.0)`: свой — `Fishing_OnStateMessage@0xFC56E0` код 0x1D,
#   чужой — `CChar_SetState@0xA13D50` при ВХОДЕ в action 65/66 (game.c:77092). Привязки к кости нет.
#   Файлы — Data/Effect/Model/EffectModel.bnz (байты совпадают с Donors/TRANSLATE_unpacked/models_orig/Data/Effect/Model,
#   откуда читают все канальные сборщики — `asda_build_char_fx.SRC_DIR`).
#
# Сборка каждого BP ПОВТОРНО ИСПОЛЬЗУЕТ `asda_build_char_fx.build_one` (тот же путь, что у навыков/состояний):
#   endMode С ДИСКА (`ONE_SHOT`: у обоих эффектов endMode 0 — разовые) + сид-узлы трека (`KF_SEED_FX`).
#   Расширение множеств-гейтов живёт только в этом процессе ⇒ чужие каналы не меняются.
# Имя ассета = `BP_Fx_<jeb>` в /Game/AsdaAssets/Effects — ровно то, что ищет
#   `AAsdaEffectActor::SpawnByJebName` (AsdaAssetPaths::FxBpName).
#
# Запуск: python Tools/run_headless_safe.py Tools/fishing_build_fx.py  (или из fishing_import_all.py)
#   ASDA_FISHFX_ONLY="fish_end_01" — точечная пересборка.
# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import asda_fx_common as FXC
import unreal

# Эффекты рыбалки (база имени .jeb, нижний регистр)
JEBS = ["fish_end_01", "fish_end_02"]


def log(m):
    """Журнал канала эффектов рыбалки."""
    unreal.log_warning("[fishfx] %s" % m)


def main():
    """Собрать BP_Fx_fish_end_01/02. Возвращает True, если построены все."""
    mods = FXC.import_fx_modules(log, with_char_fx=True)
    if mods is None:
        return False
    J, PE, CFX = mods
    FXC.point_pe_to(PE, CFX.OUT_PKG)
    jebs = FXC.only_filter(JEBS, "ASDA_FISHFX_ONLY")
    FXC.extend_channel_gates(CFX, jebs)
    ok, _skipped, missing = FXC.build_channel(J, PE, CFX, jebs, log)
    log("ГОТОВО: %d/%d построено" % (ok, len(jebs)))
    if missing:
        log("НЕТ НА ДИСКЕ (%d): %s" % (len(missing), ", ".join(missing)))
    return ok == len(jebs)


if FXC.is_direct_run(globals()):
    main()
