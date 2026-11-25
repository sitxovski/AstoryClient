# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _verify_ring_no_chiral_scope.py — ОБЛАСТЬ ДЕЙСТВИЯ нового флага `ring_no_chiral` (2026-09-10).
#
#   Закон проекта: правка ОБЩЕГО кода эффектов живёт под флагом с дефолтом «как было».
#   Скрипт доказывает ЧИСЛОМ, что флаг взведён РОВНО там, где мы его ставили, и нигде больше:
#   сканирует ВСЕ Blueprint'ы эффектов (/Game/Effects, /Game/Fx, BP_Fx_*) и ВСЕ уровни карт.
#   Только читает.
import os, unreal

def scan_bp():
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    hits, total = [], 0
    for a in ar.get_assets_by_path("/Game", recursive=True):
        nm = str(a.asset_name)
        if not nm.startswith("BP_"):
            continue
        obj = a.get_asset()
        cdo = None
        try:
            gc = unreal.load_object(None, str(a.package_name) + "." + nm + "_C")
            cdo = unreal.get_default_object(gc) if gc else None
        except Exception:
            cdo = None
        if cdo is None or not isinstance(cdo, unreal.AsdaEffectActor):
            continue
        try:
            ems = cdo.get_editor_property("emitters")
        except Exception:
            continue
        for em in ems:
            if int(em.get_editor_property("jeb_type")) != 0xE:
                continue
            total += 1
            if bool(em.get_editor_property("ring_no_chiral")):
                hits.append("%s / %s" % (nm, em.get_editor_property("name")))
    return total, hits

def main():
    total, hits = scan_bp()
    unreal.log_warning("[область] BP-эффекты: колец 0xE = %d, из них ring_no_chiral=1 — %d" % (total, len(hits)))
    for h in hits:
        unreal.log_warning("[область]   BP ВЗВЕДЁН: %s" % h)
    les = unreal.LevelEditorSubsystem(); eas = unreal.EditorActorSubsystem()
    import sys; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import asda_maps as _M
    maps = os.environ.get("ASDA_MAPS", "alpen,flammio,inferion,firewayforest,castle_of_chaos,"
                                       "castle_of_chess,palace_of_queen,t_castle_of_chess,t_palace_of_queen").split(",")
    for m in maps:
        m = m.strip()
        if not m: continue
        les.load_level("/Game/Map/%s/%s" % (_M.map_asset_name(m), _M.map_asset_name(m)))
        n = k = 0
        names = []
        for a in eas.get_all_level_actors():
            if not a.get_actor_label().startswith("MapEffect"): continue
            if not a.get_class().get_name().startswith("AsdaEffectActor"): continue
            for em in a.get_editor_property("emitters"):
                if int(em.get_editor_property("jeb_type")) != 0xE: continue
                n += 1
                if bool(em.get_editor_property("ring_no_chiral")):
                    k += 1; names.append(a.get_actor_label())
        unreal.log_warning("[область] карта %-18s колец=%-4d ring_no_chiral=1 — %d %s"
                           % (m, n, k, sorted(set(names))))
    unreal.log_warning("[область] ПРОВЕРКА ЗАВЕРШЕНА")

if __name__ == "__main__":
    main()
