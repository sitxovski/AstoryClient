# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""КОНТРОЛЬ ОХВАТА флага `strict_quad_pivot` (волна fx_spin, 10.09.2026).

   1) BP-эффекты (`/Game/AsdaAssets/Effects`) — свечение оружия, крылья, соулмейт, навыки, гача:
      флага в АССЕТЕ быть НЕ ДОЛЖНО (его ставит только печка карт).
   2) Уровни карт — сколько квад-эмиттеров получили флаг и сколько квадов осталось без него.
   Ничего не меняет: только читает (уровни загружаются, но не сохраняются).
"""
import os, sys
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

MAPS = os.environ.get("ASDA_PIVOT_MAPS",
                      "alpen,castle_of_chaos,castle_of_chess,palace_of_queen,t_castle_of_chess,"
                      "t_palace_of_queen,quizstage,conquester,firewayforest,flabis,inferion,rein,"
                      "StagnantDesert,silalis").split(",")
QUAD = (1, 2, 0xB, 0xC, 0x13, 0x14, 0x18, 0x19)


def log(m): unreal.log_warning("[пивот] %s" % m)


def main():
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    assets = ar.get_assets_by_path("/Game/AsdaAssets/Effects", recursive=True)
    bps = [a for a in assets if str(a.asset_class_path.asset_name) == "Blueprint"]
    bad, checked, ems_total = [], 0, 0
    for a in bps:
        bp = a.get_asset()
        if not bp: continue
        try: cdo = unreal.get_default_object(bp.generated_class())
        except Exception: continue
        checked += 1
        try: ems = cdo.get_editor_property("emitters")
        except Exception: continue
        for e in ems:
            ems_total += 1
            try:
                if e.get_editor_property("strict_quad_pivot"): bad.append(str(a.asset_name))
            except Exception: pass
    log("BP эффектов: %d, проверено CDO: %d, эмиттеров в них: %d" % (len(bps), checked, ems_total))
    log("BP с strict_quad_pivot=True в АССЕТЕ: %d %s" % (len(bad), sorted(set(bad))[:10]))

    for m in MAPS:
        m = m.strip()
        if not m: continue
        lvl = "/Game/Map/%s/%s" % (m.replace(" ", "_"), m.replace(" ", "_"))
        if not unreal.EditorAssetLibrary.does_asset_exist(lvl):
            log("%-20s уровня нет" % m); continue
        les.load_level(lvl)
        q_on = q_off = other_on = 0
        for a in eas.get_all_level_actors():
            if not a.get_actor_label().startswith("MapEffect"): continue
            try: ems = a.get_editor_property("emitters")
            except Exception: continue
            for e in ems:
                t = e.get_editor_property("jeb_type")
                on = bool(e.get_editor_property("strict_quad_pivot"))
                if t in QUAD and not e.get_editor_property("mesh_emitter"):
                    if on: q_on += 1
                    else:  q_off += 1
                elif on:
                    other_on += 1
        log("%-20s квадов с флагом=%4d  квадов БЕЗ флага=%4d  НЕ-квадов с флагом=%d"
            % (m, q_on, q_off, other_on))
    log("КОНТРОЛЬ ПИВОТА ЗАВЕРШЕНО")


if __name__ == "__main__":
    main()
