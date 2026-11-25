# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_loot_fx.py — BP-заготовка эффекта ПОЯВЛЕНИЯ наземного лута.
# ЭТАЛОН (loot_system_re.md §3): DropItem_InitVisual@0xF30040 → CEffectMgr_CreateEffect
#   "eff_0_item_99_drop01-a_00.jeb" в точке дропа. Пайплайн — копия asda_build_combat_impacts.py.
# ★ Аудит №4 (волна 3): форма сборки BP переехала в `asda_fx_common` (общая с шестью другими
#   канальными сборщиками); здесь остались ТОЛЬКО отличия канала лута.
import sys, os
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_fx_common as FXC
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

SRC_DIR = _P.EFFECT_MODEL_ORIG_DIR
OUT_PKG = _U.G_EFFECTS
EFFECTS = [
    ("eff_0_item_99_drop01-a_00", "BP_LootDropFx"),   # появление предмета на земле
]


def log(m): unreal.log_warning("[lootfx] %s" % m)


def build_one(J, PE, eff_name, out_name):
    jb = FXC.read_jeb(J, SRC_DIR, eff_name, log)
    if jb is None: return False
    emitters = jb.get("emitters", [])
    blend_add = FXC.blend_mode(jb)
    bp, cdo = FXC.ensure_effect_bp(out_name, OUT_PKG, log)
    if bp is None: return False
    cdo.set_editor_property("effect_name", eff_name)
    ems = []
    for em in emitters:
        try:
            fx = PE.build_emitter(em, blend_add)
            # ★ ONE-SHOT (как фикс импактов): для growQuad build_emitter жёстко ставит end_mode=1
            #   (restart-пульс, закон ВЕЧНЫХ карт) — возвращаем endMode из JEB (0 = одна вспышка).
            # ⚠ Набор типов здесь У́ЖЕ общего `FXC.QUAD_TYPES` — без 0x18: ровно так собран
            #   ПРИНЯТЫЙ ассет `BP_LootDropFx`. Расширение набора = пересборка канала, не правка формы.
            if em.get("type") in (0xB, 0xC) and em.get("params", {}).get("growQuad"):
                fx.set_editor_property("end_mode", int(em["params"].get("endMode", 0)))
            ems.append(fx)
        except Exception as ex:
            log("  %s: эмиттер %s пропущен: %s" % (eff_name, em.get("name", "?"), ex))
    # ★ ЧЕСТНО ПРО СОХРАНЕНИЕ (2026-08-21, см. шапку `asda_fx_common.finish_effect_bp`):
    #   занятый другим процессом `.uasset` НЕ сохраняется, а строка «создан…» печаталась всё равно.
    if not FXC.finish_effect_bp(bp, cdo, ems):
        log("НЕ СОХРАНЁН %s/%s: файл занят другим процессом (закрой редактор UE)" % (OUT_PKG, out_name))
        return False
    log("создан %s/%s (%s): эмиттеров %d, бленд=%s" % (OUT_PKG, out_name, eff_name, len(ems), blend_add))
    return True


def main():
    mods = FXC.import_fx_modules(log)
    if mods is None: return
    J, PE = mods
    FXC.point_pe_to(PE, OUT_PKG)
    ok = 0
    for eff, name in EFFECTS:
        ok += 1 if build_one(J, PE, eff, name) else 0
    log("ГОТОВО: %d/%d" % (ok, len(EFFECTS)))


# ★★★ ЗАКОН [[tools-no-import-side-effects]]. ЗДЕСЬ СТОЯЛ ГОЛЫЙ `main()` (аудит №4, волна 3):
#   любой `import asda_build_loot_fx` молча пересобирал и перезаливал BP_LootDropFx.
#   Замер сухим прогоном подтвердил дефект: на ОДНОМ импорте создавался 1 BP на 14 эмиттеров.
if FXC.is_direct_run(globals()):
    main()
