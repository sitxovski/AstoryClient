# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_glow_shot.py — ВИЗУАЛЬНАЯ ПРИЁМКА категории Effect Glow: правка полей меняет картинку СРАЗУ.
#
#   Лестница кадров на карте (солнце/небо/туман погашены — иначе свет эффекта тонет в дневном свете):
#     A_base    — как есть (числа ассета)
#     B_color   — правка ЦВЕТА и СИЛЫ СВЕТА через `asda.Fx.Glow.Set` (путь панели деталей)
#     C_boost   — плюс правка СИЛЫ СВЕЧЕНИЯ (эмиссия материала)
#     D_back    — возврат всех трёх полей к исходным значениям
#   ★ КОНТРОЛЬНАЯ ПАРА A/D: сцена анимирована, и разница двух кадров при ОДНОМ состоянии есть ШУМ
#     метрики (закон pixel-metric-needs-control-pairs). Без неё «стало ярче» ничего не значит.
#   ★ Кадр снимается `unreal.AutomationLibrary.take_high_res_screenshot()` — требование владельца:
#     консольный HighResShot отдаёт окно Slate, а нужен кадр вьюпорта.
#
#   Запуск: python Tools/set0_run_game.py Tools/asda_fx_glow_shot.py --map /Game/Map/alpen/alpen
#           --marker "GLOW-SHOT ЗАВЕРШЕНО" --env "ASDA_GLOW_FILTER=eff_mdel_soulidol"
import os, io, json, glob, shutil, time

OUTD = os.environ.get("ASDA_GLOW_OUTD", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/fx_glowui")
KEY = os.environ.get("ASDA_GLOW_KEY", "alpen")
FILTER = os.environ.get("ASDA_GLOW_FILTER", "eff_mdel_soulidol")
CAM = os.environ.get("ASDA_GLOW_CAM", "-30700 5754 1669 -19.7 0")
RES = os.environ.get("ASDA_GLOW_RES", "1280x720")
# Исходные числа эффекта (для возврата) — печатаются в лог на шаге A и подставляются сюда же.
BACK_CD = float(os.environ.get("ASDA_GLOW_BACK_CD", "7.68"))
# Чем снимать: "console" = HighResShot (в -game отдаёт КАДР ВЬЮПОРТА и реально пишет файл),
# "automation" = unreal.AutomationLibrary.take_high_res_screenshot (нужен в РЕДАКТОРЕ, где HighResShot
# отдаёт окно Slate). ★ ЗАМЕР 10.09.2026: в сессии `-game -unattended` automation-вариант НЕ ПИШЕТ
# НИЧЕГО и не ругается — четыре «разных» кадра оказались одним и тем же старым файлом.
SHOTAPI = os.environ.get("ASDA_GLOW_SHOTAPI", "console")
SSDIRS = [r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots/Windows",
          r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots/WindowsEditor",
          r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"]


def main():
    import unreal
    R = {"ключ": KEY, "камера": CAM, "фильтр": FILTER, "кадры": {}, "заметки": []}
    st = {"n": 0, "t": 0.0, "w": None, "q": [], "cursor": 0.0}
    rx, ry = [int(v) for v in RES.lower().split("x")]

    def log(m):
        unreal.log("[glowshot] " + str(m))

    def cc(c):
        unreal.SystemLibrary.execute_console_command(st["w"], c)
        log("cmd: " + c)

    def after(secs, fn):
        st["q"].append([st["t"] + float(secs), fn])

    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    run_queue = PH.make_time_queue_runner(st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 7 стр.)

    # ★★★ ГРАБЛЯ 10.09.2026: «самый свежий png» без порога времени берёт ВЧЕРАШНИЙ файл, если
    #   сегодняшний не появился, и лестница кадров молча вырождается в четыре копии одного снимка
    #   (проверка подтверждает несуществующее). Порог — момент старта скрипта.
    newest_png = PH.make_newest_png(SSDIRS)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 8 стр.)

    def shoot(nm):
        st["shot_t0"] = time.time() - 1.0
        if SHOTAPI == "automation":
            # ★ С КАМЕРОЙ-АКТОРОМ: без неё automation-вариант в `-game` не пишет ничего и не ругается.
            cam = st.get("cam")
            if cam is None:
                cam = unreal.GameplayStatics.get_player_camera_manager(st["w"], 0)
                st["cam"] = cam
            unreal.AutomationLibrary.take_high_res_screenshot(rx, ry, "glow_%s_%s" % (KEY, nm), camera=cam)
        else:
            cc("HighResShot %dx%d" % (rx, ry))
        log("запрошен кадр %s (%s)" % (nm, SHOTAPI))

    def collect(nm):
        src = newest_png(st.get("shot_t0", 0.0))
        if not src:
            R["заметки"].append("КАДР НЕ ПОЯВИЛСЯ: " + nm)
            log("КАДР НЕ ПОЯВИЛСЯ: " + nm)
            return
        dst = os.path.join(OUTD, "glow_%s_%s.png" % (KEY, nm))
        shutil.copy(src, dst)
        R["кадры"][nm] = dst
        log("СОХРАНЁН -> " + dst)

    def phase_init():
        w = None
        for o in unreal.ObjectIterator(unreal.World):
            if o.get_name() not in ("Untitled", "None"):
                w = o
        st["w"] = w
        log("мир: %s" % (w.get_name() if w else None))
        cc("t.MaxFPS 60")
        cc("r.VSync 0")
        after(1.0, phase_sun)

    def phase_sun():
        killed = {}
        for cn in ("DirectionalLight", "SkyLight", "SkyAtmosphere", "ExponentialHeightFog"):
            cls = getattr(unreal, cn, None)
            if cls is None:
                continue
            n = 0
            for a in unreal.GameplayStatics.get_all_actors_of_class(st["w"], cls):
                try:
                    a.set_actor_hidden_in_game(True)
                    n += 1
                except Exception:
                    pass
            killed[cn] = n
        R["погашено"] = killed
        log("ПОГАШЕНО: %s" % killed)
        after(0.5, phase_cam)

    def phase_cam():
        cc("BugItGo %s 0" % CAM)
        cc("asda.Fx.Light.RadiusCap -1")     # уровневый потолок — как у владельца
        after(3.0, ladder)

    # ★ ШАГИ ИДУТ ПО АБСОЛЮТНОМУ ВРЕМЕНИ, А НЕ «каждый через 0 секунд от сейчас»: очередь снимает
    #   по одному пункту за кадр, и четыре шага с одним и тем же сроком отработали бы за четыре кадра,
    #   то есть за 60 мс — кадр снялся бы раньше, чем свет успел перестроиться.
    def step(nm, cmds, settle=2.5):
        t0 = st["cursor"]
        st["q"].append([t0, lambda: [cc(c) for c in cmds]])
        st["q"].append([t0 + settle, lambda: shoot(nm)])
        st["q"].append([t0 + settle + 3.0, lambda: collect(nm)])
        st["cursor"] = t0 + settle + 4.0

    def ladder():
        st["cursor"] = st["t"] + 1.0
        step("A_base", [])
        step("B_color", ["asda.Fx.Glow.Set %s color 20 255 40" % FILTER,
                         "asda.Fx.Glow.Set %s intensity 40" % FILTER])
        step("C_boost", ["asda.Fx.Glow.Set %s override 1" % FILTER,
                         "asda.Fx.Glow.Set %s boost 8" % FILTER])
        step("D_back", ["asda.Fx.Glow.Set %s override 0" % FILTER,
                        "asda.Fx.Glow.Set %s colormode 2" % FILTER,
                        "asda.Fx.Glow.Set %s intensity %.2f" % (FILTER, BACK_CD)])
        st["q"].append([st["cursor"] + 1.0, finish])

    def finish():
        p = os.path.join(OUTD, "glow_%s.json" % KEY)
        with io.open(p, "w", encoding="utf-8") as fh:
            json.dump(R, fh, ensure_ascii=False, indent=1)
        log("записан " + p)
        log("GLOW-SHOT ЗАВЕРШЕНО")
        after(2.0, lambda: cc("quit"))

    def tick(dt):
        st["n"] += 1
        st["t"] += float(dt)
        if st["n"] == 1:
            phase_init()
            return
        if st["w"] is None:
            return
        try:
            run_queue()
        except Exception as ex:
            import traceback
            unreal.log_error("[glowshot] ошибка шага: %s\n%s" % (ex, traceback.format_exc()))

    unreal.register_slate_post_tick_callback(tick)
    log("старт съёмки Effect Glow, ключ %s, камера %s, фильтр %s" % (KEY, CAM, FILTER))


# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
