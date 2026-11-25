# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_skill_shot.py — КАДРЫ И ЧИСЛА ЭФФЕКТОВ НАВЫКОВ НА СТЕНДЕ (задача «эффекты навыков не как в игре»).
#
#   Живым путём игрока (-game) поднимает стенд (`asda.Fx.Light.Stand <база .jeb>`) и снимает СЕРИЮ ФАЗ:
#   на каждой фазе печатает `asda.Fx.SectionDump` (мировой центроид/радиус секции каждого эмиттера) и
#   делает кадр. Серия обязательна: вращение и стык периода на ОДНОМ кадре не видны.
#
#   ★ Кадр снимается КОНСОЛЬНЫМ `HighResShot` — `AutomationLibrary.take_high_res_screenshot()` в
#     `-game -RenderOffscreen -unattended` файл НЕ создаёт и об этом молчит (сверяем время файла).
#
#   Запуск:
#     python Tools/set0_run_game.py Tools/asda_fx_skill_shot.py --map /Game/Map/alpen/alpen \
#            --marker "FXSKILL ЗАВЕРШЕНО" --env "ASDA_FXSKILL_TAG=BEFORE" \
#            --env "ASDA_FXSKILL_JEBS=eff_1_char_05_sspee01-a_00;eff_1_char_06_bigmeob03-a_00"
import os, glob, shutil, time, json

TAG    = os.environ.get("ASDA_FXSKILL_TAG", "BEFORE")
OUTD   = os.environ.get("ASDA_FXSKILL_OUTD", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/fx_skill")
RES    = os.environ.get("ASDA_FXSKILL_RES", "1280x720")
DIST   = os.environ.get("ASDA_FXSKILL_DIST", "900")
DROP   = os.environ.get("ASDA_FXSKILL_DROP", "150")
PHASES = int(os.environ.get("ASDA_FXSKILL_PHASES", "4"))
PH_DT  = float(os.environ.get("ASDA_FXSKILL_PHASE_DT", "0.6"))
DELAY  = float(os.environ.get("ASDA_FXSKILL_DELAY", "1.5"))   # от спавна до первой фазы, с (одноразовым эффектам — 0.1…0.3)
JEBS   = [x.strip() for x in os.environ.get(
    "ASDA_FXSKILL_JEBS", "eff_1_char_05_sspee01-a_00").split(";") if x.strip()]
SSDIRS = [r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots/Windows",
          r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots/WindowsEditor",
          r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"]


def main():
    import unreal
    st = {"n": 0, "t": 0.0, "w": None, "q": [], "t0": 0.0}
    R = {"тег": TAG, "кадры": {}, "пропало": []}

    def log(m):
        unreal.log("[fxskill] " + str(m))

    def cc(c):
        unreal.SystemLibrary.execute_console_command(st["w"], c)
        log("cmd: " + c)

    def at(sec, fn):
        st["q"].append([st["t"] + float(sec), fn])

    def run_queue():
        while st["q"] and st["t"] >= st["q"][0][0]:
            st["q"].pop(0)[1]()

    def shoot():
        st["t0"] = time.time() - 1.0
        cc("HighResShot " + RES)

    def collect(jeb, ph):
        best, bt = None, -1
        for d in SSDIRS:
            for p in glob.glob(d + "/*.png"):
                mt = os.path.getmtime(p)
                if mt > bt and mt > st["t0"]:
                    best, bt = p, mt
        if best:
            dst = os.path.join(OUTD, "stand_%s_ф%d_%s.png" % (jeb, ph, TAG))
            shutil.copy(best, dst)
            R["кадры"]["%s_ф%d" % (jeb, ph)] = dst
            log("СОХРАНЁН -> " + dst)
        else:
            R["пропало"].append("%s_ф%d" % (jeb, ph))
            log("КАДР НЕ ПОЯВИЛСЯ (%s ф%d)" % (jeb, ph))

    def program():
        t = 1.0
        for jeb in JEBS:
            at(t, (lambda j: (lambda: cc("asda.Fx.Light.Stand %s %s %s" % (j, DIST, DROP))))(jeb))
            t += DELAY
            for ph in range(PHASES):
                at(t, (lambda j: (lambda: cc("asda.Fx.SectionDump %s" % j)))(jeb))
                at(t + 0.05, shoot)
                at(t + 1.2, (lambda j, p: (lambda: collect(j, p)))(jeb, ph))
                t += 1.2 + PH_DT
            at(t, lambda: cc("asda.Fx.Light.StandClear"))
            t += 0.6
        at(t + 1.0, done)

    def done():
        try:
            with open(os.path.join(OUTD, "shots_%s.json" % TAG), "w", encoding="utf-8") as f:
                json.dump(R, f, ensure_ascii=False, indent=1)
        except Exception as ex:
            log("JSON не записан: %s" % ex)
        log("FXSKILL ЗАВЕРШЕНО")
        at(2.0, lambda: cc("quit"))

    def start():
        w = None
        for o in unreal.ObjectIterator(unreal.World):
            if o.get_name() not in ("Untitled", "None"):
                w = o
        st["w"] = w
        log("мир: %s | эффекты: %s | тег %s" % (w.get_name() if w else None, ", ".join(JEBS), TAG))
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
