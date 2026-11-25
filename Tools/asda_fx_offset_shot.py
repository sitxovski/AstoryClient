# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_offset_shot.py — кадры «до/после» для задачи о СМЕЩЕНИИ ЭМИТТЕРОВ эффектов (0xE).
#
#   Снимает три эффекта alpen с фиксированных камер и, ПОМИМО картинки, пишет ЧИСЛА:
#   мировой центроид каждой секции ProceduralMesh (по эмиттеру) — это и есть «где эмиттер
#   оказался», сравнимое между прогонами без глазомера.
#
#   ★ Кадр снимается КОНСОЛЬНЫМ `HighResShot` (закон: `AutomationLibrary.take_high_res_screenshot`
#     в `-game -RenderOffscreen -unattended` файл НЕ создаёт и об этом не сообщает).
#   ★ Свежесть кадра проверяется по времени модификации (порог = момент запроса).
#
#   Запуск:
#     python Tools/set0_run_game.py Tools/asda_fx_offset_shot.py --map /Game/Map/alpen/alpen \
#            --marker "FXOFFSET ЗАВЕРШЕНО" --env "ASDA_FXOFF_TAG=BEFORE"
import os, io, json, glob, shutil, time

TAG  = os.environ.get("ASDA_FXOFF_TAG", "BEFORE")
OUTD = os.environ.get("ASDA_FXOFF_OUTD", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/fx_offset")
RES  = os.environ.get("ASDA_FXOFF_RES", "1600x900")
SSDIRS = [r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots/Windows",
          r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots/WindowsEditor",
          r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"]

# ярлык актора → (дистанция по горизонтали, подъём камеры над центром)
TARGETS = [
    ("MapEffect_eff_noeaplhentower00_a_00_01", 1100.0, 260.0, 150.0),
    ("MapEffect_eff_mdel_soulidolbase-a_00_01", 2200.0, 700.0, 35.0),
    ("MapEffect_eff_neoalpenshop_a_00_01",      1200.0, 250.0, 35.0),
]
# ★ Список целей задаётся снаружи БЕЗ КАВЫЧЕК (PowerShell съедает их в -ExecCmds/-env):
#   ASDA_FXOFF_TARGETS=ярлык:дист:подъём:азимут|ярлык:дист:подъём:азимут
_T = os.environ.get("ASDA_FXOFF_TARGETS", "")
if _T:
    TARGETS = []
    for _it in _T.split("|"):
        _p = _it.split(":")
        TARGETS.append((_p[0], float(_p[1]), float(_p[2]), float(_p[3])))


def main():
    import unreal
    R = {"тег": TAG, "кадры": {}, "секции": {}, "заметки": []}
    st = {"n": 0, "t": 0.0, "w": None, "q": [], "cursor": 0.0}
    rx, ry = [int(v) for v in RES.lower().split("x")]
    if not os.path.isdir(OUTD):
        os.makedirs(OUTD)

    def log(m): unreal.log("[fxoff] " + str(m))

    def cc(c):
        unreal.SystemLibrary.execute_console_command(st["w"], c)
        log("cmd: " + c)

    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    run_queue = PH.make_time_queue_runner(st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 5 стр.)

    newest_png = PH.make_newest_png(SSDIRS)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 7 стр.)

    find_actor = PH.make_find_effect_actor(st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 4 стр.)

    def dump_sections(lbl):
        """ЧИСЛА, а не глазомер: мировой центроид/радиус секции каждого эмиттера.
           Считает C++ (`asda.Fx.SectionDump`) по ЖИВЫМ акторам уровня — тот же путь,
           которым эффект рисуется игроку; результат остаётся в логе прогона."""
        act = find_actor(lbl)
        if act is None:
            R["заметки"].append("НЕТ АКТОРА: " + lbl); return
        # имя .jeb выводим из ярлыка (MapEffect_<jeb>_NN): в `-game` свойство effect_name
        # через get_editor_property недоступно, а фильтр команды и так по подстроке.
        cc("asda.Fx.SectionDump %s" % lbl.replace("MapEffect_", "").rsplit("_", 1)[0])

    def set_no_chiral(lbl, val):
        """Переключить `ring_no_chiral` у кольцевых эмиттеров актора (UE отдаёт КОПИИ структур)."""
        act = find_actor(lbl)
        if act is None: return
        # ★ `jeb_type` в `-game` через get_editor_property недоступен — ставим флаг ВСЕМ эмиттерам:
        #   рантайм читает его ТОЛЬКО в кольцевой ветке (0xE), для квадов/частиц он безразличен.
        ems = act.get_editor_property("emitters")
        for i in range(len(ems)):
            e = ems[i]
            e.set_editor_property("ring_no_chiral", bool(val)); ems[i] = e
        act.set_editor_property("emitters", ems)
        log("ring_no_chiral=%s у колец %s" % (val, lbl))

    def shoot(nm):
        st["shot_t0"] = time.time() - 1.0
        cc("HighResShot %dx%d" % (rx, ry))

    def collect(nm):
        src = newest_png(st.get("shot_t0", 0.0))
        if not src:
            R["заметки"].append("КАДР НЕ ПОЯВИЛСЯ: " + nm); log("КАДР НЕ ПОЯВИЛСЯ: " + nm); return
        dst = os.path.join(OUTD, "%s_%s.png" % (nm, TAG))
        shutil.copy(src, dst); R["кадры"][nm] = dst; log("СОХРАНЁН -> " + dst)

    def cam_for(lbl, dist, up, yaw=35.0):
        a = find_actor(lbl)
        if a is None: return None
        c = a.get_actor_location()
        # камера с юго-запада, взгляд на центр
        import math
        px = c.x - dist * math.cos(math.radians(yaw))
        py = c.y - dist * math.sin(math.radians(yaw))
        pz = c.z + up
        pitch = -math.degrees(math.atan2(up, dist))
        return "%.0f %.0f %.0f %.1f %.1f" % (px, py, pz, pitch, yaw)

    def step(lbl, dist, up, yaw=35.0):
        t0 = st["cursor"]
        nm = lbl.replace("MapEffect_", "")
        def go():
            cam = cam_for(lbl, dist, up, yaw)
            if cam is None:
                R["заметки"].append("НЕТ АКТОРА (камера): " + lbl); return
            cc("BugItGo %s 0" % cam)
        st["q"].append([t0, go])
        st["q"].append([t0 + 2.5, lambda: dump_sections(lbl)])
        st["q"].append([t0 + 3.0, lambda: shoot(nm)])
        st["q"].append([t0 + 6.0, lambda: collect(nm)])
        st["cursor"] = t0 + 7.0

    def phase_init():
        w = None
        for o in unreal.ObjectIterator(unreal.World):
            if o.get_name() not in ("Untitled", "None"): w = o
        st["w"] = w
        log("мир: %s" % (w.get_name() if w else None))
        cc("t.MaxFPS 60"); cc("r.VSync 0")
        # ★ Аддитивный эффект в ДНЕВНОМ свете тонет — гасим солнце/небо/туман (как в asda_fx_glow_shot).
        #   Это ТОЛЬКО режим съёмки: скрываем акторы освещения на время прогона, уровень не сохраняется.
        if os.environ.get("ASDA_FXOFF_SUN", "1") == "1":
            killed = {}
            dim = float(os.environ.get("ASDA_FXOFF_DIM", "0.12"))
            for cn in ("DirectionalLight", "SkyLight"):
                cls = getattr(unreal, cn, None)
                if cls is None: continue
                k = 0
                for a in unreal.GameplayStatics.get_all_actors_of_class(st["w"], cls):
                    try:
                        lc = a.light_component
                        lc.set_intensity(lc.intensity * dim); k += 1
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
            log("ПОГАШЕНО: %s" % killed)
        st["cursor"] = st["t"] + 2.0
        if os.environ.get("ASDA_FXOFF_ONLY_PAIR", "") != "1":
            for lbl, d, u, y in TARGETS: step(lbl, d, u, y)
        # ★ КОНТРОЛЬНЫЙ ВАРИАНТ башни: тот же кадр с ВЫКЛЮЧЕННОЙ отменой хиральности
        #   (= только фикс контракта pos-канала) — чтобы владелец видел ОБА и выбрал.
        if os.environ.get("ASDA_FXOFF_CHIRAL_PAIR", "") == "1":
            t0 = st["cursor"]
            L, LD, LU, LY = TARGETS[0]
            st["q"].append([t0, lambda: set_no_chiral(L, False)])
            st["q"].append([t0 + 0.5, lambda: cc("BugItGo %s 0" % cam_for(L, LD, LU, LY))])
            st["q"].append([t0 + 2.5, lambda: dump_sections(L)])
            st["q"].append([t0 + 3.0, lambda: shoot("eff_noeaplhentower00_a_00_01_KEEPCHIRAL")])
            st["q"].append([t0 + 6.0, lambda: collect("eff_noeaplhentower00_a_00_01_KEEPCHIRAL")])
            st["cursor"] = t0 + 7.0
        st["q"].append([st["cursor"] + 1.0, finish])

    def finish():
        p = os.path.join(OUTD, "sections_%s.json" % TAG)
        with io.open(p, "w", encoding="utf-8") as fh:
            json.dump(R, fh, ensure_ascii=False, indent=1)
        log("записан " + p)
        log("FXOFFSET ЗАВЕРШЕНО")
        st["q"].append([st["t"] + 2.0, lambda: cc("quit")])

    def tick(dt):
        st["n"] += 1; st["t"] += float(dt)
        if st["n"] == 1: phase_init(); return
        if st["w"] is None: return
        try: run_queue()
        except Exception as ex:
            import traceback
            unreal.log_error("[fxoff] ошибка шага: %s\n%s" % (ex, traceback.format_exc()))

    unreal.register_slate_post_tick_callback(tick)
    log("старт съёмки СМЕЩЕНИЯ ЭМИТТЕРОВ, тег %s" % TAG)


# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
