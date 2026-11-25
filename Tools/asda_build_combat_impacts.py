# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_combat_impacts.py — BP-заготовки ИМПАКТОВ обычной атаки (эффекты попадания).
# ЭТАЛОН:
#   - Моб бьёт игрока: Combat_ApplyDamage@0xF7C470 → CEffectMgr_CreateEffect("eff_0_mon_99_battk01-a_00.jeb",
#     1, позиция жертвы, 0, scale 1.0) на КАДРЕ удара замаха (клип 5+вариант).
#   - Наш удар по мобу: SkillEffect_GetBasicAttackJebByClassWeapon@0xA52720 → "eff_0_char_99_battk01/02/03/04"
#     по (weaponType, hitIndex); спавн на позиции цели.
# Пайплайн — копия asda_build_click_marker.py (парсер JEB + build_emitter эффектов карт).
# ★ Аудит №4 (волна 3): форма сборки BP переехала в `asda_fx_common` (общая с шестью другими
#   канальными сборщиками); здесь остались ТОЛЬКО отличия канала импактов.
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
    ("eff_0_mon_99_battk01-a_00",  "BP_ImpactMob"),      # моб → игрок
    ("eff_0_char_99_battk01-a_00", "BP_ImpactBattk01"),  # наш удар (дефолт)
    ("eff_0_char_99_battk02-a_00", "BP_ImpactBattk02"),
    ("eff_0_char_99_battk03-a_00", "BP_ImpactBattk03"),
    ("eff_0_char_99_battk04-a_00", "BP_ImpactBattk04"),
    # ★ 2026-10-29 (combat3) [ОРИГИНАЛ] КРИТ моба по игроку: `Combat_ApplyDamage@0xF7C470` (combat_pvp.c:7883) спавнит
    #   `eff_0_mon_99_cattk01-a_00.jeb` в позиции жертвы вместо battk01 (+ `S_c_cri1/2.wav` по полу). Сборка точечно:
    #   `ASDA_IMPACT_ONLY="BP_ImpactMobCrit"` — принятые импакты не пересобираются.
    ("eff_0_mon_99_cattk01-a_00",  "BP_ImpactMobCrit"),
]


def log(m): unreal.log_warning("[impactfx] %s" % m)


def build_one(J, PE, eff_name, out_name):
    jb = FXC.read_jeb(J, SRC_DIR, eff_name, log)
    if jb is None: return False
    emitters = jb.get("emitters", [])
    blend_add = FXC.blend_mode(jb)
    full = "%s/%s" % (OUT_PKG, out_name)
    bp, cdo = FXC.ensure_effect_bp(out_name, OUT_PKG, log)
    if bp is None: return False
    cdo.set_editor_property("effect_name", eff_name)
    ems = []
    for em in emitters:
        try:
            fx = PE.build_emitter(em, blend_add)
            # ★ импакт = ONE-SHOT (эталон CEffectMgr_CreateEffect, holder duration 0.35/0.5, endMode=0):
            #   build_emitter для growQuad жёстко ставит end_mode=1 (restart-пульс, закон ВЕЧНЫХ карт) →
            #   вспышка перезапускалась ~6 раз за LifeSpan 1.2с. Возвращаем endMode из JEB (battk: 0 = стоп,
            #   квад гаснет после life 0.2-0.3с — одна вспышка).
            # ⚠ Набор типов здесь У́ЖЕ общего `FXC.QUAD_TYPES` — без 0x18: ровно так собраны
            #   ПРИНЯТЫЕ ассеты импактов. Расширение набора = пересборка канала, не правка формы.
            if em.get("type") in (0xB, 0xC) and em.get("params", {}).get("growQuad"):
                fx.set_editor_property("end_mode", int(em["params"].get("endMode", 0)))
            ems.append(fx)
        except Exception as e:
            log("  %s: эмиттер %s пропущен: %s" % (eff_name, em.get("name", "?"), e))
    # ★ ЧЕСТНО ПРО СОХРАНЕНИЕ (2026-08-21, см. шапку `asda_fx_common.finish_effect_bp`):
    #   занятый другим процессом `.uasset` НЕ сохраняется, а строка «создан…» печаталась всё равно.
    if not FXC.finish_effect_bp(bp, cdo, ems):
        log("НЕ СОХРАНЁН %s: файл занят другим процессом (закрой редактор UE и повтори)" % full)
        return False
    log("создан %s (%s): эмиттеров %d, бленд=%s" % (full, eff_name, len(ems), blend_add))
    return True


def main():
    mods = FXC.import_fx_modules(log)
    if mods is None: return
    J, PE = mods
    FXC.point_pe_to(PE, OUT_PKG)
    ok = 0
    # ★ 2026-10-29: ASDA_IMPACT_ONLY="BP_Имя1;BP_Имя2" — собрать только перечисленные (закон «подтверждённые эффекты не трогать»).
    only = set(x for x in os.environ.get("ASDA_IMPACT_ONLY", "").split(";") if x)
    for eff, name in EFFECTS:
        if only and name not in only: continue
        ok += 1 if build_one(J, PE, eff, name) else 0
    log("ГОТОВО: %d/%d" % (ok, len(only) if only else len(EFFECTS)))


# ★★★ Закон tools-no-import-side-effects. Здесь стоял ГОЛЫЙ `main()` — любой `import
#   asda_build_combat_impacts` (в т.ч. из пробника или из другого билд-скрипта) молча перезаливал бы
#   BP импактов. Разбор признака прямого запуска — в шапке `asda_fx_common.is_direct_run`
#   (аудит №4, волна 3: тело жило 14 дословными копиями).
if FXC.is_direct_run(globals()):
    main()
