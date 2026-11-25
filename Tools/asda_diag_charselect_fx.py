# Copyright (c) 2026 @sitxovski (Discord)
# asda_diag_charselect_fx.py — диагностика геометрии свечения форджинга: фактические bounds шкатулки,
# трансформ эффект-актора, two-sided мастера эффектов. Только чтение, ничего не сохраняет.
import unreal
EAL = unreal.EditorAssetLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    les.load_level("/Game/Map/Login")
    for a in eas.get_all_level_actors():
        cls = a.get_class().get_name()
        lbl = a.get_actor_label()
        if cls == "SkeletalMeshActor":
            comp = a.skeletal_mesh_component
            sm = comp.get_skeletal_mesh_asset() if comp else None
            if sm and "cre_creation01" in sm.get_name().lower():
                o, e = a.get_actor_bounds(False)
                unreal.log("CASKET '%s': loc=%s scale=%s | bounds origin=%s extent=%s (низ Z=%.0f верх Z=%.0f)" % (
                    lbl, a.get_actor_location(), a.get_actor_scale3d(), o, e, o.z - e.z, o.z + e.z))
        if lbl.startswith("CharSelectEffect"):
            unreal.log("EFFECT '%s': loc=%s rot=%s scale=%s hidden=%s" % (
                lbl, a.get_actor_location(), a.get_actor_rotation(), a.get_actor_scale3d(),
                a.get_editor_property("hidden")))
            ems = a.get_editor_property("emitters")
            for i, em in enumerate(ems):
                vs = em.get_editor_property("mesh_verts")
                if vs:
                    zs = [v.z for v in vs]
                    unreal.log("  emitter%d %s: verts=%d Zlocal[%.0f..%.0f] nodes=%d period=%.2f end=%d" % (
                        i, em.get_editor_property("name"), len(vs), min(zs), max(zs),
                        len(em.get_editor_property("nodes")), em.get_editor_property("period"),
                        em.get_editor_property("end_mode")))
    m = EAL.load_asset("/Game/Effects/M_AsdaEffectAdd")
    if m:
        unreal.log("M_AsdaEffectAdd: two_sided=%s blend=%s" % (
            m.get_editor_property("two_sided"), m.get_editor_property("blend_mode")))
    unreal.log("=== DIAG DONE ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
