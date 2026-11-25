# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_backdrop_materials.py — ЗАМЕР (только чтение): материалы/меши элементов задника логина для разбора теней.
#   Первый дамп (_probe_backdrop_shadows.py) показал: у SM_m* get_num_materials()=0, материал живёт только в
#   override_materials компонента. Здесь — MIC из override, их родители, выражения мастера (WPO/PDO/ShadowPassSwitch),
#   слоты и секции мешей. Login.umap только загружается, НЕ сохраняется.
import os, json

OUT = os.environ.get("ASDA_PROBE_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/backdrop_shadows/probe_backdrop_materials.json")


def main():
    import unreal
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    MEL = unreal.MaterialEditingLibrary

    def log(m):
        unreal.log_warning("[probe-bdmat] %s" % m)

    def gp(o, names):
        d = {}
        for n in names:
            try:
                v = o.get_editor_property(n)
                d[n] = v if isinstance(v, (bool, int, float, str)) else str(v)
            except Exception:
                pass
        return d

    assert les.load_level("/Game/Map/Login")
    R = {"mics": {}, "bases": {}, "mesh_slots": {}, "casket_mats": {}}
    mics = {}; bases = {}
    n_override = {}
    for a in eas.get_all_level_actors():
        lbl = a.get_actor_label()
        if a.actor_has_tag("AsdaBackdropElem"):
            for c in a.get_components_by_class(unreal.StaticMeshComponent):
                ov = list(c.get_editor_property("override_materials"))
                n_override[len(ov)] = n_override.get(len(ov), 0) + 1
                for m in ov:
                    if m:
                        mics.setdefault(m.get_path_name(), m)
                sm = c.static_mesh
                if sm and (len(R["mesh_slots"]) < 6 or lbl == "BackdropElem_bg_102_e00_00"):
                    sl = list(sm.get_editor_property("static_materials"))
                    R["mesh_slots"][lbl] = {"mesh": sm.get_path_name(), "static_materials": len(sl),
                                            "slot_mats": [str(s.material_interface.get_path_name()) if s.material_interface else None for s in sl],
                                            "num_sections": unreal.EditorStaticMeshLibrary.get_number_materials(sm)
                                            if hasattr(unreal.EditorStaticMeshLibrary, "get_number_materials") else None,
                                            "comp_get_num_materials": c.get_num_materials(),
                                            "comp_material0": (c.get_material(0).get_path_name() if c.get_material(0) else None)}
        if lbl.startswith("LoginCasket"):
            for c in a.get_components_by_class(unreal.MeshComponent):
                for i in range(c.get_num_materials()):
                    m = c.get_material(i)
                    if m:
                        b = m.get_base_material()
                        R["casket_mats"][m.get_path_name()] = b.get_path_name() if b else None
                        if b:
                            bases.setdefault(b.get_path_name(), b)
    R["override_count_hist"] = n_override
    for p, m in sorted(mics.items()):
        d = {"class": m.get_class().get_name()}
        if isinstance(m, unreal.MaterialInstance):
            par = m.get_editor_property("parent")
            d["parent"] = par.get_path_name() if par else None
            bpo = m.get_editor_property("base_property_overrides")
            d["bpo"] = gp(bpo, ["override_blend_mode", "blend_mode", "override_shading_model", "shading_model",
                                "override_two_sided", "two_sided", "override_opacity_mask_clip_value",
                                "opacity_mask_clip_value", "override_cast_dynamic_shadow_as_masked",
                                "cast_dynamic_shadow_as_masked"])
            try:
                d["scalars"] = {str(n): MEL.get_material_instance_scalar_parameter_value(m, n) for n in MEL.get_scalar_parameter_names(m)}
                d["static_switches"] = {str(n): MEL.get_material_instance_static_switch_parameter_value(m, n)
                                        for n in MEL.get_static_switch_parameter_names(m)}
                d["textures"] = {str(n): (MEL.get_material_instance_texture_parameter_value(m, n).get_path_name()
                                          if MEL.get_material_instance_texture_parameter_value(m, n) else None)
                                 for n in MEL.get_texture_parameter_names(m)}
            except Exception as ex:
                d["param_err"] = str(ex)
            b = m.get_base_material()
            if b:
                bases.setdefault(b.get_path_name(), b)
        R["mics"][p] = d
    MP = unreal.MaterialProperty
    for p, b in bases.items():
        d = gp(b, ["blend_mode", "shading_model", "two_sided", "opacity_mask_clip_value", "cast_dynamic_shadow_as_masked",
                   "material_domain", "used_with_static_lighting", "used_with_nanite", "dithered_lod_transition",
                   "use_material_attributes", "always_evaluate_world_position_offset", "is_thin_surface"])
        for prop in ("MP_WORLD_POSITION_OFFSET", "MP_PIXEL_DEPTH_OFFSET", "MP_OPACITY_MASK", "MP_OPACITY",
                     "MP_SHADING_MODEL", "MP_SUBSURFACE_COLOR"):
            try:
                node = MEL.get_material_property_input_node(b, getattr(MP, prop))
                d["input_" + prop] = node.get_class().get_name() if node else None
            except Exception as ex:
                d["input_" + prop] = "err: %s" % ex
        kinds = {}
        try:
            for ex in unreal.ObjectIterator(unreal.MaterialExpression):
                if ex.get_outer() == b:
                    k = ex.get_class().get_name(); kinds[k] = kinds.get(k, 0) + 1
        except Exception as e2:
            kinds["err"] = str(e2)
        d["expressions"] = kinds
        R["bases"][p] = d
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(R, f, ensure_ascii=False, indent=1)
    log("записано %s" % OUT)
    log("ЗАВЕРШЕНО")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
