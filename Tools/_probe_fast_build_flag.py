# Copyright (c) 2026 @sitxovski (Discord)
# _probe_fast_build_flag.py — ПРОБНИК: доступен ли из Python флаг UStaticMesh::bDoFastBuild
#   (UPROPERTY() без спецификаторов). Именно он заставляет FStaticMeshRenderData::Cache идти
#   «быстрым» путём, который НЕ заполняет FStaticMeshRenderData::Bounds → в кук уходит мусор/NaN.
#   Ничего не сохраняет.
import unreal

def log(m): unreal.log("[probe_fb] " + str(m))

PKGS = ["/Game/Map/rein/Models/SM_tent",
        "/Game/Map/rein/Models/SM_barricade"]

def main():
    for pkg in PKGS:
        sm = unreal.load_asset(pkg)
        if not isinstance(sm, unreal.StaticMesh):
            log("%s: не StaticMesh" % pkg); continue
        for name in ("do_fast_build", "b_do_fast_build", "bDoFastBuild"):
            try:
                v = sm.get_editor_property(name)
                log("%s: get %s = %r" % (pkg, name, v))
            except Exception as e:
                log("%s: get %s EXC %s" % (pkg, name, e))
        try:
            sm.set_editor_property("do_fast_build", False)
            log("%s: set do_fast_build=False OK -> %r" % (pkg, sm.get_editor_property("do_fast_build")))
        except Exception as e:
            log("%s: set do_fast_build EXC %s" % (pkg, e))
        # чем ещё располагаем
        try:
            log("%s: materials=%s lods=%d nanite=%r" % (
                pkg, [str(m.material_interface.get_name()) if m.material_interface else None
                      for m in sm.get_editor_property("static_materials")],
                sm.get_num_lods(), sm.get_editor_property("nanite_settings").enabled))
        except Exception as e:
            log("%s: инфо EXC %s" % (pkg, e))
    log("--- методы StaticMesh со словом build/bounds ---")
    log(sorted([x for x in dir(unreal.StaticMesh) if "build" in x or "bound" in x]))

if __name__ == "__main__":
    main()
