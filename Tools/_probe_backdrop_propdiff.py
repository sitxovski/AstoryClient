# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_backdrop_propdiff.py — ЗАМЕР (только чтение): ПОЛНЫЙ дифф редакторских свойств элемента задника логина
#   против CDO (компонент, актёр) и свойств корней иерархии LoginBackdrop_Anchor/Flight. Уровень только грузится,
#   НИЧЕГО не сохраняется. Нужен, т.к. в -game даже Cube+DefaultMaterial на компоненте bg_102 тени не дал.
import os, json

OUT = os.environ.get("ASDA_PROBE_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/backdrop_shadows/probe_backdrop_propdiff.json")


def main():
    import unreal
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    def log(m):
        unreal.log_warning("[probe-bddiff] %s" % m)

    def allprops(o):
        d = {}
        for n in dir(o):
            if n.startswith("_"):
                continue
            try:
                v = o.get_editor_property(n)
            except Exception:
                continue
            if callable(v) and not isinstance(v, unreal.Object):
                continue
            try:
                d[n] = v.get_path_name() if isinstance(v, unreal.Object) else str(v)
            except Exception:
                d[n] = "?"
        return d

    def diff(a, b):
        A, B = allprops(a), allprops(b)
        return {k: [A.get(k), B.get(k)] for k in sorted(set(A) | set(B)) if A.get(k) != B.get(k)}

    assert les.load_level("/Game/Map/Login")
    R = {}
    acts = {a.get_actor_label(): a for a in eas.get_all_level_actors()}
    el = acts["BackdropElem_bg_102_e00_00"]
    smc_cdo = unreal.get_default_object(unreal.StaticMeshComponent)
    sma_cdo = unreal.get_default_object(unreal.StaticMeshActor)
    R["elem_comp_vs_cdo"] = diff(el.static_mesh_component, smc_cdo)
    R["elem_actor_vs_cdo"] = diff(el, sma_cdo)
    sc_cdo = unreal.get_default_object(unreal.SceneComponent)
    for lbl in ("LoginBackdrop_Anchor", "LoginBackdrop_Flight"):
        a = acts.get(lbl)
        if a:
            R[lbl + "_class"] = a.get_class().get_name()
            R[lbl + "_root_class"] = a.root_component.get_class().get_name()
            R[lbl + "_root_vs_scenecdo"] = diff(a.root_component, sc_cdo)
            R[lbl + "_actor_hidden"] = str(a.get_editor_property("hidden"))
    cas = next((a for l, a in acts.items() if l.startswith("LoginCasket")), None)
    if cas:
        sk = cas.get_components_by_class(unreal.SkeletalMeshComponent)[0]
        R["casket_comp_vs_skcdo"] = diff(sk, unreal.get_default_object(unreal.SkeletalMeshComponent))
        R["casket_parent"] = cas.get_attach_parent_actor().get_actor_label() if cas.get_attach_parent_actor() else None
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(R, f, ensure_ascii=False, indent=1)
    log("ЗАВЕРШЕНО")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
