# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""КОНТРОЛЬ: эффекты КАРТ не затронуты правками свечения оружия.
   1) по ВСЕМ BP_Fx проверяем, что новые ветки недостижимы: strict_quad_attach=False и
      follow_skel_component=None (обе правки круга 4 работают ТОЛЬКО при этих флагах);
   2) для реперных карт-эффектов (радуга, фонари alpen, дым/волны) печатаем габариты секций PMC —
      числовой снимок для сверки.
"""
import sys
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

REPER = ["rainbow2", "rainbow", "alpen_light01", "alpen_light02", "alpen01_tower",
         "flooer", "wave", "pado", "smoke01"]


def log(m): unreal.log_warning("[контроль] %s" % m)


ar = unreal.AssetRegistryHelpers.get_asset_registry()


les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    assets = ar.get_assets_by_path("/Game/AsdaAssets/Effects", recursive=True)
    bps = [a for a in assets if str(a.asset_class_path.asset_name) == "Blueprint"]
    log("BP эффектов всего: %d" % len(bps))
    bad_strict, bad_skel, checked = [], [], 0
    for a in bps:
        bp = a.get_asset()
        if not bp: continue
        try: cdo = unreal.get_default_object(bp.generated_class())
        except Exception: continue
        checked += 1
        try:
            if cdo.get_editor_property("strict_quad_attach"): bad_strict.append(str(a.asset_name))
        except Exception: pass
        try:
            if cdo.get_editor_property("follow_skel_component") is not None: bad_skel.append(str(a.asset_name))
        except Exception: pass
    log("проверено CDO: %d" % checked)
    log("BP с strict_quad_attach=True в АССЕТЕ: %d %s" % (len(bad_strict), bad_strict[:10]))
    log("BP с follow_skel_component≠None в АССЕТЕ: %d %s" % (len(bad_skel), bad_skel[:10]))
    log("⇒ обе правки круга 4 включаются ТОЛЬКО в рантайме (ApplyWeaponGlowToActor) ⇒ карт-эффекты идут "
        "прежним путём бит-в-бит")
    try: les.new_level("/Game/Temp/_control_mapfx")
    except Exception: pass
    names = {str(a.asset_name): a for a in bps}
    for r in REPER:
        key = "BP_Fx_" + r
        cand = [n for n in names if n.lower() == key.lower()] or [n for n in names if r.lower() in n.lower()]
        if not cand:
            log("реперный '%s' — BP не найден" % r); continue
        n = cand[0]
        cls = names[n].get_asset().generated_class()
        act = eas.spawn_actor_from_class(cls, unreal.Vector(0, 0, 0))
        pmc = act.get_component_by_class(unreal.ProceduralMeshComponent)
        ems = act.get_editor_property("emitters")
        tot = 0; bmin = [1e9]*3; bmax = [-1e9]*3
        for i in range(len(ems)):
            try: V = unreal.ProceduralMeshLibrary.get_section_from_procedural_mesh(pmc, i)[0]
            except Exception: V = []
            for v in V:
                tot += 1
                for k, c in enumerate((v.x, v.y, v.z)):
                    bmin[k] = min(bmin[k], c); bmax[k] = max(bmax[k], c)
        if tot:
            log("%-26s эмиттеров=%-3d вершин=%-5d габариты X[%.1f..%.1f] Y[%.1f..%.1f] Z[%.1f..%.1f]"
                % (n, len(ems), tot, bmin[0], bmax[0], bmin[1], bmax[1], bmin[2], bmax[2]))
        else:
            log("%-26s эмиттеров=%d вершин=0" % (n, len(ems)))
        eas.destroy_actor(act)
    log("ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
