# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_spin_shot.py — кадры и ЧИСЛА для задачи «эмиттер вращается вокруг оси ЭФФЕКТА, а не своей».
#
#   По каждому целевому эффекту:
#     * серия ФАЗ (по умолчанию 4) — на каждой фазе `asda.Fx.SectionDump` (мировой центроид секции
#       каждого эмиттера) + кадр `HighResShot`. Если центроид ездит по окружности — это ОРБИТА,
#       если стоит — вращение вокруг своей оси (закон: на статичном кадре вращения не видно).
#     * центроиды складываются в JSON, чтобы сравнивать ДО/ПОСЛЕ без глазомера.
#
#   ★ Кадр снимается КОНСОЛЬНЫМ `HighResShot` (закон: `AutomationLibrary.take_high_res_screenshot`
#     в `-game -RenderOffscreen -unattended` файл НЕ создаёт и об этом не сообщает).
#
#   Запуск:
#     python Tools/set0_run_game.py Tools/asda_fx_spin_shot.py --map /Game/Map/castle_of_chaos/castle_of_chaos \
#            --marker "FXSPIN ЗАВЕРШЕНО" --env "ASDA_FXSPIN_TAG=BEFORE" \
#            --env "ASDA_FXSPIN_TARGETS=MapEffect_eff_mdel_obsdoor02-a_00_03:1400:400:35"
import os, io, json, glob, shutil, time, math

TAG  = os.environ.get("ASDA_FXSPIN_TAG", "BEFORE")
OUTD = os.environ.get("ASDA_FXSPIN_OUTD", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/fx_spin")
RES  = os.environ.get("ASDA_FXSPIN_RES", "1600x900")
PHASES = int(os.environ.get("ASDA_FXSPIN_PHASES", "4"))
PHASE_DT = float(os.environ.get("ASDA_FXSPIN_PHASE_DT", "1.0"))
SSDIRS = [r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots/Windows",
          r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots/WindowsEditor",
          r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"]

# ярлык актора → (дистанция, подъём камеры, азимут)
TARGETS = [("MapEffect_eff_mdel_obsdoor02-a_00_03", 1400.0, 400.0, 35.0)]
_T = os.environ.get("ASDA_FXSPIN_TARGETS", "")
if _T:
    TARGETS = []
    for _it in _T.split("|"):
        _p = _it.split(":")
        TARGETS.append((_p[0], float(_p[1]), float(_p[2]), float(_p[3])))


def main():
    import unreal
    R = {"тег": TAG, "кадры": {}, "центроиды": {}, "заметки": []}
    st = {"n": 0, "t": 0.0, "w": None, "q": [], "cursor": 0.0, "shot_t0": 0.0}
    rx, ry = [int(v) for v in RES.lower().split("x")]
    if not os.path.isdir(OUTD):
        os.makedirs(OUTD)

    def log(m): unreal.log("[fxspin] " + str(m))

    def cc(c):
        unreal.SystemLibrary.execute_console_command(st["w"], c)

    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    run_queue = PH.make_time_queue_runner(st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 5 стр.)

    newest_png = PH.make_newest_png(SSDIRS)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 7 стр.)

    find_actor = PH.make_find_effect_actor(st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 4 стр.)

    def dump_sections(lbl, ph):
        """ЧИСЛА: мировой центроид секции каждого эмиттера — тем же путём, каким рисуется игроку."""
        act = find_actor(lbl)
        if act is None:
            R["заметки"].append("НЕТ АКТОРА: " + lbl); return
        loc = act.get_actor_location()
        # маркер фазы в лог — по нему разбираются строки `[секции]` дальше (EmitterSectionStats
        # не UFUNCTION, из питона недоступна; ЧИСЛА печатает сама консольная команда)
        log("ФАЗА %d ЦЕЛЬ %s АКТОР %.1f %.1f %.1f" % (ph, lbl, loc.x, loc.y, loc.z))
        cc("asda.Fx.SectionDump %s" % lbl.replace("MapEffect_", "").rsplit("_", 1)[0])
        R["центроиды"].setdefault(lbl, {})["актор"] = [round(loc.x, 1), round(loc.y, 1), round(loc.z, 1)]

    def set_pivot(lbl, val):
        """Переключить `strict_quad_pivot` у эмиттеров актора (UE отдаёт КОПИИ структур).
           Нужно, чтобы снять кадр «как было» на УЖЕ перепечённой карте: рантайм читает флаг
           только в квад-ветке, прочим эмиттерам он безразличен."""
        act = find_actor(lbl)
        if act is None: return
        ems = act.get_editor_property("emitters")
        for i in range(len(ems)):
            e = ems[i]; e.set_editor_property("strict_quad_pivot", bool(val)); ems[i] = e
        act.set_editor_property("emitters", ems)
        log("strict_quad_pivot=%s у %s (%d эм.)" % (val, lbl, len(ems)))

    def shoot():
        st["shot_t0"] = time.time() - 1.0
        cc("HighResShot %dx%d" % (rx, ry))

    def collect(nm):
        src = newest_png(st["shot_t0"])
        if not src:
            R["заметки"].append("КАДР НЕ ПОЯВИЛСЯ: " + nm); log("КАДР НЕ ПОЯВИЛСЯ: " + nm); return
        dst = os.path.join(OUTD, "%s_%s.png" % (nm, TAG))
        shutil.copy(src, dst); R["кадры"][nm] = dst; log("СОХРАНЁН -> " + dst)

    def cam_for(lbl, dist, up, yaw):
        a = find_actor(lbl)
        if a is None: return None
        c = a.get_actor_location()
        px = c.x - dist * math.cos(math.radians(yaw))
        py = c.y - dist * math.sin(math.radians(yaw))
        pz = c.z + up
        pitch = -math.degrees(math.atan2(up, dist))
        return "%.0f %.0f %.0f %.1f %.1f" % (px, py, pz, pitch, yaw)

    def step(lbl, dist, up, yaw):
        t0 = st["cursor"]
        nm = lbl.replace("MapEffect_", "")
        def go():
            cam = cam_for(lbl, dist, up, yaw)
            if cam is None:
                R["заметки"].append("НЕТ АКТОРА (камера): " + lbl); return
            cc("BugItGo %s 0" % cam)
        st["q"].append([t0, go])
        _sp = os.environ.get("ASDA_FXSPIN_SETPIVOT", "")
        if _sp in ("0", "1"):
            st["q"].append([t0 + 0.2, (lambda l=lbl, v=(_sp == "1"): set_pivot(l, v))])
        t = t0 + 2.0
        for ph in range(PHASES):
            st["q"].append([t, (lambda l=lbl, p=ph: dump_sections(l, p))])
            st["q"].append([t + 0.05, shoot])
            st["q"].append([t + 1.5, (lambda n=nm, p=ph: collect("%s_ф%d" % (n, p)))])
            t += 1.5 + PHASE_DT
        st["cursor"] = t + 1.0

    def phase_init():
        w = None
        for o in unreal.ObjectIterator(unreal.World):
            if o.get_name() not in ("Untitled", "None"): w = o
        st["w"] = w
        log("мир: %s" % (w.get_name() if w else None))
        cc("t.MaxFPS 60"); cc("r.VSync 0")
        if os.environ.get("ASDA_FXSPIN_SUN", "1") == "1":
            dim = float(os.environ.get("ASDA_FXSPIN_DIM", "0.12"))
            killed = {}
            for cn in ("DirectionalLight", "SkyLight"):
                cls = getattr(unreal, cn, None)
                if cls is None: continue
                k = 0
                for a in unreal.GameplayStatics.get_all_actors_of_class(st["w"], cls):
                    try:
                        lc = a.light_component; lc.set_intensity(lc.intensity * dim); k += 1
                    except Exception: pass
                killed[cn] = k
            for cn in ("SkyAtmosphere", "VolumetricCloud"):
                cls = getattr(unreal, cn, None)
                if cls is None: continue
                k = 0
                for a in unreal.GameplayStatics.get_all_actors_of_class(st["w"], cls):
                    try: a.set_actor_hidden_in_game(True); k += 1
                    except Exception: pass
                killed[cn] = k
            R["погашено"] = killed
        st["cursor"] = st["t"] + 2.0
        for lbl, d, u, y in TARGETS: step(lbl, d, u, y)
        st["q"].append([st["cursor"] + 1.0, finish])

    def finish():
        p = os.path.join(OUTD, "spin_%s_%s.json" % (os.environ.get("ASDA_FXSPIN_MAPTAG", "map"), TAG))
        with io.open(p, "w", encoding="utf-8") as fh:
            json.dump(R, fh, ensure_ascii=False, indent=1)
        log("записан " + p)
        log("FXSPIN ЗАВЕРШЕНО")
        st["q"].append([st["t"] + 2.0, lambda: cc("quit")])

    def tick(dt):
        st["n"] += 1; st["t"] += float(dt)
        if st["n"] == 1: phase_init(); return
        if st["w"] is None: return
        try: run_queue()
        except Exception as ex:
            import traceback
            unreal.log_error("[fxspin] ошибка шага: %s\n%s" % (ex, traceback.format_exc()))

    unreal.register_slate_post_tick_callback(tick)
    log("старт съёмки ВРАЩЕНИЯ, тег %s" % TAG)


# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
