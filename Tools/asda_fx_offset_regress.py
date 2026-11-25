# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_offset_regress.py — ДЫМОВАЯ ПРОВЕРКА СМЕЖНОГО после фикса смещения колец 0xE.
#
#   Живым путём игрока (-game) поднимает стенд эффектов навыков (`asda.Fx.Light.Stand`) и печатает
#   геометрию его секций (`asda.Fx.SectionDump`) — если бы правка задела ОБЩИЙ путь колец, у
#   кольцевых эмиттеров стенда центроид/радиус поехал бы или секция пропала.
#   Плюс кадр стенда. Ничего не сохраняет.
#   Запуск: python Tools/set0_run_game.py Tools/asda_fx_offset_regress.py --map <карта>
#           --marker "РЕГРЕСС ЗАВЕРШЕНО" --env "ASDA_FXREG_JEB=<база .jeb>"
import os, glob, shutil, time

JEB  = os.environ.get("ASDA_FXREG_JEB", "eff_1_char_02_blsbuff01-a_00")
OUTD = os.environ.get("ASDA_FXREG_OUTD", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/fx_offset")
TAG  = os.environ.get("ASDA_FXREG_TAG", "STAND")
SSDIRS = [r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots/Windows",
          r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots/WindowsEditor",
          r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"]


def main():
    import unreal
    st = {"n": 0, "t": 0.0, "w": None, "q": [], "t0": 0.0}

    def log(m): unreal.log("[fxreg] " + str(m))
    def cc(c):
        unreal.SystemLibrary.execute_console_command(st["w"], c); log("cmd: " + c)
    def at(sec, fn): st["q"].append([st["t"] + float(sec), fn])
    def run_queue():
        if st["q"] and st["t"] >= st["q"][0][0]:
            st["q"].pop(0)[1]()

    def collect():
        best, bt = None, -1
        for d in SSDIRS:
            for p in glob.glob(d + "/*.png"):
                mt = os.path.getmtime(p)
                if mt > bt and mt > st["t0"]: best, bt = p, mt
        if best:
            dst = os.path.join(OUTD, "stand_%s_%s.png" % (JEB, TAG))
            shutil.copy(best, dst); log("СОХРАНЁН -> " + dst)
        else:
            log("КАДР НЕ ПОЯВИЛСЯ")

    def start():
        w = None
        for o in unreal.ObjectIterator(unreal.World):
            if o.get_name() not in ("Untitled", "None"): w = o
        st["w"] = w
        log("мир: %s" % (w.get_name() if w else None))
        cc("t.MaxFPS 60")
        at(1.0, lambda: cc("asda.Fx.Light.Stand %s 700 150" % JEB))
        at(4.0, lambda: cc("asda.Fx.SectionDump %s" % JEB))
        at(4.5, shoot)
        at(8.0, collect)
        at(9.0, done)

    def shoot():
        st["t0"] = time.time() - 1.0
        cc("HighResShot 1280x720")

    def done():
        log("РЕГРЕСС ЗАВЕРШЕНО")
        at(2.0, lambda: cc("quit"))

    def tick(dt):
        st["n"] += 1; st["t"] += float(dt)
        if st["n"] == 1: start(); return
        if st["w"] is None: return
        try: run_queue()
        except Exception as ex:
            import traceback
            unreal.log_error("[fxreg] ошибка: %s\n%s" % (ex, traceback.format_exc()))

    unreal.register_slate_post_tick_callback(tick)
    log("старт дымовой проверки стенда, .jeb=%s" % JEB)


# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
