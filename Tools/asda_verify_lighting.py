# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_verify_lighting.py — ЧИСЛОВАЯ проверка света (headless слеп к картинке): ТОЛЬКО ЧТЕНИЕ, ничего не сохраняет.
#   Для 22 карт + Login: солнце (forward, поворот, сила, цвет, light function), SkyLight (сила, цвет, тип),
#   PPV (экспозиция) и УГОЛ между forward солнца и -C(sunDir) из .mapraw ver>=3 (ожидается 0°; для
#   справки — угол к вектору СТАРОЙ формулы из бэкапа ver=2, если он есть).
#   Для Lit-мастеров: число узлов, дубли параметров, дефолт AmbientFloor (из кэша ассета), вход Emissive,
#   имена скаляров/векторов — и проверка, что имён старой unlit-модели (Ambient/LightColor/LightDir) НЕТ.
#   Итог: JSON (ASDA_VERIFY_OUT) + строка на карту в лог. Маркер: LIGHTVERIFY-FINISHED.
# Запуск (СКРЫТО): python Tools/run_headless_safe.py Tools/asda_verify_lighting.py --marker LIGHTVERIFY-FINISHED --timeout 1500
import os, sys, json, math
import unreal
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_coords as _C
import asda_mapraw as _MR
import asda_maps as _MAPS
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)
import asda_mat_graph as MG

EAL = unreal.EditorAssetLibrary; MEL = unreal.MaterialEditingLibrary; MP = unreal.MaterialProperty
RAW = os.environ.get("ASDA_RAW", r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/maps")
RAW_OLD = os.environ.get("ASDA_RAW_OLD", r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/maps_bak_v2_2026-09-03")
OUT = os.environ.get("ASDA_VERIFY_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/audit_2026-09-02/lighting_verify.json")
MASTERS = ["M_AsdaObjectLit", "M_AsdaTerrainLit", "M_AsdaCharLit", "M_AsdaCharLitSoul",
           "M_AsdaSoulSilhouette", "M_AsdaPreviewFade", "M_AsdaObjectBlendLit"]
OLD_UNLIT_PARAMS = {"Ambient", "LightColor", "LightDir"}


def log(m): unreal.log_warning("[verify] %s" % m)


def unit_travel(v):
    d = _C.to_ue(v[0], v[1], v[2], 1.0)
    n = math.sqrt(d.x * d.x + d.y * d.y + d.z * d.z) or 1.0
    return (-d.x / n, -d.y / n, -d.z / n)


def ang(a, b):
    dot = max(-1.0, min(1.0, a[0] * b[0] + a[1] * b[1] + a[2] * b[2]))
    return math.degrees(math.acos(dot))


def col(c):
    try: return [round(float(c.r), 4), round(float(c.g), 4), round(float(c.b), 4)]
    except Exception: return str(c)


def level_entry(lvl, mapname):
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not EAL.does_asset_exist(lvl): return {"error": "нет уровня"}
    if not les.load_level(lvl): return {"error": "не загрузился"}
    e = {"suns": [], "skys": [], "ppv": None, "builtdata": EAL.does_asset_exist(lvl + "_BuiltData")}
    for a in eas.get_all_level_actors():
        if isinstance(a, unreal.DirectionalLight):
            dc = a.get_component_by_class(unreal.DirectionalLightComponent)
            f = a.get_actor_forward_vector(); r = a.get_actor_rotation()
            lf = None
            try:
                lfm = dc.get_editor_property("light_function_material"); lf = lfm.get_name() if lfm else None
            except Exception: pass
            # угол считаем по НЕокруглённому forward (округление до 4 знаков давало до 0.46° «ошибки» у acos≈1)
            e["suns"].append({"label": a.get_actor_label(), "forward": [round(f.x, 4), round(f.y, 4), round(f.z, 4)],
                              "_fwd_raw": (float(f.x), float(f.y), float(f.z)),
                              "rot": [round(r.pitch, 2), round(r.yaw, 2), round(r.roll, 2)],
                              "intensity": round(float(dc.get_editor_property("intensity")), 4),
                              "color": col(dc.get_editor_property("light_color")),
                              "mobility": str(dc.get_editor_property("mobility")), "light_function": lf})
        elif isinstance(a, unreal.SkyLight):
            sc = a.get_component_by_class(unreal.SkyLightComponent)
            e["skys"].append({"label": a.get_actor_label(), "intensity": round(float(sc.get_editor_property("intensity")), 4),
                              "color": col(sc.get_editor_property("light_color")),
                              "lower": col(sc.get_editor_property("lower_hemisphere_color")),
                              "source": str(sc.get_editor_property("source_type")),
                              "rtc": bool(sc.get_editor_property("real_time_capture")),
                              "mobility": str(sc.get_editor_property("mobility"))})
        elif isinstance(a, unreal.PostProcessVolume):
            s = a.get_editor_property("settings")
            e["ppv"] = {"label": a.get_actor_label(),
                        "exp_min": float(s.get_editor_property("auto_exposure_min_brightness")),
                        "exp_max": float(s.get_editor_property("auto_exposure_max_brightness"))}
    # ожидание из данных
    if mapname:
        raw = os.path.join(RAW, mapname + ".mapraw")
        if os.path.exists(raw):
            sc = _MR.read_scene(raw)
            e["mapraw_ver"] = sc["ver"]; e["ambient"] = [round(x, 4) for x in sc["ambient"]]; e["diffuse"] = [round(x, 4) for x in sc["diffuse"]]
            e["sunDir"] = [round(x, 4) for x in sc["sunDir"]] if sc["sunDir"] else None
            if sc["sunDir"]:
                tr = unit_travel(sc["sunDir"]); e["expected_forward"] = [round(x, 4) for x in tr]
                sun = next((s for s in e["suns"] if s["label"].startswith("Sun_")), e["suns"][0] if e["suns"] else None)
                if sun: e["angle_to_sunDir"] = round(ang(sun["_fwd_raw"], tr), 4)
        rawo = os.path.join(RAW_OLD, mapname + ".mapraw")
        if os.path.exists(rawo):
            sco = _MR.read_scene(rawo)
            tro = unit_travel(sco["lightDir"]); e["old_formula_forward"] = [round(x, 4) for x in tro]
            sun = next((s for s in e["suns"] if s["label"].startswith("Sun_")), e["suns"][0] if e["suns"] else None)
            if sun: e["angle_to_old_formula"] = round(ang(sun["_fwd_raw"], tro), 3)
    for s_ in e["suns"]: s_.pop("_fwd_raw", None)
    return e


def master_entry(name):
    p = "/Game/Materials/" + name
    m = EAL.load_asset(p)
    if not m: return {"error": "нет"}
    em = MEL.get_material_property_input_node(m, MP.MP_EMISSIVE_COLOR)
    sc = [str(n) for n in MEL.get_scalar_parameter_names(m)]; vc = [str(n) for n in MEL.get_vector_parameter_names(m)]
    fl = None
    if "AmbientFloor" in sc: fl = MEL.get_material_default_scalar_parameter_value(m, "AmbientFloor")
    return {"nodes": MG.expression_count(m), "dups": MG.parameter_duplicates(m) or {},
            "AmbientFloor": fl, "emissive_node": em.get_name() if em else None,
            "scalars": sc, "vectors": vc, "old_unlit_params_present": sorted(OLD_UNLIT_PARAMS & (set(sc) | set(vc))),
            "shading": str(m.get_editor_property("shading_model")), "blend": str(m.get_editor_property("blend_mode"))}


def main():
    res = {"levels": {}, "masters": {}}
    for name in MASTERS:
        res["masters"][name] = master_entry(name)
        log("мастер %s: %s" % (name, json.dumps(res["masters"][name], ensure_ascii=False)))
    maps = list(_MAPS.ALL_MAPS)
    for mp in maps:
        asset = _MAPS.map_asset_name(mp)
        e = level_entry(_U.level_path(mp), mp)
        res["levels"][mp] = e
        sun = next((s for s in e.get("suns", []) if s["label"].startswith("Sun_")), (e.get("suns") or [None])[0])
        sky = (e.get("skys") or [None])[0]
        log("%-18s sun=%s rot=%s I=%s col=%s LF=%s | угол↔sunDir=%s (к старой формуле %s) | sky=%s col=%s | exp=%s" % (
            mp, sun and sun["forward"], sun and sun["rot"], sun and sun["intensity"], sun and sun["color"], sun and sun["light_function"],
            e.get("angle_to_sunDir"), e.get("angle_to_old_formula"),
            sky and sky["intensity"], sky and sky["color"], e.get("ppv") and e["ppv"]["exp_min"]))
    e = level_entry("/Game/Map/Login", None); res["levels"]["Login"] = e
    log("Login: suns=%s skys=%s builtdata=%s" % (e.get("suns"), e.get("skys"), e.get("builtdata")))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f: json.dump(res, f, ensure_ascii=False, indent=1)
    log("JSON -> %s" % OUT)
    log("ЗАВЕРШЕНО (LIGHTVERIFY-FINISHED)")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
