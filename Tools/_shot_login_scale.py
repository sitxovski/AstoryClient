# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _shot_login_scale.py — КОНТРОЛЬНЫЕ КАДРЫ сцены логина ПУТЁМ ИГРОКА (-game, закон
#   ui-check-game-path-geometry). Ничего в проекте не меняет: правит только рантайм-свойства
#   компонентов текущего прогона.
#
# Зачем: владелец говорит «Directional/Spot на логине не работают». Одного кадра мало — нужна
#   КОНТРОЛЬНАЯ ПАРА (закон pixel-metric-needs-control-pairs): один и тот же ракурс при
#   выключенном солнце, при штатном и при ×10. Если три кадра неотличимы — солнце до пикселей
#   действительно не доезжает; если отличимы — «не работает» относится не к диффузу, а к теням.
#
# Кадры → ASDA_LOGIN_SHOT_DIR (умолч. Reports/login_scale), имена login_<tag>_<вариант>.png,
#   рядом login_<tag>_shots.json с яркостью кадра (средняя/медиана по PNG считается отдельно).
#
# Запуск (СКРЫТО):
#   python Tools/set0_run_game.py Tools/_shot_login_scale.py --map /Game/Map/Login \
#          --marker "ЗАВЕРШЕНО" --res 1600x900 --log <лог> --env ASDA_LOGIN_SHOT_TAG=before
import os, glob, json, shutil

OUTD = os.environ.get("ASDA_LOGIN_SHOT_DIR", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/login_scale")
TAG = os.environ.get("ASDA_LOGIN_SHOT_TAG", "before")
SSDIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
SETTLE = int(os.environ.get("ASDA_LOGIN_SETTLE", "300"))   # кадров прогрева (мипы, TSR, виджеты логина)


def main():
    import unreal
    st = {"n": 0, "w": None, "q": [], "seen": set(), "sun": None, "sun_i": None}
    R = {"tag": TAG, "shots": [], "scene": {}, "notes": []}

    def log(m):
        unreal.log("[login-shot] " + str(m))

    def note(m):
        R["notes"].append(str(m)); log("NOTE: " + str(m))

    def cc(c):
        unreal.SystemLibrary.execute_console_command(st["w"], c); log("cmd: " + c)

    def after(frames, fn):
        st["q"].append([frames, fn])

    def run_queue():
        if not st["q"]:
            return
        it = st["q"][0]
        it[0] -= 1
        if it[0] <= 0:
            st["q"].pop(0); it[1]()

    def find_world():
        for path in ("/Game/Map/Login.Login", "/Game/Map/Login"):
            for fn in (unreal.find_object, unreal.load_object):
                try:
                    o = fn(None, path)
                    if o and isinstance(o, unreal.World):
                        return o
                except Exception:
                    pass
        return None

    def new_pngs():
        cur = set(glob.glob(os.path.join(SSDIR, "**", "*.png"), recursive=True))
        new = sorted(cur - st["seen"], key=os.path.getmtime)
        st["seen"] |= cur
        return new

    def take(name):
        new = new_pngs()
        if not new:
            note("нет нового PNG для %s" % name); return
        dst = os.path.join(OUTD, "login_%s_%s.png" % (TAG, name))
        shutil.copy(new[-1], dst)
        R["shots"].append({"name": name, "png": dst, "bytes": os.path.getsize(dst)})
        log("СОХРАНЁН -> %s" % dst)

    def sun():
        if st["sun"] is None:
            for a in unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.DirectionalLight):
                st["sun"] = a
                st["sun_i"] = a.root_component.get_editor_property("intensity")
                break
        return st["sun"]

    def set_sun(mult):
        s = sun()
        if s is None:
            note("Sun не найден"); return
        c = s.root_component
        c.set_editor_property("intensity", float(st["sun_i"]) * float(mult))
        log("Sun intensity = %.4f (×%s от авторских %.4f)" % (float(st["sun_i"]) * mult, mult, st["sun_i"]))

    def dump_scene():
        """Числа сцены глазами ИГРЫ (не редактора): габариты, дистанции, свет."""
        lo = [1e30] * 3; hi = [-1e30] * 3; sizes = []
        for a in unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.StaticMeshActor):
            try:
                org, ext = a.get_actor_bounds(False)
            except Exception:
                continue
            if ext.x + ext.y + ext.z <= 0:
                continue
            if not a.get_actor_label().startswith("BackdropElem"):
                pass
            lo = [min(lo[0], org.x - ext.x), min(lo[1], org.y - ext.y), min(lo[2], org.z - ext.z)]
            hi = [max(hi[0], org.x + ext.x), max(hi[1], org.y + ext.y), max(hi[2], org.z + ext.z)]
            sizes.append(round(2.0 * max(ext.x, ext.y, ext.z), 1))
        sizes.sort()
        R["scene"]["n_static"] = len(sizes)
        if sizes:
            R["scene"]["elem_size"] = {"min": sizes[0], "p50": sizes[len(sizes) // 2],
                                       "p90": sizes[int(len(sizes) * 0.9)], "max": sizes[-1]}
            R["scene"]["bbox_size"] = [round(hi[i] - lo[i], 1) for i in range(3)]
        s = sun()
        if s is not None:
            c = s.root_component
            R["scene"]["sun"] = {
                "intensity": float(c.get_editor_property("intensity")),
                "rot_pitch": float(s.get_actor_rotation().pitch),
                "shadow_dist": float(c.get_editor_property("dynamic_shadow_distance_movable_light")),
                "cascades": int(c.get_editor_property("dynamic_shadow_cascades")),
                "cast_shadows": bool(c.get_editor_property("cast_shadows"))}
        # камера игрока: где она и как далеко до сцены
        pc = unreal.GameplayStatics.get_player_controller(st["w"], 0)
        if pc:
            try:
                cm = pc.player_camera_manager
                loc = cm.get_camera_location()
                R["scene"]["cam_loc"] = [round(loc.x, 1), round(loc.y, 1), round(loc.z, 1)]
                R["scene"]["cam_fov"] = round(float(cm.get_fov_angle()), 2)
                if sizes:
                    mid = [(lo[i] + hi[i]) / 2.0 for i in range(3)]
                    d = ((loc.x - mid[0]) ** 2 + (loc.y - mid[1]) ** 2 + (loc.z - mid[2]) ** 2) ** 0.5
                    R["scene"]["cam_to_center"] = round(d, 1)
            except Exception as e:
                note("камера: %s" % e)
        log("СЦЕНА: %s" % json.dumps(R["scene"], ensure_ascii=False))

    def set_shadows(on):
        sn = sun()
        if sn is None:
            note("Sun не найден"); return
        sn.root_component.set_editor_property("cast_shadows", bool(on))
        log("Sun cast_shadows = %s" % bool(on))

    def variant(name, mult, shadows=None):
        after(1, lambda: set_sun(mult))
        if shadows is not None:
            after(1, lambda: set_shadows(shadows))
        after(60, lambda: cc("HighResShot 1600x900"))
        after(30, lambda: take(name))

    def finish():
        os.makedirs(OUTD, exist_ok=True)
        out = os.path.join(OUTD, "login_%s_shots.json" % TAG)
        with open(out, "w", encoding="utf-8") as f:
            json.dump(R, f, ensure_ascii=False, indent=1)
        log("записано %s" % out)
        log("ЗАВЕРШЕНО")
        cc("quit")

    def phase_init():
        st["w"] = find_world()
        if st["w"] is None:
            unreal.log_error("[login-shot] МИР НЕ НАЙДЕН")
            unreal.SystemLibrary.execute_console_command(None, "quit"); return
        os.makedirs(OUTD, exist_ok=True)
        st["seen"] = set(glob.glob(os.path.join(SSDIR, "**", "*.png"), recursive=True))
        cc("r.SetRes 1600x900w")
        cc("r.ScreenPercentage 100")
        # ★ ЗАМОРОЗКА КАЧАНИЯ ФОНА (штатный ключ отката дирижёра). Без неё попиксельное сравнение
        #   слепо: 632 качающихся элемента дают «шум» 20/255 — больше любого измеряемого сигнала
        #   (закон pixel-metric-needs-control-pairs).
        if os.environ.get("ASDA_LOGIN_FREEZE") == "1":
            cc("asda.LoginBackdropAnim 0")
        after(SETTLE, phase_shots)

    def phase_shots():
        dump_scene()
        variant("A_base", 1.0)       # авторское солнце
        variant("B_sun0", 0.0)       # солнце выключено
        variant("C_sunx10", 10.0)    # солнце ×10
        variant("D_base2", 1.0)      # возврат — контроль повторяемости
        if os.environ.get("ASDA_LOGIN_SHADOW_PAIR") == "1":
            # ★ Контрольная пара ТЕНЕЙ: доказать, что каскады теперь достают до сцены.
            #   Шум сцены (качание фона) снимаем тем же способом — парой одинаковых кадров E/G.
            variant("E_shadow_on", 1.0, shadows=True)
            variant("F_shadow_off", 1.0, shadows=False)
            variant("G_shadow_on2", 1.0, shadows=True)
            # ★ ВТОРОЙ МЕТОД доказательства «в ×100-сцене теней не было»: укорачиваем каскады так,
            #   чтобы их дальность стала МЕНЬШЕ дистанции камера→сцена — ровно положение ДО правки
            #   (каскады 40 000 юн при камере в 139 690 юн от центра). Если кадр совпал с «тени выкл» —
            #   короткие каскады и есть причина плоской картинки.
            after(1, lambda: cc("r.Shadow.DistanceScale 0.005"))
            variant("H_cascades_short", 1.0, shadows=True)
            after(1, lambda: cc("r.Shadow.DistanceScale 1.0"))
            variant("I_shadow_on3", 1.0, shadows=True)
        after(5, finish)

    def tick(_dt):
        st["n"] += 1
        if st["n"] == 1:
            phase_init(); return
        if st["w"] is None:
            return
        try:
            run_queue()
        except Exception as ex:
            import traceback
            unreal.log_error("[login-shot] ошибка шага: %s\n%s" % (ex, traceback.format_exc()))

    unreal.register_slate_post_tick_callback(tick)
    log("старт, tag=%s settle=%d" % (TAG, SETTLE))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
