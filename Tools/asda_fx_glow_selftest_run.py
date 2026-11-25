# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_glow_selftest_run.py — гоняет `asda.Fx.Glow.SelfTest` ПУТЁМ ИГРОКА (-game, не PIE).
#   Сам сценарий живёт в C++ (AsdaEffectActor.cpp): правки полей идут ровно тем же событием, что
#   строит панель деталей, а между шагами нужны РЕАЛЬНЫЕ КАДРЫ (секции пересобираются в Tick).
#   Запуск: python Tools/set0_run_game.py Tools/asda_fx_glow_selftest_run.py --map /Game/Map/alpen/alpen
#           --marker "GLOW-RUN ЗАВЕРШЕНО" --env "ASDA_GLOW_FILTER=eff_mdel_soulidol"
import os

FILTER = os.environ.get("ASDA_GLOW_FILTER", "eff_mdel_soulidol")
# Камера «x y z pitch yaw» (пусто = не двигать). Дефолт — кадр столбов alpen волны Ф4.
CAM = os.environ.get("ASDA_GLOW_CAM", "-30700 5754 1669 -19.7 0")


def main():
    import unreal
    st = {"n": 0, "w": None, "q": [], "t": 0.0, "wait": -1.0}

    def log(m):
        unreal.log("[glowrun] " + str(m))

    def cc(c):
        unreal.SystemLibrary.execute_console_command(st["w"], c)
        log("cmd: " + c)

    def after(fr, fn):
        st["q"].append([fr, fn])

    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    run_queue = PH.make_frame_queue_runner(st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 8 стр.)

    def phase_init():
        w = None
        for o in unreal.ObjectIterator(unreal.World):
            if o.get_name() not in ("Untitled", "None"):
                w = o
        st["w"] = w
        log("мир: %s" % (w.get_name() if w else None))
        cc("t.MaxFPS 0")
        after(60, phase_run)

    def phase_run():
        cc("asda.Fx.Light.RadiusCap -1")          # уровневый потолок — как у владельца
        # ★ КАМЕРУ — К ЭФФЕКТУ. Дальше CullDistance секции процедурного меша ВЫЧИЩЕНЫ, и дамп
        #   материалов честно печатает «секций 0»: проверять «что стоит на секции» издалека нечего.
        #   Камера — та же, что у волны Ф4 на столбах alpen (сравнимость кадров).
        if CAM:
            cc("BugItGo %s 0" % CAM)
        after(120, phase_test)

    def phase_test():
        cc("asda.Fx.Glow.SelfTest %s" % FILTER)
        # ★ ЖДЁМ ПО ВРЕМЕНИ, А НЕ ПО КАДРАМ. Сценарий в C++ разложен на ТАЙМЕРЫ МИРА (последний в 9.8 с),
        #   а кадры при t.MaxFPS 0 идут по 3 мс — 900 кадров это 3 секунды, и прошлый прогон выходил
        #   РАНЬШЕ первого же шага. Классическая ложная «тишина в логе».
        st["wait"] = st["t"] + 20.0

    def phase_done():
        log("GLOW-RUN ЗАВЕРШЕНО")
        after(30, lambda: cc("quit"))

    def tick(dt):
        st["n"] += 1
        st["t"] += float(dt)
        if st["wait"] > 0.0 and st["t"] >= st["wait"]:
            st["wait"] = -1.0
            phase_done()
            return
        if st["n"] == 1:
            phase_init()
            return
        if st["w"] is None:
            return
        try:
            run_queue()
        except Exception as ex:
            import traceback
            unreal.log_error("[glowrun] ошибка шага: %s\n%s" % (ex, traceback.format_exc()))

    unreal.register_slate_post_tick_callback(tick)
    log("старт самопроверки Effect Glow, фильтр «%s»" % FILTER)


# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
