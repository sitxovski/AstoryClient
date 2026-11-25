# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_backdrop_masktree.py — ЗАМЕР (только чтение, уровень не грузит, НИЧЕГО не сохраняет):
#   дерево выражений, питающих OpacityMask (и WPO) у /Game/Materials/M_AsdaObjectLit; параметры MIC элементов
#   задника против MIC шкатулки (у шкатулки тень есть, у элементов — нет, мастер общий).
import os, json

OUT = os.environ.get("ASDA_PROBE_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/backdrop_shadows/probe_backdrop_masktree.json")
MASTER = "/Game/Materials/M_AsdaObjectLit"
MICS = ["/Game/AsdaAssets/Models/Backdrop/MI_cre_creback-t_00_blend",
        "/Game/AsdaAssets/Models/Backdrop/MI_cre_creback-a_00",
        "/Game/AsdaAssets/Models/Char/cre_creation01-a_00/Materials/cre_creation01-a_00",
        "/Game/AsdaAssets/Models/Char/cre_creation01-a_00/Materials/cre_creation01-a_03_blend"]


def main():
    import unreal
    MEL = unreal.MaterialEditingLibrary
    EAL = unreal.EditorAssetLibrary

    def log(m):
        unreal.log_warning("[probe-bdmask] %s" % m)

    def props(e):
        d = {}
        for n in ("parameter_name", "default_value", "collection", "const_a", "const_b", "r", "constant", "texture",
                  "sampler_type", "channel", "shading_model", "default_value"):
            try:
                v = e.get_editor_property(n)
                d[n] = v if isinstance(v, (bool, int, float, str)) else (v.get_path_name() if isinstance(v, unreal.Object) else str(v))
            except Exception:
                pass
        return d

    def tree(mat, e, depth=0, seen=None):
        seen = seen if seen is not None else set()
        if e is None:
            return None
        node = {"cls": e.get_class().get_name(), "name": e.get_name()}
        node.update(props(e))
        if e.get_name() in seen or depth > 25:
            node["ref"] = True; return node
        seen.add(e.get_name())
        try:
            ins = MEL.get_inputs_for_material_expression(mat, e)
            node["inputs"] = [tree(mat, i, depth + 1, seen) for i in ins]
        except Exception as ex:
            node["inputs_err"] = str(ex)
        return node

    R = {"trees": {}, "mics": {}, "mpc": {}}
    m = EAL.load_asset(MASTER)
    MP = unreal.MaterialProperty
    for pn in ("MP_OPACITY_MASK", "MP_WORLD_POSITION_OFFSET", "MP_BASE_COLOR", "MP_EMISSIVE_COLOR"):
        try:
            R["trees"][pn] = tree(m, MEL.get_material_property_input_node(m, getattr(MP, pn)))
        except Exception as ex:
            R["trees"][pn] = "err %s" % ex
    for p in MICS:
        mi = EAL.load_asset(p)
        if not mi:
            R["mics"][p] = "нет"; continue
        d = {"parent": mi.get_editor_property("parent").get_path_name() if mi.get_editor_property("parent") else None}
        try:
            d["scalars"] = {str(n): MEL.get_material_instance_scalar_parameter_value(mi, n) for n in MEL.get_scalar_parameter_names(mi)}
            d["switches"] = {str(n): MEL.get_material_instance_static_switch_parameter_value(mi, n) for n in MEL.get_static_switch_parameter_names(mi)}
            d["textures"] = {str(n): (MEL.get_material_instance_texture_parameter_value(mi, n).get_path_name()
                                      if MEL.get_material_instance_texture_parameter_value(mi, n) else None)
                             for n in MEL.get_texture_parameter_names(mi)}
            d["vectors"] = {str(n): str(MEL.get_material_instance_vector_parameter_value(mi, n)) for n in MEL.get_vector_parameter_names(mi)}
        except Exception as ex:
            d["err"] = str(ex)
        try:
            bpo = mi.get_editor_property("base_property_overrides")
            d["bpo_blend"] = [bpo.get_editor_property("override_blend_mode"), str(bpo.get_editor_property("blend_mode"))]
        except Exception:
            pass
        R["mics"][p] = d
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(R, f, ensure_ascii=False, indent=1)
    log("ЗАВЕРШЕНО")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
