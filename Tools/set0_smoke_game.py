# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# set0_smoke_game.py — дым-тест возможностей Python в -game (сет 0 рендера): найти мир, тик,
#   акторы, офлайн-павн, HighResShot, Shot, ProfileGPU, vis. Ничего не меняет в проекте.
import os, glob


def main():
    import unreal
    st = {"n": 0, "w": None, "dt": []}
    MAP = os.environ.get("ASDA_SET0_MAP", "/Game/Map/alpen/alpen")

    def log(m):
        unreal.log("[set0-smoke] " + str(m))

    def cc(c):
        unreal.SystemLibrary.execute_console_command(st["w"], c)
        log("cmd: " + c)

    def find_world():
        base = MAP.rsplit("/", 1)[-1]
        for path in (MAP + "." + base, MAP):
            for fn in (unreal.find_object, unreal.load_object):
                try:
                    o = fn(None, path)
                    if o and isinstance(o, unreal.World):
                        log("мир найден: %s через %s (%s)" % (path, fn.__name__, o.get_name()))
                        return o
                    log("не мир: %s %s -> %r" % (fn.__name__, path, o))
                except Exception as e:
                    log("ошибка %s %s: %s" % (fn.__name__, path, e))
        return None

    def tick(dt):
        st["n"] += 1
        n = st["n"]
        if n == 1:
            st["w"] = find_world()
            if st["w"] is None:
                log("МИР НЕ НАЙДЕН")
                unreal.SystemLibrary.execute_console_command(None, "quit")
                return
        w = st["w"]
        st["dt"].append(unreal.GameplayStatics.get_world_delta_seconds(w))
        if n == 60:
            pc = unreal.GameplayStatics.get_player_controller(w, 0)
            log("PC=%r pawn=%r" % (pc, pc.get_pawn() if pc else None))
            for cls in (unreal.DirectionalLight, unreal.SkyLight, unreal.PostProcessVolume,
                        unreal.ExponentialHeightFog, unreal.AsdaMobActor, unreal.AsdaNpcActor):
                try:
                    acts = unreal.GameplayStatics.get_all_actors_of_class(w, cls)
                    log("%s: %d %s" % (cls.__name__, len(acts), [a.get_name() for a in acts[:4]]))
                except Exception as e:
                    log("%s: ошибка %s" % (cls.__name__, e))
            cc("Asda.World.FakeSelfSpawn 167 372 Set0")
            cc("r.ProfileGPU.ShowUI 0")
        if n == 200:
            pc = unreal.GameplayStatics.get_player_controller(w, 0)
            pawn = pc.get_pawn() if pc else None
            log("после спавна: pawn=%r loc=%s" % (pawn, pawn.get_actor_location() if pawn else None))
            s = unreal.Vector(-39800.0, 14200.0, 20000.0)
            e = unreal.Vector(-39800.0, 14200.0, -20000.0)
            hit = unreal.SystemLibrary.line_trace_single(w, s, e, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
                                                         False, [], unreal.DrawDebugTrace.NONE, True)
            log("трасса под камерой: %s" % (hit,))
            loc = unreal.Vector(-39800.0, 14200.0, 3600.0)
            rot = unreal.Rotator(0.0, -40.0, 45.0)   # roll, pitch, yaw позиционно
            cam = None
            try:
                cam = w.spawn_actor(unreal.CameraActor, loc, rot)
            except Exception as ex:
                log("spawn_actor мира не работает: %s" % ex)
            if cam is None:
                try:
                    tr = unreal.Transform(loc, rot, unreal.Vector(1, 1, 1))
                    cam = unreal.GameplayStatics.begin_deferred_actor_spawn_from_class(w, unreal.CameraActor, tr)
                    cam = unreal.GameplayStatics.finish_spawning_actor(cam, tr)
                except Exception as ex:
                    log("deferred spawn не работает: %s" % ex)
            log("камера=%r rot=%s" % (cam, cam.get_actor_rotation() if cam else None))
            if cam and pc:
                pc.set_view_target_with_blend(cam, 0.0)
        if n == 320:
            cc("HighResShot 1920x1080")
        if n == 380:
            cc("ProfileGPU")
        if n == 440:
            cc("Shot showui")
        if n == 500:
            cc("vis ScreenSpaceAO")
        if n in (520, 521):
            cc("Shot")
        if n == 560:
            cc("vis off")
        if n == 600:
            d = st["dt"][-200:]
            log("frame ms avg=%.2f (200 кадров)" % (1000.0 * sum(d) / max(1, len(d))))
            for f in glob.glob(r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots/**/*.png", recursive=True):
                log("PNG: %s %d" % (f, os.path.getsize(f)))
            log("ЗАВЕРШЕНО")
            cc("quit")

    try:
        h = unreal.register_slate_post_tick_callback(tick)
        log("tick зарегистрирован %r" % (h,))
    except Exception as e:
        log("register_slate_post_tick_callback НЕДОСТУПЕН: %s" % e)


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
