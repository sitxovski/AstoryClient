# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_light_test.py — ★ Сет E, фаза E-4 (07.09.2026; спецификация setE4_prep_report.md §3): ПОСТРОИТЬ пробный уровень
#   /Game/Temp/L_LightTest = копия alpen.umap (все актёры: Sun/Sky/MapFog/PP откалиброваны) с 12 локальными светами через
#   AAsdaLocalLightActor (пресеты UAsdaRenderSettings) у моста k1_a1. Папка /Game/Temp не кукается (DefaultGame.ini DirectoriesToNeverCook).
#   Правки копии: Sun_alpen intensity × ASDA_LT_SUN_SCALE (0.15 ≈ ночь: фонари читаются), Sky_alpen × ASDA_LT_SKY_SCALE (0.3),
#   MapFog.enable_volumetric_fog=True (свечение в тумане на High/Epic). Исходные значения — в лог и в JSON.
#   Расстановка (T = цель камер alpen (−37958, 16042, 1477), ось дороги yaw 45°, шаг 600 юн, высота = земля + 300):
#     LT_Main_1..3   StreetLantern/Main  (0), (+1700), (−1700) вдоль дороги — тени (3 point = 18 VSM-карт на Epic; бюджет 8/4/2/0)
#     LT_Fill_1..7   StreetLantern/Fill  ×0.5, ряд через 600 юн, +250 вбок; №7 на 4200 — проверка MaxDrawDistance 4000 (Epic) / 2000 (Low ×0.5)
#     LT_Glow_1..2   CrystalGlow/Glow    сфера r=60 с эмиссивом (/Game/Temp/M_LT_Glow, EmissiveMultiplier) + спутник; у опор моста ±400 вбок
#     LT_Torch_1     Torch/Fill          стена дома T+(−600,+900), земля + 150
#   Высота земли — UAsdaWalkGrid alpen (/Game/Map/alpen/DA_WalkGrid: game X = UE.y, game Z = −UE.x), как в set0_shot_game.py.
#   Пишет: Reports/render_sets_2026-09-05/set0_lighttest_cams.json (4 камеры C1..C4 с высотами по земле) и setE4/lighttest_build.json.
#   Запуск (headless, скрыто): python Tools/run_headless_safe.py Tools/asda_build_light_test.py --marker LIGHTTEST-BUILT
#   ASDA_LT_REBUILD=1 (умолч.) — удалить существующий L_LightTest и построить заново; ASDA_LT_SUN_SCALE / ASDA_LT_SKY_SCALE.
#   Импорт без побочных эффектов (закон tools-no-import-side-effects); unreal — лениво в main().
import os, sys, json, math

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
if TOOLS_DIR not in sys.path: sys.path.insert(0, TOOLS_DIR)
SRC_LEVEL = "/Game/Map/alpen/alpen"
DST_PKG, DST_NAME = "/Game/Temp", "L_LightTest"
DST_LEVEL = DST_PKG + "/" + DST_NAME
GRID_PATH = "/Game/Map/alpen/DA_WalkGrid.DA_WalkGrid"
MAT_PKG, MAT_NAME = "/Game/Temp", "M_LT_Glow"
OUTD = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/render_sets_2026-09-05"
SUN_SCALE = float(os.environ.get("ASDA_LT_SUN_SCALE", "0.15"))
SKY_SCALE = float(os.environ.get("ASDA_LT_SKY_SCALE", "0.3"))
REBUILD = os.environ.get("ASDA_LT_REBUILD", "1") == "1"
T = (-37958.30425057301, 16041.695749426986, 1476.6712646484375)   # цель камер k1_a1 (set0_alpen_cams.json target)
YAW = 45.0
MARKER = "ЗАВЕРШЕНО (LIGHTTEST-BUILT)"
GLOW_RGB = (96, 208, 255)


def main():
    import unreal
    log = lambda m: unreal.log("[LT] " + str(m))
    EAL = unreal.EditorAssetLibrary
    R = {"level": DST_LEVEL, "sun_scale": SUN_SCALE, "sky_scale": SKY_SCALE, "lights": [], "notes": []}

    # 1) копия уровня
    if EAL.does_asset_exist(DST_LEVEL):
        if REBUILD:
            ok = EAL.delete_asset(DST_LEVEL); log("удалён прежний %s: %s" % (DST_LEVEL, ok))
        else:
            log("уровень уже есть, ASDA_LT_REBUILD=0 — используем как есть")
    if not EAL.does_asset_exist(DST_LEVEL):
        d = EAL.duplicate_asset(SRC_LEVEL, DST_LEVEL)
        log("duplicate_asset %s → %s: %s" % (SRC_LEVEL, DST_LEVEL, d))
        if not d:
            unreal.log_error("[LT] не удалось скопировать уровень"); unreal.log(MARKER); return
        # ★ Грабли первого прогона 07.09: python-ссылка на копию мира не даёт GC выгрузить её при load_level →
        #   Fatal «Old world … not cleaned up by garbage collection» (EditorServer.cpp:2544). Сохранить на диск, отпустить, собрать мусор.
        EAL.save_asset(DST_LEVEL)
        d = None
        unreal.SystemLibrary.collect_garbage()
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not les.load_level(DST_LEVEL):
        unreal.log_error("[LT] load_level %s не удался" % DST_LEVEL); unreal.log(MARKER); return
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    log("мир: %s" % world.get_name())

    # 2) солнце/небо/туман копии
    for a in eas.get_all_level_actors():
        lbl = a.get_actor_label()
        if isinstance(a, unreal.DirectionalLight):
            c = a.get_component_by_class(unreal.DirectionalLightComponent)
            i0 = c.get_editor_property("intensity"); c.set_editor_property("intensity", i0 * SUN_SCALE)
            R["sun"] = {"label": lbl, "intensity_before": i0, "intensity_after": i0 * SUN_SCALE}
            log("%s: intensity %.4f → %.4f (×%.2f)" % (lbl, i0, i0 * SUN_SCALE, SUN_SCALE))
        elif isinstance(a, unreal.SkyLight):
            c = a.get_component_by_class(unreal.SkyLightComponent)
            i0 = c.get_editor_property("intensity"); c.set_editor_property("intensity", i0 * SKY_SCALE)
            R["sky"] = {"label": lbl, "intensity_before": i0, "intensity_after": i0 * SKY_SCALE}
            log("%s: intensity %.4f → %.4f (×%.2f)" % (lbl, i0, i0 * SKY_SCALE, SKY_SCALE))
        elif isinstance(a, unreal.ExponentialHeightFog):
            c = a.get_component_by_class(unreal.ExponentialHeightFogComponent)
            v0 = c.get_editor_property("enable_volumetric_fog"); c.set_editor_property("enable_volumetric_fog", True)
            R["fog"] = {"label": lbl, "volumetric_before": bool(v0), "volumetric_after": True,
                        "volumetric_fog_distance": c.get_editor_property("volumetric_fog_distance")}
            log("%s: enable_volumetric_fog %s → True" % (lbl, v0))

    # 3) земля (WalkGrid alpen)
    grid = unreal.load_object(None, GRID_PATH)
    def ground_z(x, y):
        if grid is None: return None
        try:
            r = grid.get_ground_y(float(y), float(-x))
            if isinstance(r, tuple): return float(r[1]) if r[0] else None
            return float(r)
        except Exception: return None
    gz = ground_z(T[0], T[1]); log("земля под T: %s (T.z=%.1f)" % (gz, T[2]))
    R["ground_source"] = GRID_PATH if gz is not None else "нет WalkGrid — T.z"
    def Z(x, y, h):
        g = ground_z(x, y)
        return (g if g is not None else T[2]) + h

    # 4) эмиссив-материал кристалла: BaseColor = цвет, Emissive = цвет × EmissiveMultiplier (скаляр, дефолт 2 — доводка E-4)
    mel = unreal.MaterialEditingLibrary
    atools = unreal.AssetToolsHelpers.get_asset_tools()
    mpath = MAT_PKG + "/" + MAT_NAME
    if EAL.does_asset_exist(mpath): EAL.delete_asset(mpath)
    m = atools.create_asset(MAT_NAME, MAT_PKG, unreal.Material, unreal.MaterialFactoryNew())
    col = mel.create_material_expression(m, unreal.MaterialExpressionConstant3Vector, -500, 0)
    col.set_editor_property("constant", unreal.LinearColor(GLOW_RGB[0] / 255.0, GLOW_RGB[1] / 255.0, GLOW_RGB[2] / 255.0, 1.0))
    mul = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -500, 200)
    mul.set_editor_property("parameter_name", "EmissiveMultiplier"); mul.set_editor_property("default_value", 2.0)   # доводка E-4 07.09: 3→2 (ревизия М-5: 3 пересвечивал кристалл; = CrystalGlow.emissive.emissive_multiplier в JSON)
    mult = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -250, 100)
    mel.connect_material_expressions(col, "", mult, "A"); mel.connect_material_expressions(mul, "", mult, "B")
    mel.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(mult, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m); EAL.save_asset(mpath)
    log("материал %s создан (EmissiveMultiplier=2, RGB %s)" % (mpath, GLOW_RGB))
    sphere = unreal.load_asset("/Engine/BasicShapes/Sphere")

    # 5) расстановка
    u = (math.cos(math.radians(YAW)), math.sin(math.radians(YAW)))       # вдоль дороги
    v = (-u[1], u[0])                                                     # вбок (влево от направления)
    cls = unreal.AsdaLocalLightActor
    # Имена значений enum в python-API UE зависят от пайтонизации (STREET_LANTERN / STREETLANTERN…) — берём по имени без регистра/подчёркиваний
    def enum_val(E, name):
        want = name.replace("_", "").lower()
        for n in dir(E):
            if not n.startswith("_") and n.replace("_", "").lower() == want: return getattr(E, n)
        raise AttributeError("%s: нет значения %s среди %s" % (E, name, [n for n in dir(E) if not n.startswith("_")]))
    log("AsdaLightPreset: %s" % [n for n in dir(unreal.AsdaLightPreset) if not n.startswith("_")])
    class _E:
        pass
    EP, ER = _E(), _E()
    for n in ("StreetLantern", "Torch", "Candle", "CrystalGlow", "Showcase", "Custom"):
        setattr(EP, {"StreetLantern": "STREET_LANTERN", "CrystalGlow": "CRYSTAL_GLOW"}.get(n, n.upper()), enum_val(unreal.AsdaLightPreset, n))
    for n in ("Main", "Fill", "Glow"):
        setattr(ER, n.upper(), enum_val(unreal.AsdaLightRole, n))
    def spawn(label, preset, role, along, side, h, scale=1.0, glow=False):
        x = T[0] + along * u[0] + side * v[0]; y = T[1] + along * u[1] + side * v[1]; z = Z(x, y, h)
        a = eas.spawn_actor_from_class(cls, unreal.Vector(x, y, z), unreal.Rotator(0, 0, YAW))
        a.set_actor_label(label)
        a.set_editor_property("preset", preset); a.set_editor_property("light_role", role)
        a.set_editor_property("intensity_scale", scale)
        if glow:
            mc = a.get_editor_property("mesh")
            mc.set_static_mesh(sphere); mc.set_world_scale3d(unreal.Vector(1.2, 1.2, 1.2))   # сфера 100 юн × 1.2 → r = 60
            mc.set_material(0, m); mc.set_editor_property("emissive_light_source", True)
        a.apply_preset()
        lc = a.get_active_light()
        rec = {"label": label, "preset": str(preset), "role": str(role), "loc": [x, y, z], "intensity_scale": scale,
               "intensity_cd": lc.get_editor_property("intensity"), "units": str(lc.get_editor_property("intensity_units")),
               "cast_shadows": bool(lc.get_editor_property("cast_shadows")), "radius": lc.get_editor_property("attenuation_radius"),
               "max_draw": lc.get_editor_property("max_draw_distance"), "tags": [str(t) for t in a.tags],
               "resolved_source": a.get_editor_property("resolved_intensity_source"), "sun_lux": a.get_editor_property("resolved_sun_lux")}
        R["lights"].append(rec); log("%s @ (%.0f, %.0f, %.0f): %.3f кд, тени %s, теги %s, %s" % (label, x, y, z, rec["intensity_cd"], rec["cast_shadows"], rec["tags"], rec["resolved_source"]))
        return a
    spawn("LT_Main_1", EP.STREET_LANTERN, ER.MAIN, 0, 0, 300)
    spawn("LT_Main_2", EP.STREET_LANTERN, ER.MAIN, 1700, 0, 300)
    spawn("LT_Main_3", EP.STREET_LANTERN, ER.MAIN, -1700, 0, 300)
    for i in range(1, 8):
        spawn("LT_Fill_%d" % i, EP.STREET_LANTERN, ER.FILL, i * 600, 250, 300, scale=0.5)
    spawn("LT_Glow_1", EP.CRYSTAL_GLOW, ER.GLOW, 0, -400, 80, glow=True)
    spawn("LT_Glow_2", EP.CRYSTAL_GLOW, ER.GLOW, 0, 400, 80, glow=True)
    # стена дома T+(−600,+900) в мировых осях (спецификация даёт смещение в UE-осях, не вдоль дороги)
    tx, ty = T[0] - 600, T[1] + 900
    a = eas.spawn_actor_from_class(cls, unreal.Vector(tx, ty, Z(tx, ty, 150)), unreal.Rotator(0, 0, YAW))
    a.set_actor_label("LT_Torch_1"); a.set_editor_property("preset", EP.TORCH); a.set_editor_property("light_role", ER.FILL); a.apply_preset()
    lc = a.get_active_light()
    R["lights"].append({"label": "LT_Torch_1", "preset": "Torch", "role": "Fill", "loc": [tx, ty, Z(tx, ty, 150)],
                        "intensity_cd": lc.get_editor_property("intensity"), "cast_shadows": bool(lc.get_editor_property("cast_shadows")),
                        "tags": [str(t) for t in a.tags]})
    log("LT_Torch_1: %.3f кд" % lc.get_editor_property("intensity"))

    # 6) камеры (высоты по земле) → set0_lighttest_cams.json
    def cam(name, along, side, h, pitch, yaw, note, base=None):
        bx, by = (base if base else T[:2])
        x = bx + along * u[0] + side * v[0]; y = by + along * u[1] + side * v[1]
        return {"name": name, "loc": [x, y, Z(x, y, h)], "rot": {"roll": 0.0, "pitch": pitch, "yaw": yaw}, "note": note}
    g1 = (T[0] - 400 * v[0], T[1] - 400 * v[1])
    cams = {"_doc": "★ E-4 (07.09.2026): камеры L_LightTest (Tools/asda_build_light_test.py); T = цель k1_a1, дорога yaw 45, земля по WalkGrid alpen",
            "cams": [
                {"name": "lt_over", "loc": [-39800.0, 14200.0, 3600.0], "rot": {"roll": 0.0, "pitch": -25.0, "yaw": 45.0}, "note": "C1 общий вид (= k1_a1, pitch −25): все 12 светов"},
                cam("lt_main", -400, 0, 320, -20.0, 45.0, "C2 400 юн от LT_Main_1 назад по дороге, герой под фонарём — тень героя от point"),
                cam("lt_row", -800, 250, 250, -6.0, 45.0, "C3 начало ряда Fill, взгляд вдоль дороги: гаснут ли №6–7 (MaxDraw 4000 Epic / 2000 Low)"),
                cam("lt_glow", -250, 0, 140, -12.0, 45.0, "C4 250 юн от кристалла LT_Glow_1: эмиссив + спутник", base=g1)],
            "target": list(T)}
    os.makedirs(OUTD, exist_ok=True)
    with open(os.path.join(OUTD, "set0_lighttest_cams.json"), "w", encoding="utf-8") as fh: json.dump(cams, fh, ensure_ascii=False, indent=1)

    # 7) сохранить уровень (read-only Perforce снять тем же путём, что map_lit)
    try:
        from asda_convert_map_lit import _level_writable
        _level_writable(DST_LEVEL)
    except Exception as e: log("_level_writable: %s" % e)
    ok = les.save_current_level()
    log("save_current_level: %s" % ok)
    R["saved"] = bool(ok)
    R["local_light_components"] = sum(1 for a in eas.get_all_level_actors() for _ in a.get_components_by_class(unreal.LocalLightComponent))
    log("локальных компонентов света в уровне: %d" % R["local_light_components"])
    os.makedirs(os.path.join(OUTD, "setE4"), exist_ok=True)
    with open(os.path.join(OUTD, "setE4", "lighttest_build.json"), "w", encoding="utf-8") as fh: json.dump(R, fh, ensure_ascii=False, indent=1, default=str)
    unreal.log(MARKER)


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
