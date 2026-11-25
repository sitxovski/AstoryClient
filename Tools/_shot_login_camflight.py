# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _shot_login_camflight.py — КОНТРОЛЬНЫЕ КАДРЫ перелёта логина ПУТЁМ ИГРОКА (-game), до и после
#   перевода «сцена летит → камера летит». Ничего в проекте не меняет.
#
# Режимы (ASDA_CF_MODE):
#   flight  — плеер LS_LoginFlight ставится ПРЫЖКОМ в кадры 0 / 30 / 60 / 100 (t = 0, 1.0, 2.0, 3.33 с);
#             на каждом: кадр с освещением и кадр без освещения (ShowFlag.Lighting 0 — картинка, не
#             зависящая от направления света: проверяет ИМЕННО геометрию и ракурс). В конце повтор
#             кадра 30 — шум прогона. Плюс мировые позы камеры/фона/шкатулки в JSON: по ним поза
#             «камера относительно сцены» сравнивается числом, а не только пикселями.
#   charsel — нужен ASDA_TEST_FLOW=1: ждём открытия шкатулки по ЧАСАМ, ставим паузу игры и снимаем
#             два кадра подряд (внутрипрогонный шум).
#   forge   — нужны ASDA_TEST_FLOW=1 и ASDA_TEST_FORGE=1: GameMode сам снимает forge_1..4 по своим
#             таймерам (FScreenshotRequest); забираем их по имени.
# Фон всегда заморожен (asda.LoginBackdropAnim 0, закон pixel-metric-needs-control-pairs).
#
# Запуск:
#   python Tools/set0_run_game.py Tools/_shot_login_camflight.py --map /Game/Map/Login --marker ЗАВЕРШЕНО \
#          --res 1600x900 --env ASDA_CF_TAG=before --env ASDA_CF_MODE=flight
import os, glob, json, shutil, time, math

OUTD = os.environ.get("ASDA_CF_DIR", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/login_camflight")
TAG = os.environ.get("ASDA_CF_TAG", "before")
MODE = os.environ.get("ASDA_CF_MODE", "flight")
SSDIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
# ★ Конец перелёта снимаем в 99.999, а НЕ в 100: конец диапазона воспроизведения ИСКЛЮЧЁН, прыжок ровно в 100
#   секвенцию не вычисляет и поза остаётся от прошлого прыжка (замер 13.09: «f100» совпал с «f060»).
FRAMES = [float(x) for x in os.environ.get("ASDA_CF_FRAMES", "0,30,60,99.999").split(",")]
SETTLE = int(os.environ.get("ASDA_CF_SETTLE", "240"))          # тиков прогрева (мипы, TSR)
CHARSEL_WAIT = float(os.environ.get("ASDA_CF_CHARSEL_SEC", "16"))   # с от старта до кадра char-select
FORGE_WAIT = float(os.environ.get("ASDA_CF_FORGE_SEC", "34"))       # с от старта до сбора кадров форджинга


def main():
    import unreal
    st = {"n": 0, "w": None, "q": [], "seen": set(), "t0": time.time(), "player": None, "wait_until": None}
    R = {"tag": TAG, "mode": MODE, "shots": [], "poses": {}, "notes": []}

    def log(m):
        unreal.log("[camflight-shot] " + str(m))

    def note(m):
        R["notes"].append(str(m)); log("NOTE: " + str(m))

    def cc(c):
        unreal.SystemLibrary.execute_console_command(st["w"], c); log("cmd: " + c)

    def after(frames, fn):
        st["q"].append([frames, fn])

    def run_queue():
        if st["wait_until"] is not None:
            if time.time() < st["wait_until"]:
                return
            st["wait_until"] = None
        if not st["q"]:
            return
        it = st["q"][0]
        it[0] -= 1
        if it[0] <= 0:
            st["q"].pop(0); it[1]()

    def wait_sec_from_start(sec):
        def _w():
            st["wait_until"] = st["t0"] + sec
            log("жду до t=%.1f с от старта" % sec)
        after(1, _w)

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
        dst = os.path.join(OUTD, "cf_%s_%s.png" % (TAG, name))
        shutil.copy(new[-1], dst)
        R["shots"].append({"name": name, "png": dst})
        log("СОХРАНЁН -> %s" % dst)

    def shot(name, delay=45):
        after(delay, lambda: cc("HighResShot 1600x900"))
        after(30, lambda: take(name))

    def v3(v):
        return [round(float(v.x), 4), round(float(v.y), 4), round(float(v.z), 4)]

    def rot3(r):
        return [round(float(r.pitch), 4), round(float(r.yaw), 4), round(float(r.roll), 4)]

    def pose(name):
        p = {}
        pc = unreal.GameplayStatics.get_player_controller(st["w"], 0)
        if pc and pc.player_camera_manager:
            cm = pc.player_camera_manager
            p["cam_loc"] = v3(cm.get_camera_location()); p["cam_rot"] = rot3(cm.get_camera_rotation())
        for a in unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.Actor):
            try:
                lbl = a.get_actor_label()
            except Exception:
                lbl = a.get_name()
            if lbl in ("LoginBackdrop_Flight", "LoginCasket_cre_creation01", "LoginCamera"):
                t = a.get_actor_transform()
                p[lbl] = {"loc": v3(t.translation), "rot": rot3(t.rotation.rotator()), "scale": v3(t.scale3d)}
        R["poses"][name] = p
        log("ПОЗА %s: %s" % (name, json.dumps(p, ensure_ascii=False)))

    def get_player():
        if st["player"] is not None:
            return st["player"]
        for a in unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.LevelSequenceActor):
            pl = None
            for getter in ("get_sequence_player",):
                try:
                    pl = getattr(a, getter)()
                except Exception:
                    pl = None
            if pl is None:
                try:
                    pl = a.get_editor_property("sequence_player")
                except Exception:
                    pl = None
            if pl is not None:
                st["player"] = pl
                log("плеер секвенции найден у %s" % a.get_name())
                break
        if st["player"] is None:
            note("LevelSequenceActor/плеер не найден")
        return st["player"]

    def jump(frame):
        pl = get_player()
        if pl is None:
            return
        ok = False
        errs = []
        whole = int(math.floor(frame)); sub = float(frame - whole)
        ft = unreal.FrameTime(unreal.FrameNumber(whole))
        try:
            ft.set_editor_property("sub_frame", sub)
        except Exception as e:
            errs.append("sub_frame: %s" % e)
        try:
            params = unreal.MovieSceneSequencePlaybackParams()
            params.set_editor_property("frame", ft)
            params.set_editor_property("position_type", unreal.MovieScenePositionType.FRAME)
            params.set_editor_property("update_method", unreal.UpdatePositionMethod.JUMP)
            pl.set_playback_position(params)
            ok = True
        except Exception as e:
            errs.append("set_playback_position: %s" % e)
        if not ok:
            try:
                pl.jump_to_frame(ft)
                ok = True
            except Exception as e:
                errs.append("jump_to_frame: %s" % e)
        try:
            _ct = pl.get_current_time().time
            cur = "%d+%.4f" % (_ct.frame_number.value, _ct.sub_frame)
        except Exception:
            cur = "?"
        log("прыжок в кадр %.3f: %s (текущий %s) %s" % (frame, "ОК" if ok else "НЕ ВЫШЛО", cur, errs))
        if not ok:
            note("прыжок в кадр %.3f не удался: %s" % (frame, errs))

    def finish():
        out = os.path.join(OUTD, "cf_%s_%s.json" % (TAG, MODE))
        with open(out, "w", encoding="utf-8") as f:
            json.dump(R, f, ensure_ascii=False, indent=1)
        log("записано %s" % out)
        log("ЗАВЕРШЕНО")
        cc("quit")

    def phase_flight():
        for fr in FRAMES:
            nm = "f%03d" % int(round(fr))
            after(1, lambda fr=fr: jump(fr))
            after(40, lambda nm=nm: pose(nm))
            shot(nm + "_lit", 5)
            after(1, lambda: cc("ShowFlag.Lighting 0"))
            shot(nm + "_unlit", 40)
            after(1, lambda: cc("ShowFlag.Lighting 1"))
        # шум прогона: тот же кадр 30 ещё раз
        after(1, lambda: jump(30))
        after(40, lambda: pose("f030_rep"))
        shot("f030_lit_rep", 5)
        after(1, lambda: cc("ShowFlag.Lighting 0"))
        shot("f030_unlit_rep", 40)
        after(1, lambda: cc("ShowFlag.Lighting 1"))
        after(5, finish)

    def phase_charsel():
        wait_sec_from_start(CHARSEL_WAIT)
        after(1, lambda: pose("charsel"))
        after(1, lambda: cc("pause"))
        shot("charsel_A", 30)
        shot("charsel_B", 30)
        after(5, finish)

    def phase_forge():
        wait_sec_from_start(FORGE_WAIT)
        def collect():
            got = 0
            for nm in ("forge_1_close", "forge_2_flash", "forge_3_glow", "forge_4_open"):
                c = sorted(glob.glob(os.path.join(SSDIR, "**", nm + "*.png"), recursive=True), key=os.path.getmtime)
                if c:
                    dst = os.path.join(OUTD, "cf_%s_%s.png" % (TAG, nm))
                    shutil.copy(c[-1], dst); R["shots"].append({"name": nm, "png": dst}); got += 1
                else:
                    note("кадр форджинга %s не найден" % nm)
            log("кадров форджинга собрано %d" % got)
        after(1, collect)
        after(1, lambda: pose("forge_end"))
        after(5, finish)

    def phase_init():
        st["w"] = find_world()
        if st["w"] is None:
            unreal.log_error("[camflight-shot] МИР НЕ НАЙДЕН")
            unreal.SystemLibrary.execute_console_command(None, "quit"); return
        os.makedirs(OUTD, exist_ok=True)
        if os.path.isdir(SSDIR):
            for f in glob.glob(os.path.join(SSDIR, "**", "forge_*.png"), recursive=True):
                try:
                    os.remove(f)          # чужие forge-кадры прошлых прогонов не должны подмешаться
                except Exception:
                    pass
        st["seen"] = set(glob.glob(os.path.join(SSDIR, "**", "*.png"), recursive=True))
        cc("r.SetRes 1600x900w")
        cc("r.ScreenPercentage 100")
        cc("asda.LoginBackdropAnim 0")
        if MODE == "flight":
            after(SETTLE, phase_flight)
        elif MODE == "charsel":
            after(SETTLE, phase_charsel)
        else:
            after(SETTLE, phase_forge)

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
            unreal.log_error("[camflight-shot] ошибка шага: %s\n%s" % (ex, traceback.format_exc()))

    unreal.register_slate_post_tick_callback(tick)
    log("старт, tag=%s mode=%s" % (TAG, MODE))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
