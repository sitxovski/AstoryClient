# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_backdrop_shadows.py — ЗАМЕР (только чтение): почему элементы задника логина (BackdropElem_bg_*)
#   не отбрасывают тени. Уровень Login только ЗАГРУЖАЕТСЯ, НИЧЕГО не сохраняется и не меняется
#   (★★★ Login.umap содержит ручной свет владельца — запрет записи).
# Пишет: ASDA_PROBE_OUT (умолч. Reports/backdrop_shadows/probe_backdrop_shadows.json)
# Запуск: python Tools/run_headless_safe.py Tools/_probe_backdrop_shadows.py --log <лог>
import os, json

OUT = os.environ.get("ASDA_PROBE_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/backdrop_shadows/probe_backdrop_shadows.json")

# Флаги компонента примитива, влияющие на участие в тенях/проходах.
COMP_PROPS = ["cast_shadow", "cast_dynamic_shadow", "cast_static_shadow", "cast_hidden_shadow", "cast_far_shadow",
              "self_shadow_only", "cast_inset_shadow", "cast_shadow_as_two_sided", "cast_contact_shadow",
              "affect_dynamic_indirect_lighting", "affect_distance_field_lighting", "visible_in_ray_tracing",
              "render_in_main_pass", "render_in_depth_pass", "hidden_in_game", "visible", "mobility",
              "bounds_scale", "shadow_cache_invalidation_behavior", "cast_cinematic_shadow",
              "light_attachments_as_group", "use_as_occluder", "treat_as_background_for_occlusion",
              "evaluate_world_position_offset", "world_position_offset_disable_distance", "forced_lod_model",
              "visible_in_scene_capture_only", "hidden_in_scene_capture", "cached_max_draw_distance", "ld_max_draw_distance",
              "override_materials"]
LIGHT_PROPS = ["intensity", "cast_shadows", "cast_static_shadows", "cast_dynamic_shadows", "affects_world", "mobility",
               "shadow_bias", "shadow_slope_bias", "shadow_sharpen", "contact_shadow_length", "cast_translucent_shadows",
               "cast_volumetric_shadow", "cast_raytraced_shadow", "shadow_resolution_scale", "affect_translucent_lighting",
               "transmission", "specular_scale", "dynamic_shadow_distance_movable_light", "dynamic_shadow_distance_stationary_light",
               "dynamic_shadow_cascades", "cascade_distribution_exponent", "cascade_transition_fraction",
               "shadow_distance_fadeout_fraction", "far_shadow_cascade_count", "far_shadow_distance",
               "use_inset_shadows_for_movable_objects", "shadow_amount", "attenuation_radius", "inner_cone_angle",
               "outer_cone_angle", "source_radius", "soft_source_radius", "light_source_angle", "atmosphere_sun_light",
               "virtual_shadow_map_resolution_lod_bias_directional", "use_ray_traced_distance_field_shadows",
               "source_type", "lower_hemisphere_is_black", "occlusion_max_distance", "min_occlusion",
               "cast_shadows_on_translucency", "light_function_material", "use_temperature", "intensity_units"]


def main():
    import unreal
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    def log(m):
        unreal.log_warning("[probe-bdshadow] %s" % m)

    def js(v):
        if isinstance(v, (bool, int, float, str)) or v is None:
            return v
        if isinstance(v, (list, tuple)):
            return [js(x) for x in v]
        if isinstance(v, unreal.Vector):
            return [round(v.x, 2), round(v.y, 2), round(v.z, 2)]
        if isinstance(v, unreal.LightingChannels):
            return {"ch0": v.channel0, "ch1": v.channel1, "ch2": v.channel2}
        if isinstance(v, unreal.Object):
            return v.get_path_name()
        return str(v)

    def gp(o, names):
        d = {}
        for n in names:
            try:
                d[n] = js(o.get_editor_property(n))
            except Exception:
                pass
        return d

    assert les.load_level("/Game/Map/Login")
    world = unreal.EditorLevelLibrary.get_editor_world()
    R = {"elements": {}, "flag_histogram": {}, "meshes": {}, "materials": {}, "parents": {}, "lights": {},
         "casket": {}, "hierarchy": {}, "spot_reach": {}, "cam": {}}

    actors = eas.get_all_level_actors()
    elems = [a for a in actors if a.actor_has_tag("AsdaBackdropElem")]
    log("элементов с тегом: %d" % len(elems))
    R["n_elements"] = len(elems)
    hist = {}
    meshes = {}
    mats = {}
    elem_bounds = []
    for a in elems:
        lbl = a.get_actor_label()
        comps = a.get_components_by_class(unreal.PrimitiveComponent)
        for c in comps:
            p = gp(c, COMP_PROPS)
            p["lighting_channels"] = js(c.get_editor_property("lighting_channels"))
            for k, v in p.items():
                if k in ("bounds_scale", "override_materials", "cached_max_draw_distance"):
                    continue
                key = "%s=%s" % (k, json.dumps(v, ensure_ascii=False))
                hist[key] = hist.get(key, 0) + 1
            if isinstance(c, unreal.StaticMeshComponent):
                sm = c.static_mesh
                if sm:
                    meshes.setdefault(sm.get_path_name(), sm)
                for i in range(c.get_num_materials()):
                    m = c.get_material(i)
                    if m:
                        mats.setdefault(m.get_path_name(), m)
        org, ext = a.get_actor_bounds(False)
        elem_bounds.append((lbl, org, ext))
        if lbl in ("BackdropElem_bg_102_e00_00",) or len(R["elements"]) < 12:
            e = {"class": a.get_class().get_name(), "loc": js(a.get_actor_location()),
                 "scale": js(a.get_actor_scale3d()), "bounds_origin": js(org), "bounds_extent": js(ext),
                 "parent": (a.get_attach_parent_actor().get_actor_label() if a.get_attach_parent_actor() else None),
                 "hidden_in_game_actor": js(a.get_editor_property("hidden")), "comps": []}
            for c in comps:
                cd = {"name": c.get_name(), "class": c.get_class().get_name()}
                cd.update(gp(c, COMP_PROPS))
                cd["lighting_channels"] = js(c.get_editor_property("lighting_channels"))
                if isinstance(c, unreal.StaticMeshComponent):
                    cd["mesh"] = js(c.static_mesh)
                    cd["materials"] = [js(c.get_material(i)) for i in range(c.get_num_materials())]
                e["comps"].append(cd)
            R["elements"][lbl] = e
    R["flag_histogram"] = dict(sorted(hist.items()))
    log("мешей: %d, материалов: %d" % (len(meshes), len(mats)))

    for path, sm in sorted(meshes.items()):
        md = {}
        try:
            ns = sm.get_editor_property("nanite_settings")
            md["nanite_enabled"] = bool(ns.get_editor_property("enabled"))
        except Exception as ex:
            md["nanite_err"] = str(ex)
        md.update(gp(sm, ["support_ray_tracing", "support_uniformly_distributed_sampling", "generate_mesh_distance_field",
                          "light_map_resolution", "min_lod", "lod_group", "distance_field_self_shadow_bias",
                          "extended_bounds", "positive_bounds_extension", "negative_bounds_extension"]))
        try:
            bs = unreal.EditorStaticMeshLibrary.get_lod_build_settings(sm, 0)
            md["build"] = gp(bs, ["generate_distance_field_as_if_two_sided", "distance_field_resolution_scale",
                                  "recompute_normals", "recompute_tangents", "use_full_precision_u_vs"])
        except Exception as ex:
            md["build_err"] = str(ex)
        try:
            md["tris_lod0"] = unreal.EditorStaticMeshLibrary.get_number_verts(sm, 0)
            md["lods"] = unreal.EditorStaticMeshLibrary.get_lod_count(sm)
        except Exception:
            pass
        try:
            b = sm.get_bounds()
            md["bounds_extent"] = js(b.box_extent)
        except Exception:
            pass
        R["meshes"][path] = md
        if len(R["meshes"]) >= 40:
            R["meshes_truncated_at"] = 40
            break
    R["n_meshes"] = len(meshes)

    MP = unreal.MaterialProperty
    parents = {}
    for path, m in sorted(mats.items()):
        d = {"class": m.get_class().get_name()}
        if isinstance(m, unreal.MaterialInstance):
            d["parent"] = js(m.get_editor_property("parent"))
            try:
                bpo = m.get_editor_property("base_property_overrides")
                d["base_property_overrides"] = gp(bpo, [
                    "override_blend_mode", "blend_mode", "override_shading_model", "shading_model",
                    "override_two_sided", "two_sided", "override_opacity_mask_clip_value", "opacity_mask_clip_value",
                    "override_cast_dynamic_shadow_as_masked", "cast_dynamic_shadow_as_masked", "override_dithered_lod_transition"])
            except Exception as ex:
                d["bpo_err"] = str(ex)
            try:
                d["static_switches"] = js([str(x) for x in unreal.MaterialEditingLibrary.get_static_switch_parameter_names(m)])
                d["scalars"] = {str(n): unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(m, n)
                                for n in unreal.MaterialEditingLibrary.get_scalar_parameter_names(m)}
            except Exception:
                pass
            base = m.get_base_material()
            if base:
                parents.setdefault(base.get_path_name(), base)
        R["materials"][path] = d

    for path, base in parents.items():
        d = gp(base, ["blend_mode", "shading_model", "two_sided", "opacity_mask_clip_value", "cast_dynamic_shadow_as_masked",
                      "material_domain", "used_with_static_lighting", "used_with_skeletal_mesh", "used_with_instanced_static_meshes",
                      "used_with_nanite", "dithered_lod_transition", "is_thin_surface", "tangent_space_normal",
                      "use_material_attributes", "always_evaluate_world_position_offset", "allow_negative_emissive_color",
                      "fully_rough", "is_masked", "output_translucent_velocity", "enable_responsive_aa"])
        for prop in ("MP_WORLD_POSITION_OFFSET", "MP_PIXEL_DEPTH_OFFSET", "MP_OPACITY_MASK", "MP_OPACITY", "MP_SHADING_MODEL"):
            try:
                node = unreal.MaterialEditingLibrary.get_material_property_input_node(base, getattr(MP, prop))
                d["input_" + prop] = node.get_class().get_name() if node else None
            except Exception as ex:
                d["input_" + prop] = "err: %s" % ex
        # Все выражения материала: ищем ShadowPassSwitch / ShadowReplace / PerInstance / CameraVector и т.п.
        kinds = {}
        try:
            for ex in unreal.ObjectIterator(unreal.MaterialExpression):
                if ex.get_outer() == base or ex.get_path_name().startswith(base.get_path_name() + ":"):
                    k = ex.get_class().get_name()
                    kinds[k] = kinds.get(k, 0) + 1
        except Exception as e2:
            kinds["err"] = str(e2)
        d["expressions"] = kinds
        R["parents"][path] = d

    # Свет — ТОЛЬКО ЧТЕНИЕ.
    cam = None
    for a in actors:
        lbl = a.get_actor_label()
        if isinstance(a, unreal.Light) or isinstance(a, unreal.SkyLight):
            d = {"class": a.get_class().get_name(), "loc": js(a.get_actor_location()),
                 "rot": [round(a.get_actor_rotation().pitch, 2), round(a.get_actor_rotation().yaw, 2), round(a.get_actor_rotation().roll, 2)],
                 "fwd": js(a.get_actor_forward_vector()), "hidden": js(a.get_editor_property("hidden"))}
            for c in a.get_components_by_class(unreal.SceneComponent):
                if isinstance(c, (unreal.LightComponentBase,)):
                    cd = gp(c, LIGHT_PROPS)
                    try:
                        cd["lighting_channels"] = js(c.get_editor_property("lighting_channels"))
                    except Exception:
                        pass
                    cd["visible"] = js(c.get_editor_property("visible"))
                    d.setdefault("comps", {})[c.get_name()] = cd
            R["lights"][lbl] = d
        if lbl.startswith("LoginCasket"):
            R["casket"][lbl] = {"comps": [dict(gp(c, COMP_PROPS), name=c.get_name(), cls=c.get_class().get_name(),
                                               lighting_channels=js(c.get_editor_property("lighting_channels")),
                                               mats=[js(c.get_material(i)) for i in range(c.get_num_materials())]
                                               if isinstance(c, unreal.MeshComponent) else [])
                                          for c in a.get_components_by_class(unreal.PrimitiveComponent)],
                                "bounds": js(list(a.get_actor_bounds(False)))}
        if lbl in ("LoginBackdrop_Flight", "LoginBackdrop_Anchor", "LoginCamera", "LoginBackdropAnimator"):
            R["hierarchy"][lbl] = {"loc": js(a.get_actor_location()), "scale": js(a.get_actor_scale3d()),
                                   "rot": str(a.get_actor_rotation()),
                                   "parent": a.get_attach_parent_actor().get_actor_label() if a.get_attach_parent_actor() else None}
            if lbl == "LoginCamera":
                cam = a

    # Геометрия охвата: какие элементы попадают в сферу/конус каждого Spot; дистанции камера→элементы.
    for lbl, d in R["lights"].items():
        if d["class"] not in ("SpotLight", "PointLight"):
            continue
        comp = list(d.get("comps", {}).values())[0] if d.get("comps") else {}
        rad = comp.get("attenuation_radius", 0) or 0
        cone = comp.get("outer_cone_angle", 90) or 90
        L = unreal.Vector(*d["loc"])
        F = unreal.Vector(*d["fwd"])
        import math
        cos_c = math.cos(math.radians(cone))
        in_sphere = 0; in_cone = 0; near = []
        for (el, org, ext) in elem_bounds:
            v = org - L
            dist = v.length()
            r_el = ext.length()
            if dist - r_el <= rad:
                in_sphere += 1
                if dist > 1e-3 and (v.dot(F) / dist) >= cos_c - r_el / max(dist, 1.0):
                    in_cone += 1
                    if len(near) < 8:
                        near.append([el, round(dist, 1), round(r_el, 1)])
        R["spot_reach"][lbl] = {"radius": rad, "cone": cone, "elems_in_sphere": in_sphere, "elems_in_cone": in_cone,
                                "sample": near}
    if cam is not None:
        C = cam.get_actor_location()
        ds = sorted(((org - C).length(), el) for (el, org, ext) in elem_bounds)
        R["cam"] = {"loc": js(C), "n": len(ds), "nearest": [[round(x, 1), e] for x, e in ds[:5]],
                    "p50": round(ds[len(ds) // 2][0], 1) if ds else None, "farthest": round(ds[-1][0], 1) if ds else None}
        for (el, org, ext) in elem_bounds:
            if el == "BackdropElem_bg_102_e00_00":
                R["cam"]["bg_102_dist"] = round((org - C).length(), 1)

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(R, f, ensure_ascii=False, indent=1)
    log("записано %s" % OUT)
    log("ЗАВЕРШЕНО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
