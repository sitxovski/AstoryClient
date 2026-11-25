# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _shot_npc_actor.py — снимок одного запечённого NPC уровня сбоку (визуальная сверка: как стоит модель). Только чтение.
#   env: ASDA_MAP (карта), ASDA_LABEL (подстрока метки актора, напр. raidboard), ASDA_SHOT (png), ASDA_DIST (расстояние камеры, 900),
#        ASDA_AZ (азимут камеры от оси X, градусы, по умолчанию 30).
# Запуск — с живым RHI: python Tools\run_headless_safe.py Tools\_shot_npc_actor.py --exec-cmds --render-offscreen --marker "SHOT-NPC:"
# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
import math
import os


def main():
    import unreal
    mp = os.environ.get("ASDA_MAP", "flammio")
    label = os.environ.get("ASDA_LABEL", "raidboard")
    out = os.environ.get("ASDA_SHOT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/npc_pick_zone_2026-10-22/shot_%s_%s.png" % (mp, label))
    dist = float(os.environ.get("ASDA_DIST", "900"))
    az = math.radians(float(os.environ.get("ASDA_AZ", "30")))
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level("/Game/Map/%s/%s" % (mp, mp))
    act = [a for a in eas.get_all_level_actors() if a.get_class().get_name() == "AsdaNpcActor" and label in a.get_actor_label()]
    if not act:
        unreal.log_warning("SHOT-NPC: актор с «%s» не найден" % label)
        return
    a = act[0]
    loc = a.get_actor_location()
    look = unreal.Vector(loc.x, loc.y, loc.z + 150.0)
    # маркер начала координат актора: тонкий цилиндр высотой 600 (видно, где «центр» NPC относительно модели)
    try:
        cyl = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Cylinder")
        mk = eas.spawn_actor_from_object(cyl, loc)
        mk.set_actor_scale3d(unreal.Vector(0.08, 0.08, 6.0))
    except Exception as ex:
        unreal.log_warning("маркер не поставлен: %s" % ex)
    azs = [float(x) for x in os.environ.get("ASDA_AZS", str(math.degrees(az))).split(",")]
    st = {"n": 0, "h": None, "k": 0, "cur": None}

    def shoot(k):
        a_ = math.radians(azs[k])
        eye = unreal.Vector(loc.x + dist * math.cos(a_), loc.y + dist * math.sin(a_), loc.z + 350.0)
        cam = eas.spawn_actor_from_class(unreal.CameraActor, eye, unreal.MathLibrary.find_look_at_rotation(eye, look))
        try:
            cam.camera_component.set_editor_property("field_of_view", 70.0)
        except Exception:
            pass
        f = out.replace(".png", "_az%d.png" % int(azs[k]))
        st["cur"] = f
        unreal.AutomationLibrary.take_high_res_screenshot(1280, 900, f, camera=cam)
        unreal.log_warning("снимок запрошен: %s %s az=%d -> %s" % (mp, a.get_actor_label(), int(azs[k]), f))

    def tick(_dt):
        st["n"] += 1
        if os.path.exists(st["cur"]) or st["n"] > 600:
            st["k"] += 1
            st["n"] = 0
            if st["k"] < len(azs):
                shoot(st["k"])
                return
            unreal.unregister_slate_post_tick_callback(st["h"])
            unreal.log_warning("SHOT-NPC: готово, снимков %d" % len(azs))
    shoot(0)
    st["h"] = unreal.register_slate_post_tick_callback(tick)


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
