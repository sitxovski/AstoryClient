# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_login_actors_full.py — полный дамп акторов Login (метка, класс, трансформ, меш).
#   Ничего не меняет. Нужен, чтобы перевод сцены в ×1 не пропустил ни одного актора.
import os, io, json
import unreal

OUT = os.environ.get("ASDA_PROBE_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/login_scale/login_actors.json")
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def v3(v):
    return [round(float(v.x), 3), round(float(v.y), 3), round(float(v.z), 3)]


def main():
    assert les.load_level("/Game/Map/Login")
    rows = []
    for a in eas.get_all_level_actors():
        lbl = a.get_actor_label()
        if lbl.startswith("BackdropElem_"):
            continue                      # 697 штук — их пересобирает asda_build_backdrop_elements
        r = {"label": lbl, "class": a.get_class().get_name(),
             "loc": v3(a.get_actor_location()), "scale": v3(a.get_actor_scale3d()),
             "rot": [round(float(a.get_actor_rotation().pitch), 3),
                     round(float(a.get_actor_rotation().yaw), 3),
                     round(float(a.get_actor_rotation().roll), 3)]}
        par = a.get_attach_parent_actor()
        r["parent"] = par.get_actor_label() if par else None
        rc = a.root_component
        if rc:
            try:
                r["rel_loc"] = v3(rc.get_editor_property("relative_location"))
                r["rel_scale"] = v3(rc.get_editor_property("relative_scale3d"))
            except Exception as e:
                r["rel_err"] = str(e)
        for comp in a.get_components_by_class(unreal.MeshComponent):
            if isinstance(comp, unreal.StaticMeshComponent) and comp.static_mesh:
                r["mesh"] = comp.static_mesh.get_path_name()
            elif isinstance(comp, unreal.SkeletalMeshComponent):
                sk = comp.get_editor_property("skeletal_mesh_asset")
                if sk:
                    r["mesh"] = sk.get_path_name()
            try:
                b = a.get_actor_bounds(False)
                r["bbox"] = [round(2 * b[1].x, 1), round(2 * b[1].y, 1), round(2 * b[1].z, 1)]
            except Exception:
                pass
            break
        rows.append(r)
    # ещё: сколько элементов задника и их общий bbox
    n_elem = sum(1 for a in eas.get_all_level_actors() if a.get_actor_label().startswith("BackdropElem_"))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with io.open(OUT, "w", encoding="utf-8") as f:
        f.write(json.dumps({"n_backdrop_elems": n_elem, "actors": rows}, ensure_ascii=False, indent=1))
    unreal.log_warning("[probe-actors] записано %s (прочих акторов %d, элементов задника %d)"
                       % (OUT, len(rows), n_elem))
    unreal.log_warning("[probe-actors] PROBE-ACTORS-FINISHED")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
