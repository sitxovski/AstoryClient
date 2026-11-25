# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_glow_verify.py — ★ 13.09.2026: ЗАМЕР «КОНФИГУРАТОР СВЕЧЕНИЯ МЕНЯЕТ КАРТИНКУ В ИГРЕ» путём игрока (-game).
#
#   Поднимает стенд asda.Fx.Light.Stand <.jeb> item=<код> и снимает серии кадров по фазам:
#     ov0_a, ov0_b  — asda.Fx.Glow.Overrides 0 (как до конфигуратора): КОНТРОЛЬНАЯ ПАРА (шум анимации эффекта);
#     ov1_item      — ключ 1, эффект со своим предметом: действует переопределение «предмет + эффект»;
#     ov1_noitem    — ключ 1, тот же эффект БЕЗ предмета: переопределение предмета действовать НЕ должно;
#     ov0_c         — снова 0: возврат к исходному (откат работает на живой сцене).
#   На каждой фазе печатает строку стенда «[стенд] свечение: …» (действующие числа) — её ищет разбор.
#   Кадры: HighResShot (закон из asda_fx_skill_shot.py — AutomationLibrary в -game файлов не создаёт).
#
#   Запуск:
#     python Tools/set0_run_game.py Tools/asda_fx_glow_verify.py --map /Game/Map/nightvalley/nightvalley \
#            --marker "GLOWVERIFY ЗАВЕРШЕНО" --env "ASDA_GV_JEB=ohs_a_co_ma_03" --env "ASDA_GV_ITEM=20600"
#   Закон tools-no-import-side-effects: работает только как скрипт -ExecCmds (main под guard).
import os, glob, shutil, time, json

JEB    = os.environ.get("ASDA_GV_JEB", "ohs_a_co_ma_03")
ITEM   = int(os.environ.get("ASDA_GV_ITEM", "20600"))
OUTD   = os.environ.get("ASDA_GV_OUTD", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/fx_glow_configurator")
RES    = os.environ.get("ASDA_GV_RES", "1280x720")
DIST   = os.environ.get("ASDA_GV_DIST", "500")
DROP   = os.environ.get("ASDA_GV_DROP", "120")
FRAMES = int(os.environ.get("ASDA_GV_FRAMES", "4"))
SSDIRS = [r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots/Windows",
          r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots/WindowsEditor",
          r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"]
PHASES = [("ov0_a", 0, ITEM), ("ov0_b", 0, ITEM), ("ov1_item", 1, ITEM), ("ov1_noitem", 1, 0), ("ov0_c", 0, ITEM)]


def main():
    import unreal
    st = {"n": 0, "t": 0.0, "w": None, "q": [], "t0": 0.0}
    R = {"jeb": JEB, "item": ITEM, "кадры": {}, "пропало": []}

    def log(m):
        unreal.log("[glowverify] " + str(m))

    def cc(c):
        unreal.SystemLibrary.execute_console_command(st["w"], c)
        log("cmd: " + c)

    def at(sec, fn):
        st["q"].append([st["t"] + float(sec), fn])
        st["q"].sort(key=lambda e: e[0])

    def run_queue():
        while st["q"] and st["t"] >= st["q"][0][0]:
            st["q"].pop(0)[1]()

    def shoot():
        st["t0"] = time.time() - 1.0
        cc("HighResShot " + RES)

    def collect(name):
        best, bt = None, -1
        for d in SSDIRS:
            for p in glob.glob(d + "/*.png"):
                mt = os.path.getmtime(p)
                if mt > bt and mt > st["t0"]:
                    best, bt = p, mt
        if best:
            dst = os.path.join(OUTD, "%s.png" % name)
            shutil.copy(best, dst)
            R["кадры"][name] = dst
            log("СОХРАНЁН -> " + dst)
        else:
            R["пропало"].append(name)
            log("КАДР НЕ ПОЯВИЛСЯ " + name)

    def program():
        t = 1.0
        for ph, ov, item in PHASES:
            at(t, (lambda v: (lambda: cc("asda.Fx.Glow.Overrides %d" % v)))(ov))
            at(t + 0.3, (lambda it: (lambda: cc("asda.Fx.Light.Stand %s %s %s item=%d" % (JEB, DIST, DROP, it))))(item))
            t += 2.5
            for k in range(FRAMES):
                name = "%s_%s_f%d" % (JEB, ph, k)
                at(t, shoot)
                at(t + 1.1, (lambda nm: (lambda: collect(nm)))(name))
                t += 1.4
            at(t, lambda: cc("asda.Fx.Light.StandClear"))
            t += 0.5
        at(t + 0.5, done)

    def done():
        try:
            with open(os.path.join(OUTD, "shots_%s.json" % JEB), "w", encoding="utf-8") as f:
                json.dump(R, f, ensure_ascii=False, indent=1)
        except Exception as ex:
            log("JSON не записан: %s" % ex)
        log("GLOWVERIFY ЗАВЕРШЕНО")
        at(2.0, lambda: cc("quit"))

    def start():
        w = None
        for o in unreal.ObjectIterator(unreal.World):
            if o.get_name() not in ("Untitled", "None"):
                w = o
        st["w"] = w
        log("мир: %s | эффект %s | предмет %d" % (w.get_name() if w else None, JEB, ITEM))
        if not os.path.isdir(OUTD):
            os.makedirs(OUTD)
        cc("t.MaxFPS 60")
        program()

    def tick(dt):
        st["n"] += 1
        st["t"] += float(dt)
        if st["n"] == 1:
            start()
            return
        if st["w"] is None:
            return
        try:
            run_queue()
        except Exception as ex:
            log("ОШИБКА: %s" % ex)

    unreal.register_slate_post_tick_callback(tick)


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
