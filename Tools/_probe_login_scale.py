# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_login_scale.py — ЗАМЕР масштаба и света сцены логина против обычной игровой карты.
#
# Зачем: владелец говорит «на login-сцене масштабы огромные» и «Spot/Directional не работают».
#   Скрипт НИЧЕГО не меняет — только собирает числа, по которым решение принимается по данным:
#     • габариты сцены (общий bbox), габариты типового элемента, дистанция камеры;
#     • то же для игровой карты (ASDA_CMP_MAP, умолч. alpen) — эталон «нормального» масштаба;
#     • источники света уровня со ВСЕМИ параметрами, от которых зависит «работает/не работает»
#       (дальность динамической тени, каскады, радиус затухания, конус);
#     • мастера материалов по каждому актору (Lit/Unlit) — свет физически не может влиять на Unlit.
#
# Запуск (СКРЫТО, редактор закрыт):
#   python Tools/run_headless_safe.py Tools/_probe_login_scale.py --marker PROBE-SCALE-FINISHED
import os, io, json, math
import unreal

OUT = os.environ.get("ASDA_PROBE_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/login_scale/probe_scale.json")
CMP_MAP = os.environ.get("ASDA_CMP_MAP", "alpen")
LEVELS = ["/Game/Map/Login", "/Game/Map/%s/%s" % (CMP_MAP, CMP_MAP)]

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
EAL = unreal.EditorAssetLibrary


def log(m):
    unreal.log_warning("[probe-scale] %s" % m)


def v3(v):
    return [round(float(v.x), 2), round(float(v.y), 2), round(float(v.z), 2)]


def master_of(mat):
    """Путь корневого Material для MaterialInterface + его shading model."""
    m = mat
    guard = 0
    while isinstance(m, unreal.MaterialInstance) and guard < 8:
        p = m.get_editor_property("parent")
        if p is None:
            break
        m = p
        guard += 1
    if isinstance(m, unreal.Material):
        try:
            sm = str(m.get_editor_property("shading_model"))
        except Exception:
            sm = "?"
        return m.get_path_name(), sm
    return (m.get_path_name() if m else None), "?"


def light_info(a):
    d = {"label": a.get_actor_label(), "class": a.get_class().get_name(),
         "loc": v3(a.get_actor_location()), "rot": [round(float(x), 3) for x in
                                                    (a.get_actor_rotation().pitch,
                                                     a.get_actor_rotation().yaw,
                                                     a.get_actor_rotation().roll)]}
    c = a.root_component
    for p in ("intensity", "affects_world", "cast_shadows", "visible", "mobility",
              "attenuation_radius", "source_radius", "outer_cone_angle", "inner_cone_angle",
              "intensity_units", "dynamic_shadow_distance_movable_light",
              "dynamic_shadow_distance_stationary_light", "dynamic_shadow_cascades",
              "cascade_distribution_exponent", "cascade_transition_fraction",
              "shadow_distance_fadeout_fraction", "max_draw_distance", "light_falloff_exponent",
              "use_inverse_squared_falloff", "indirect_lighting_intensity",
              "volumetric_scattering_intensity", "specular_scale", "shadow_bias"):
        try:
            d[p] = str(c.get_editor_property(p))
        except Exception:
            pass
    return d


def scan(level):
    assert les.load_level(level), "не загрузился %s" % level
    w = unreal.EditorLevelLibrary.get_editor_world()
    actual = w.get_path_name() if w else "?"
    assert level.split("/")[-1] in actual, "загрузился НЕ тот уровень: просили %s, открыт %s" % (level, actual)
    res = {"level": level, "actual": actual, "actors": 0, "classes": {}, "lights": [], "cameras": [],
           "masters": {}, "unlit_actors": 0, "lit_actors": 0, "pp": [], "fog": [],
           "bbox": None, "elem_sizes": [], "big_samples": []}
    lo = [1e30] * 3
    hi = [-1e30] * 3
    sizes = []
    for a in eas.get_all_level_actors():
        res["actors"] += 1
        cn = a.get_class().get_name()
        res["classes"][cn] = res["classes"].get(cn, 0) + 1
        if isinstance(a, unreal.Light):
            res["lights"].append(light_info(a))
        if isinstance(a, unreal.CameraActor):
            cc = a.camera_component
            res["cameras"].append({"label": a.get_actor_label(), "loc": v3(a.get_actor_location()),
                                   "rot": [round(float(a.get_actor_rotation().pitch), 3),
                                           round(float(a.get_actor_rotation().yaw), 3),
                                           round(float(a.get_actor_rotation().roll), 3)],
                                   "fov": round(float(cc.get_editor_property("field_of_view")), 3)})
        if isinstance(a, unreal.PostProcessVolume):
            res["pp"].append({"label": a.get_actor_label(),
                              "unbound": str(a.get_editor_property("unbound")),
                              "priority": str(a.get_editor_property("priority"))})
        if isinstance(a, unreal.ExponentialHeightFog):
            res["fog"].append(a.get_actor_label())
        # габариты + материалы у всего, что рисуется
        comps = a.get_components_by_class(unreal.MeshComponent)
        has_unlit = False
        has_lit = False
        for comp in comps:
            try:
                org, ext = a.get_actor_bounds(False)
            except Exception:
                org, ext = None, None
            if ext is not None and (ext.x + ext.y + ext.z) > 0:
                lo = [min(lo[0], org.x - ext.x), min(lo[1], org.y - ext.y), min(lo[2], org.z - ext.z)]
                hi = [max(hi[0], org.x + ext.x), max(hi[1], org.y + ext.y), max(hi[2], org.z + ext.z)]
                sizes.append(2.0 * max(ext.x, ext.y, ext.z))
            for i in range(comp.get_num_materials()):
                m = comp.get_material(i)
                if m is None:
                    continue
                mp, sm = master_of(m)
                key = "%s | %s" % (mp, sm)
                res["masters"][key] = res["masters"].get(key, 0) + 1
                if "Unlit" in sm:
                    has_unlit = True
                else:
                    has_lit = True
            break   # одного компонента достаточно для классификации актора
        if has_unlit and not has_lit:
            res["unlit_actors"] += 1
        elif has_lit:
            res["lit_actors"] += 1
    if sizes:
        sizes.sort()
        n = len(sizes)
        res["elem_sizes"] = {"n": n, "min": round(sizes[0], 1), "p50": round(sizes[n // 2], 1),
                             "p90": round(sizes[int(n * 0.9)], 1), "max": round(sizes[-1], 1),
                             "mean": round(sum(sizes) / n, 1)}
    if lo[0] < 1e29:
        res["bbox"] = {"min": [round(x, 1) for x in lo], "max": [round(x, 1) for x in hi],
                       "size": [round(hi[i] - lo[i], 1) for i in range(3)]}
    # дистанция камеры до центра сцены
    if res["cameras"] and res["bbox"]:
        c = res["cameras"][0]["loc"]
        mid = [(lo[i] + hi[i]) / 2.0 for i in range(3)]
        res["cam_to_scene_center"] = round(math.sqrt(sum((c[i] - mid[i]) ** 2 for i in range(3))), 1)
    return res


def asset_bounds(path):
    a = EAL.load_asset(path)
    if a is None:
        return None
    try:
        b = a.get_bounds()
        return {"origin": v3(b.origin), "extent": v3(b.box_extent),
                "size": [round(2 * b.box_extent.x, 1), round(2 * b.box_extent.y, 1),
                         round(2 * b.box_extent.z, 1)]}
    except Exception as e:
        return {"err": str(e)}


def main():
    data = {"levels": [], "assets": {}}
    for lv in LEVELS:
        try:
            data["levels"].append(scan(lv))
            log("уровень %s — снят" % lv)
        except Exception as e:
            data["levels"].append({"level": lv, "err": str(e)})
            log("уровень %s — ОШИБКА %s" % (lv, e))
    # ключевые ассеты: шкатулка, превью персонажей, элемент задника, персонаж игрового мира
    for p in ("/Game/AsdaAssets/Models/Backdrop/SM_m000",
              "/Game/Map/alpen/Models/SM_terrain",
              "/Game/AsdaAssets/Models/SK_cre_creation01",
              "/Game/AsdaAssets/Models/Casket/SK_cre_creation01",
              "/Game/AsdaAssets/Models/Chars/ch1_m_cremmy_preview",
              "/Game/AsdaAssets/Models/ch1_m_cremmy_preview"):
        b = asset_bounds(p)
        if b:
            data["assets"][p] = b
    # добить поиском по реестру, если путей не угадали
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    for pat in ("creation01", "cremmy_preview", "ch1_m_chst", "ch1_m_face", "SM_m001"):
        for ad in list(ar.get_assets_by_path("/Game/AsdaAssets", recursive=True)) + list(ar.get_assets_by_path("/Game/Map/alpen", recursive=True)):
            nm = str(ad.asset_name)
            if pat in nm and nm not in [k.rsplit("/", 1)[-1] for k in data["assets"]]:
                pn = str(ad.package_name)
                b = asset_bounds(pn)
                if b and "err" not in b:
                    data["assets"][pn] = b
                break
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with io.open(OUT, "w", encoding="utf-8") as f:
        f.write(json.dumps(data, ensure_ascii=False, indent=1))
    log("записано %s" % OUT)
    log("PROBE-SCALE-FINISHED")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
