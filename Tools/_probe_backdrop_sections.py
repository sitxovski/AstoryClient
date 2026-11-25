# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_backdrop_sections.py — ЗАМЕР (только чтение, НИЧЕГО не сохраняет, уровень НЕ грузит):
#   флаг «секция отбрасывает тень» у мешей задника логина SM_m* против контрольных статмешей проекта.
#   Причина разбора: StaticMeshSceneProxy.cpp:427 — bCastShadow = bCastShadow && bAnySectionCastsShadows.
import os, json

OUT = os.environ.get("ASDA_PROBE_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/backdrop_shadows/probe_backdrop_sections.json")
MESHES = ["/Game/AsdaAssets/Models/Backdrop/SM_m102", "/Game/AsdaAssets/Models/Backdrop/SM_m000",
          "/Game/AsdaAssets/Models/Backdrop/SM_m001", "/Game/AsdaAssets/Models/Backdrop/SM_m631"]


def main():
    import unreal
    smes = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    EAL = unreal.EditorAssetLibrary

    def log(m):
        unreal.log_warning("[probe-bdsec] %s" % m)

    # Контроль: несколько статмешей НЕ из задника (любые, что есть в проекте).
    ctrl = []
    try:
        ar = unreal.AssetRegistryHelpers.get_asset_registry()
        flt = unreal.ARFilter(class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "StaticMesh")],
                              package_paths=["/Game/AsdaAssets/Models"], recursive_paths=True)
        for ad in ar.get_assets(flt):
            p = str(ad.package_name)
            if "/Backdrop/" not in p:
                ctrl.append(p)
            if len(ctrl) >= 4:
                break
    except Exception as ex:
        log("контроль: %s" % ex)
    R = {}
    for path in MESHES + ctrl:
        sm = EAL.load_asset(path)
        if not sm:
            R[path] = "нет ассета"; continue
        d = {"backdrop": "/Backdrop/" in path}
        try:
            d["static_materials"] = len(sm.get_editor_property("static_materials"))
        except Exception as ex:
            d["static_materials"] = "err %s" % ex
        for fn in ("get_number_materials", "get_number_verts", "get_lod_count"):
            try:
                f = getattr(smes, fn)
                d[fn] = f(sm) if fn != "get_number_verts" else f(sm, 0)
            except Exception as ex:
                d[fn] = "err %s" % ex
        secs = {}
        for s in range(4):
            try:
                secs[s] = smes.is_section_cast_shadow_enabled(sm, 0, s)
            except Exception as ex:
                secs[s] = "err %s" % ex
        d["section_cast_shadow_lod0"] = secs
        try:
            d["section_collision_lod0_s0"] = smes.is_section_collision_enabled(sm, 0, 0)
        except Exception as ex:
            d["section_collision_lod0_s0"] = "err %s" % ex
        R[path] = d
        log("%s: %s" % (path, json.dumps(d, ensure_ascii=False)))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(R, f, ensure_ascii=False, indent=1)
    log("ЗАВЕРШЕНО")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
