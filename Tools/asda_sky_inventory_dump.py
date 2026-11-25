# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_sky_inventory_dump.py — ИНВЕНТАРЬ НЕБА по уровням (аудит 05.09.2026, sky_audit_report.md): для каждого уровня
#   список акторов 8 компонентов комплекта (SkyDome / MapFog / CloudShadows / CloudField / Sun_ / Sky_ / PP_ / Rim_)
#   с ключевыми полями — ТОЛЬКО ЧТЕНИЕ, ничего не сохраняет. Отличие от set0_dump_light_actors.py: тот не раскрывает
#   поля AsdaCloudFieldActor (cloud_meshes/material/count/wind) и AsdaCloudShadowsActor (sun/cloud_texture/master) —
#   а именно пустой cloud_meshes даёт «облака-сферы» вместо метабол-мешей cloudblob_a..d.
# env: ASDA_SKY_DUMP_MAPS = ALL (22 карты asda_maps.ALL_MAPS + Login + L_Loading) | "a||b";
#      ASDA_SKY_DUMP_OUT  = путь JSON (умолч. Reports/render_sets_2026-09-05/sky_audit_dump.json).
# Запуск (скрыто, редактор закрыт): python Tools/run_headless_safe.py Tools/asda_sky_inventory_dump.py --marker "SKYDUMP ЗАВЕРШЕНО"
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_maps as _MAPS
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)
import unreal

OUT = os.environ.get("ASDA_SKY_DUMP_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/render_sets_2026-09-05/sky_audit_dump.json")


def select_maps():
    """dict имя→путь уровня по ASDA_SKY_DUMP_MAPS (ALL = 22 карты + Login + L_Loading)."""
    want = os.environ.get("ASDA_SKY_DUMP_MAPS", "ALL")
    allm = {}
    for m in _MAPS.ALL_MAPS:
        a = _MAPS.map_asset_name(m)
        allm[a] = _U.level_path(m)
    allm["Login"] = "/Game/Map/Login"
    allm["L_Loading"] = "/Game/Map/L_Loading"
    if want.upper() == "ALL":
        return allm
    return {k: v for k, v in allm.items() if k in want.split("||")}


def _name(o):
    """Путь ассета/объекта или None."""
    try:
        return o.get_path_name() if o else None
    except Exception:
        return str(o)


def _rgb(c):
    """LinearColor → [r,g,b] округлённо (строка, если не цвет)."""
    try:
        return [round(c.r, 4), round(c.g, 4), round(c.b, 4)]
    except Exception:
        return str(c)


def _vec(v):
    return [round(v.x, 1), round(v.y, 1), round(v.z, 1)]


def _base(a):
    """Общие поля актора: ярлык, класс, позиция, масштаб."""
    return {"label": a.get_actor_label(), "class": a.get_class().get_name(),
            "loc": _vec(a.get_actor_location()), "scale": _vec(a.get_actor_scale3d())}


def _props(a, d, keys):
    for k in keys:
        v = a.get_editor_property(k)
        d[k] = round(v, 4) if isinstance(v, float) else v


def dump_level(path):
    """Снимок 8 компонентов неба одного уровня (+ легаси SM_sky и посторонние VolumetricCloud/SkyAtmosphere)."""
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not les.load_level(path):
        return {"error": "load_level=False"}
    e = {"SkyDome": [], "MapFog": [], "CloudShadows": [], "CloudField": [], "Sun_": [], "Sky_": [], "PP_": [], "Rim_": [],
         "legacy_sky": [], "other": []}
    for a in eas.get_all_level_actors():
        cls = a.get_class().get_name()
        try:
            if cls == "AsdaSkyDomeActor":
                d = _base(a); c = a.get_editor_property("dome")
                d["mesh"] = _name(c.static_mesh) if c else None
                d["materials"] = [_name(m) for m in (c.get_materials() if c else [])]
                d["sort_priority"] = c.get_editor_property("translucency_sort_priority") if c else None
                d["follow_camera"] = a.get_editor_property("follow_camera")
                e["SkyDome"].append(d)
            elif cls == "ExponentialHeightFog":
                d = _base(a); c = a.get_component_by_class(unreal.ExponentialHeightFogComponent.static_class())
                _props(c, d, ("fog_density", "fog_height_falloff", "start_distance", "fog_cutoff_distance", "fog_max_opacity",
                              "enable_volumetric_fog"))
                d["fog_inscattering_luminance"] = _rgb(c.get_editor_property("fog_inscattering_luminance"))
                e["MapFog"].append(d)
            elif cls == "AsdaCloudShadowsActor":
                d = _base(a)
                sun = a.get_editor_property("sun")
                d["sun"] = sun.get_actor_label() if sun else None
                d["cloud_texture"] = _name(a.get_editor_property("cloud_texture"))
                d["master"] = _name(a.get_editor_property("master"))
                e["CloudShadows"].append(d)
            elif cls == "AsdaCloudFieldActor":
                d = _base(a)
                d["material"] = _name(a.get_editor_property("material"))
                d["cloud_meshes"] = [_name(m) for m in a.get_editor_property("cloud_meshes")]
                _props(a, d, ("count", "solid_fraction", "z_min", "z_max", "scale_min", "scale_max", "wind_yaw", "wind_speed",
                              "high_alt_slowdown", "cast_shadows", "opacity_min", "opacity_max", "seed"))
                ae = a.get_editor_property("area_extent"); d["area_extent"] = [round(ae.x, 1), round(ae.y, 1)]
                e["CloudField"].append(d)
            elif cls == "DirectionalLight":
                d = _base(a); c = a.get_component_by_class(unreal.DirectionalLightComponent.static_class())
                r = a.get_actor_rotation(); d["rot"] = [round(r.pitch, 2), round(r.yaw, 2), round(r.roll, 2)]
                _props(c, d, ("intensity", "contact_shadow_length", "light_source_angle"))
                d["light_color"] = str(c.get_editor_property("light_color"))
                d["light_function"] = _name(c.get_editor_property("light_function_material"))
                e["Sun_"].append(d)
            elif cls == "SkyLight":
                d = _base(a); c = a.get_component_by_class(unreal.SkyLightComponent.static_class())
                _props(c, d, ("intensity",))
                d["light_color"] = str(c.get_editor_property("light_color"))
                d["lower_hemisphere_color"] = _rgb(c.get_editor_property("lower_hemisphere_color"))
                e["Sky_"].append(d)
            elif cls == "PostProcessVolume":
                d = _base(a); d["unbound"] = a.get_editor_property("unbound"); e["PP_"].append(d)
            elif cls == "AsdaRimLightActor":
                d = _base(a)
                _props(a, d, ("enable", "rim_strength", "rim_power"))
                d["rim_color"] = _rgb(a.get_editor_property("rim_color"))
                d["collection"] = _name(a.get_editor_property("collection"))
                e["Rim_"].append(d)
            elif cls in ("VolumetricCloud", "SkyAtmosphere", "AsdaLayeredCloudsActor"):
                e["other"].append(_base(a))
            elif cls == "StaticMeshActor":
                c = a.static_mesh_component
                n = c.static_mesh.get_name() if c and c.static_mesh else ""
                if n.lower() in ("sm_sky", "sm_skydome"):
                    d = _base(a); d["mesh"] = n; e["legacy_sky"].append(d)
        except Exception as ex:
            e["other"].append({"class": cls, "label": a.get_actor_label(), "error": str(ex)})
    return e


def main():
    """Обход уровней → JSON. Вынесено в функцию (закон [[tools-no-import-side-effects]])."""
    res = {}
    for name, path in select_maps().items():
        unreal.log("[SKYDUMP] %s" % name)
        try:
            res[name] = dump_level(path)
        except Exception as ex:
            res[name] = {"error": str(ex)}
        try: unreal.SystemLibrary.collect_garbage()
        except Exception: pass
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(res, f, ensure_ascii=False, indent=1)
    unreal.log("[SKYDUMP] записано %s (%d уровней)" % (OUT, len(res)))
    unreal.log("=== SKYDUMP ЗАВЕРШЕНО ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
