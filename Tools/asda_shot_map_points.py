# Copyright (c) 2026 @sitxovski (Discord)
# asda_shot_map_points.py — кадры уровня карты с камеры на заданные точки (сверка «до/после» правок держателя объектов).
#   Ничего не сохраняет: грузит уровень, ставит временную CameraActor на каждую точку, HighResShot, выходит.
#   Нужен живой RHI: python Tools/run_headless_safe.py Tools/asda_shot_map_points.py --exec-cmds --render-offscreen
#                     --marker "ЗАВЕРШЕНО asda_shot_map_points" --res 1600x900
# env: ASDA_MAP (имя карты), ASDA_POINTS="x,y,z,dist,yaw;..." (UE-см; dist — отлёт камеры, yaw — азимут, град),
#      ASDA_OUTDIR (папка PNG), ASDA_TAG (суффикс имени: before/after).
import os, sys, math
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_maps as _MAPS

MAP = os.environ.get("ASDA_MAP", "t_castle_of_chess")
POINTS = os.environ.get("ASDA_POINTS", "")
OUTDIR = os.environ.get("ASDA_OUTDIR", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/map_children_transforms/shots")
TAG = os.environ.get("ASDA_TAG", "before")
WAIT_TICKS = 240          # прогрев стриминга/шейдеров перед каждым кадром

_state = {"i": -1, "t": 0, "cam": None, "out": None, "h": None}
_pts = []


def _next():
    """Следующая точка: временная камера + запрос кадра; по окончании — маркер и выход."""
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if _state["cam"] is not None:
        eas.destroy_actor(_state["cam"]); _state["cam"] = None
    _state["i"] += 1
    if _state["i"] >= len(_pts):
        unreal.unregister_slate_post_tick_callback(_state["h"])
        unreal.log("ЗАВЕРШЕНО asda_shot_map_points %d" % len(_pts))
        unreal.SystemLibrary.quit_editor()
        return
    x, y, z, d, yaw = _pts[_state["i"]]
    a = math.radians(yaw)
    eye = unreal.Vector(x - d * math.cos(a), y - d * math.sin(a), z + d * 0.9)
    rot = unreal.MathLibrary.find_look_at_rotation(eye, unreal.Vector(x, y, z))
    cam = eas.spawn_actor_from_class(unreal.CameraActor, eye, rot)
    cam.camera_component.set_editor_property("field_of_view", 70.0)
    _state["cam"] = cam
    _state["out"] = os.path.join(OUTDIR, "%s_p%d_%s.png" % (MAP, _state["i"] + 1, TAG))
    _state["t"] = 0


def _tick(dt):
    _state["t"] += 1
    if _state["t"] == WAIT_TICKS:
        unreal.AutomationLibrary.take_high_res_screenshot(1600, 900, _state["out"], camera=_state["cam"])
        unreal.log("SHOT -> %s" % _state["out"])
    elif _state["t"] > WAIT_TICKS and (os.path.exists(_state["out"]) or _state["t"] > WAIT_TICKS + 600):
        if _state["t"] > WAIT_TICKS + 600:
            unreal.log_warning("кадр не записан: %s" % _state["out"])
        _next()


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    for p in POINTS.split(";"):
        if p.strip():
            v = [float(t) for t in p.split(",")]
            _pts.append((v[0], v[1], v[2], v[3], v[4] if len(v) > 4 else 45.0))
    for i in range(len(_pts)):
        f = os.path.join(OUTDIR, "%s_p%d_%s.png" % (MAP, i + 1, TAG))
        if os.path.exists(f):
            os.remove(f)
    a = _MAPS.map_asset_name(MAP)
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level("/Game/Map/%s/%s" % (a, a))
    # ASDA_CM_RESTORE=<colormap_apply.json> (asda_terrain_colormap_empty.py): кадр «до» снятия Colormap — прежние
    #   color_-текстуры возвращаются в MIT_patch_* ТОЛЬКО В ПАМЯТИ (ничего не сохраняется; выход без сохранения).
    cmr = os.environ.get("ASDA_CM_RESTORE", "")
    if cmr:
        import json
        rows = json.load(open(cmr, encoding="utf-8"))["maps"].get(MAP, {}).get("colormaps", [])
        mel = unreal.MaterialEditingLibrary
        for name, tex in rows:
            mic = unreal.EditorAssetLibrary.load_asset("/Game/Map/%s/Materials/%s" % (a, name))
            mel.set_material_instance_texture_parameter_value(mic, "Colormap", unreal.EditorAssetLibrary.load_asset(tex.split(".")[0]))
        unreal.log("Colormap возвращён в памяти: %d MIT" % len(rows))
    _next()
    _state["h"] = unreal.register_slate_post_tick_callback(_tick)


# ★ ЗАКОН [[tools-no-import-side-effects]]
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
