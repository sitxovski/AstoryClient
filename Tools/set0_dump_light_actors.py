# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# set0_dump_light_actors.py — СЕТ 0 рендера: снимок ТЕКУЩИХ значений световых акторов из ассетов
#   уровней (Sun_* / Sky_* / PP_* / MapFog / SkyAtmosphere / VolumetricCloud) — владелец тюнил их
#   руками, значения нельзя потерять. Только чтение, ничего не сохраняет. Headless, -NullRHI.
# Выход: @@ASTORY_DEV_ROOT_FWD@@/Reports/render_sets_2026-09-05/set0_actors_before.json
# Запуск: python Tools/run_headless_safe.py Tools/set0_dump_light_actors.py --marker "ЗАВЕРШЕНО"
# ★ Раскатка сета A (05.09, ревизия ВАЖНО №2 — гейт ручных значений на всех картах):
#   ASDA_SET0_DUMP_MAPS="ALL" — все 22 карты asda_maps.ALL_MAPS + Login; "a||b" — подмножество (имена ассетов, Mountain_Flamion);
#   в дамп добавлен купол неба ("skydome": актор/меш/scale/bbox — из него считается ASDA_FOGCUTOFF=dome), fog "enable_volumetric_fog"
#   (ревизия, мелочь 11: прежнее имя volumetric_fog не совпадало), строки Struct без адреса памяти (иначе ложный дифф),
#   light_function_material у MID → путь мастера (имя транзиентного MID _0/_1 меняется при каждой загрузке — ложный дифф).
import json, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_maps as _MAPS

_DEFAULT_MAPS = {
    "alpen": "/Game/Map/alpen/alpen",
    "nightvalley": "/Game/Map/nightvalley/nightvalley",
    "Mountain_Flamion": "/Game/Map/Mountain_Flamion/Mountain_Flamion",
    "castle_of_chess": "/Game/Map/castle_of_chess/castle_of_chess",
}


def select_dump_maps():
    """dict имя→путь уровня по ASDA_SET0_DUMP_MAPS (пусто = прежние 4 карты сета 0)."""
    want = os.environ.get("ASDA_SET0_DUMP_MAPS", "")
    if not want:
        return dict(_DEFAULT_MAPS)
    allm = {}
    for m in _MAPS.ALL_MAPS:
        a = _MAPS.map_asset_name(m)
        allm[a] = "/Game/Map/%s/%s" % (a, a)
    allm["Login"] = "/Game/Map/Login"
    allm["L_Loading"] = "/Game/Map/L_Loading"   # ★ Сет B (05.09): режим shadow ALL проходит и L_Loading
    if want.upper() == "ALL":
        return allm
    return {k: v for k, v in allm.items() if k in want.split("||")}


MAPS = select_dump_maps()
OUT = os.environ.get("ASDA_SET0_DUMP_OUT",   # ★ Сет A (05.09): дамп «после» — в другой файл, «до» не затирать
                     r"@@ASTORY_DEV_ROOT_FWD@@/Reports/render_sets_2026-09-05/set0_actors_before.json")

SUN_PROPS = ["intensity", "light_color", "use_temperature", "temperature", "light_source_angle",
             "light_source_soft_angle", "contact_shadow_length", "contact_shadow_length_in_ws",
             "shadow_filter_sharpen", "shadow_bias", "shadow_slope_bias", "shadow_resolution_scale",
             "dynamic_shadow_distance_movable_light", "dynamic_shadow_cascades",
             "cascade_distribution_exponent", "cascade_transition_fraction", "shadow_amount",
             "cast_shadows", "atmosphere_sun_light", "specular_scale", "volumetric_scattering_intensity",
             "indirect_lighting_intensity", "mobility",
             # ★ Сет E-1 (06.09): DF-тени солнца (дефолт движка True) — разрешённое поле дифф-гейта режима df
             "use_ray_traced_distance_field_shadows", "distance_field_shadow_distance", "distance_field_trace_distance"]
SKY_PROPS = ["intensity", "light_color", "lower_hemisphere_color", "lower_hemisphere_is_black",
             "real_time_capture", "source_type", "sky_distance_threshold", "cubemap_resolution",
             "cast_shadows", "indirect_lighting_intensity", "volumetric_scattering_intensity",
             "occlusion_max_distance", "contrast", "min_occlusion", "mobility"]
FOG_PROPS = ["fog_density", "fog_height_falloff", "fog_inscattering_luminance", "fog_max_opacity",
             "start_distance", "fog_cutoff_distance", "directional_inscattering_luminance",
             "directional_inscattering_exponent", "directional_inscattering_start_distance",
             "volumetric_fog", "enable_volumetric_fog", "volumetric_fog_scattering_distribution", "volumetric_fog_albedo",
             "volumetric_fog_extinction_scale", "second_fog_data", "sky_atmosphere_ambient_contribution_color_scale"]
PP_PROPS = ["auto_exposure_method", "auto_exposure_bias", "auto_exposure_min_brightness",
            "auto_exposure_max_brightness", "auto_exposure_speed_up", "auto_exposure_speed_down",
            "auto_exposure_apply_physical_camera_exposure",
            "ambient_occlusion_intensity", "ambient_occlusion_radius", "ambient_occlusion_static_fraction",
            "ambient_occlusion_power", "ambient_occlusion_bias", "ambient_occlusion_quality",
            "ambient_occlusion_fade_distance", "ambient_occlusion_fade_radius",
            "indirect_lighting_intensity", "color_saturation", "color_contrast", "color_gamma",
            "color_gain", "white_temp", "white_tint", "bloom_intensity", "vignette_intensity",
            "film_slope", "film_toe", "film_shoulder", "sharpen", "reflection_method",
            "dynamic_global_illumination_method", "lumen_scene_lighting_quality"]
PP_OVERRIDE = ["override_auto_exposure_method", "override_auto_exposure_bias",
               "override_auto_exposure_min_brightness", "override_auto_exposure_max_brightness",
               "override_ambient_occlusion_intensity", "override_ambient_occlusion_radius",
               "override_ambient_occlusion_static_fraction", "override_indirect_lighting_intensity",
               "override_color_saturation", "override_color_contrast", "override_white_temp",
               "override_bloom_intensity", "override_reflection_method"]


def sval(v):
    import unreal
    if isinstance(v, (unreal.LinearColor,)):
        return [round(v.r, 5), round(v.g, 5), round(v.b, 5), round(v.a, 5)]
    if isinstance(v, unreal.Color):
        return [v.r, v.g, v.b, v.a]
    if isinstance(v, unreal.Vector):
        return [round(v.x, 3), round(v.y, 3), round(v.z, 3)]
    if isinstance(v, unreal.Rotator):
        return {"roll": round(v.roll, 4), "pitch": round(v.pitch, 4), "yaw": round(v.yaw, 4)}
    if isinstance(v, (int, float, bool, str)) or v is None:
        return v
    return re.sub(r"\s*\(0x[0-9A-Fa-f]+\)", "", str(v))   # адрес памяти Struct — не значение


def grab(obj, props):
    d = {}
    for p in props:
        try:
            d[p] = sval(obj.get_editor_property(p))
        except Exception:
            pass
    return d


def actor_common(a):
    d = {"label": a.get_actor_label(), "class": a.get_class().get_name(),
         "location": sval(a.get_actor_location()), "rotation": sval(a.get_actor_rotation())}
    try:
        d["forward"] = sval(a.get_actor_forward_vector())
    except Exception:
        pass
    return d


def _sky_mesh_name(a):
    """Имя меша StaticMeshActor, если это легаси-небо (SM_sky / SM_skydome), иначе None."""
    try:
        c = a.static_mesh_component
        sm = c.get_editor_property("static_mesh") if c else None
        n = sm.get_name() if sm else None
        return n if n and n.lower() in ("sm_sky", "sm_skydome") else None
    except Exception:
        return None


def dome_info(a):
    """Купол неба: класс, scale, меш, bbox меша (локальные единицы), ближайшая к пивоту грань bbox × scale (юн UE)."""
    import unreal
    d = actor_common(a)
    try:
        d["scale"] = sval(a.get_actor_scale3d())
        c = a.get_editor_property("dome") if a.get_class().get_name() == "AsdaSkyDomeActor" else a.static_mesh_component
        sm = c.get_editor_property("static_mesh") if c else None
        d["mesh"] = sm.get_path_name() if sm else None
        # ★ Доводка сета A (05.09): порядок среди транслюцентов и режим смешивания базового материала слота 0 —
        #   доказательство «купол translucent + под всеми транслюцентами» на каждой карте (гейт setA_fix_driver.py).
        try:
            d["translucency_sort_priority"] = int(c.get_editor_property("translucency_sort_priority"))
        except Exception:
            pass
        try:
            m0 = c.get_material(0) if c else None
            base = m0.get_base_material() if m0 else None
            d["mat0"] = m0.get_path_name() if m0 else None
            d["mat0_blend"] = str(base.get_editor_property("blend_mode")) if base else None
            d["mat0_apply_fogging"] = bool(base.get_editor_property("use_translucency_vertex_fog")) if base else None
            d["mat0_is_sky"] = bool(base.get_editor_property("is_sky")) if base else None
        except Exception as ex:
            d["mat0_err"] = str(ex)
        if sm:
            try:
                b = sm.get_bounding_box(); mn, mx = b.min, b.max
            except Exception:
                bs = sm.get_bounds(); o, ex = bs.origin, bs.box_extent
                mn = unreal.Vector(o.x - ex.x, o.y - ex.y, o.z - ex.z); mx = unreal.Vector(o.x + ex.x, o.y + ex.y, o.z + ex.z)
            d["bbox_min"] = sval(mn); d["bbox_max"] = sval(mx)
            span = max(mx.x - mn.x, mx.y - mn.y, mx.z - mn.z, 1e-6)
            faces = [abs(v) for v in (mn.x, mx.x, mn.y, mx.y, mn.z, mx.z) if abs(v) > 1e-3 * span]
            s = a.get_actor_scale3d(); k = min(abs(s.x), abs(s.y), abs(s.z))
            d["min_face_dist_ue"] = round(min(faces) * k, 1) if faces else None
        try:
            d["follow_camera"] = a.get_editor_property("follow_camera")
        except Exception:
            pass
    except Exception as ex:
        d["error"] = str(ex)
    return d


def dump_map(les, eas, path):
    import unreal
    if not les.load_level(path):
        return {"error": "load failed"}
    e = {"suns": [], "skylights": [], "fogs": [], "postprocess": [], "atmosphere": [], "clouds": [],
         "mobs": 0, "npcs": 0, "other_lights": [], "skydome": [], "isms": []}
    for a in eas.get_all_level_actors():
        cn = a.get_class().get_name()
        # ★ Сет B (05.09): ISM-компоненты `I_<model>` (AsdaMapObjects) — только теневые флаги, для дифф-гейта режима shadow
        #   (разрешённое поле isms.<i>.cast_contact_shadow). Сортировка по имени — порядок не зависит от обхода.
        try:
            for comp in a.get_components_by_class(unreal.InstancedStaticMeshComponent):
                nm = comp.get_name()
                if not nm.startswith("I_"):
                    continue
                e["isms"].append({"label": nm,
                                  "cast_contact_shadow": bool(comp.get_editor_property("cast_contact_shadow")),
                                  "cast_shadow": bool(comp.get_editor_property("cast_shadow")),
                                  # ★ Сет E-1 (06.09): разрешённое поле дифф-гейта режима df (листва вне DF-сцены)
                                  "affect_distance_field_lighting": bool(comp.get_editor_property("affect_distance_field_lighting"))})
        except Exception:
            pass
        if isinstance(a, unreal.DirectionalLight):
            d = actor_common(a)
            d.update(grab(a.get_component_by_class(unreal.DirectionalLightComponent), SUN_PROPS))
            try:
                lf = a.get_component_by_class(unreal.DirectionalLightComponent).get_editor_property("light_function_material")
                d["light_function_material"] = lf.get_path_name() if lf else None
                if lf and isinstance(lf, unreal.MaterialInstanceDynamic):      # транзиентный MID: имя гуляет, мастер — нет
                    par = lf.get_editor_property("parent")
                    d["light_function_material"] = "MID(%s)" % (par.get_path_name() if par else None)
            except Exception:
                pass
            e["suns"].append(d)
        elif isinstance(a, unreal.SkyLight):
            d = actor_common(a)
            d.update(grab(a.get_component_by_class(unreal.SkyLightComponent), SKY_PROPS))
            e["skylights"].append(d)
        elif isinstance(a, unreal.ExponentialHeightFog):
            d = actor_common(a)
            d.update(grab(a.get_component_by_class(unreal.ExponentialHeightFogComponent), FOG_PROPS))
            e["fogs"].append(d)
        elif isinstance(a, unreal.PostProcessVolume):
            d = actor_common(a)
            try:
                d["unbound"] = a.get_editor_property("unbound")
                d["priority"] = a.get_editor_property("priority")
                d["blend_weight"] = a.get_editor_property("blend_weight")
                d["enabled"] = a.get_editor_property("enabled")
            except Exception:
                pass
            s = a.get_editor_property("settings")
            d["settings"] = grab(s, PP_PROPS)
            d["overrides"] = grab(s, PP_OVERRIDE)
            e["postprocess"].append(d)
        elif isinstance(a, unreal.SkyAtmosphere):
            d = actor_common(a)
            c = a.get_component_by_class(unreal.SkyAtmosphereComponent)
            d.update(grab(c, ["rayleigh_scattering_scale", "rayleigh_scattering", "mie_scattering_scale",
                              "mie_absorption_scale", "mie_anisotropy", "multi_scattering_factor",
                              "aerial_pespective_view_distance_scale", "height_fog_contribution",
                              "transmittance_min_light_elevation_angle", "ground_albedo"]))
            e["atmosphere"].append(d)
        elif isinstance(a, unreal.VolumetricCloud):
            e["clouds"].append(actor_common(a))
        elif cn == "AsdaSkyDomeActor" or (cn == "StaticMeshActor" and _sky_mesh_name(a)):
            e["skydome"].append(dome_info(a))
        elif cn == "AsdaMobActor":
            e["mobs"] += 1
        elif cn == "AsdaNpcActor":
            e["npcs"] += 1
        elif isinstance(a, unreal.Light):
            d = actor_common(a)
            try:
                lc = a.get_component_by_class(unreal.LightComponent)
                d.update(grab(lc, ["intensity", "light_color", "mobility", "cast_shadows",
                                   "use_ray_traced_distance_field_shadows"]))   # ★ Сет E-1
            except Exception:
                pass
            e["other_lights"].append(d)
    e["isms"].sort(key=lambda d: d["label"])
    return e


def main():
    import unreal
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    res = {}
    for k, p in MAPS.items():
        unreal.log("[set0-dump] карта %s" % p)
        try:
            res[k] = dump_map(les, eas, p)
        except Exception as ex:
            res[k] = {"error": str(ex)}
        unreal.log("[set0-dump] %s: suns=%d sky=%d fog=%d pp=%d" % (
            k, len(res[k].get("suns", [])), len(res[k].get("skylights", [])),
            len(res[k].get("fogs", [])), len(res[k].get("postprocess", []))))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(res, f, ensure_ascii=False, indent=1)
    unreal.log("[set0-dump] записано %s" % OUT)
    unreal.log("[set0-dump] ЗАВЕРШЕНО")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
