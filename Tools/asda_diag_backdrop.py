# Copyright (c) 2026 @sitxovski (Discord)
# Диагностика декомпозиции: позиции акторов (relative/world), геометрия мешей (tris/bounds),
# локальные bounds старого скелетного backdrop (сверка масштаба glTF-импорта vs raw-jbm).
import unreal
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level("/Game/Map/Login")
    elems = [a for a in eas.get_all_level_actors() if a.get_actor_label().startswith("BackdropElem")]
    anchor = next((a for a in eas.get_all_level_actors() if a.get_actor_label()=="LoginBackdrop_Anchor"), None)
    print("BackdropElem акторов:", len(elems))
    if anchor:
        print("ЯКОРЬ world loc=%s scale=%s"%(anchor.get_actor_location(), anchor.get_actor_scale3d()))
    for a in elems[:6]:
        smc = a.get_component_by_class(unreal.StaticMeshComponent)
        sm = smc.get_editor_property("static_mesh") if smc else None
        relloc = smc.get_editor_property("relative_location") if smc else None
        tris = -1; ext = "-"
        if sm:
            try: tris = sm.get_num_triangles(0)
            except Exception as e: tris = "err:%s"%e
            try:
                bb = sm.get_bounds(); ext = "extent=%s origin=%s"%(bb.box_extent, bb.origin)
            except Exception as e: ext = "bounds err:%s"%e
        wl = a.get_actor_location()
        print("  %-28s worldXYZ=(%.0f,%.0f,%.0f) relXYZ=(%.0f,%.0f,%.0f) mesh=%s tris=%s %s"%(
            a.get_actor_label(), wl.x, wl.y, wl.z,
            (relloc.x if relloc else 0),(relloc.y if relloc else 0),(relloc.z if relloc else 0),
            sm.get_name() if sm else "НЕТ", tris, ext))
    # старый скелетный: локальные bounds меша (масштаб)
    old = next((a for a in eas.get_all_level_actors() if a.get_actor_label()=="LoginBackdrop_OLD"), None)
    if old:
        c = old.get_component_by_class(unreal.SkeletalMeshComponent)
        m = c.get_skeletal_mesh_asset() if c else None
        if m:
            try: bb = m.get_bounds().box_extent; print("СТАРЫЙ SKEL mesh=%s extent(half)=%s"%(m.get_name(), bb))
            except Exception as e: print("СТАРЫЙ SKEL bounds err:%s"%e)
    print("=== DONE ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
