# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_skillglow_shot.py — КАДРЫ ЯРКОСТИ ЭФФЕКТОВ НАВЫКОВ ПО ПРЕСЕТАМ (04.11.2026, жалоба владельца «эффекты навыков светятся
#   слишком ярко», StampBerserker на песке). [ПОРТ]
#
#   Путь игры (-game -RenderOffscreen, окна нет): на карте ставится стенд `asda.Fx.Light.Stand <jeb> <дист> <пол> plate=0` (эффект над
#   полом САМОЙ карты, без площадки), на каждом пресете качества (`scalability 0..4`) снимаются кадры эффекта в двух фазах и
#   контрольный кадр без эффекта; для каждого эффекта — ещё кадр с выключенным эффект-светом (`asda.Fx.Light 0`), чтобы отделить
#   свечение материала от освещения пола. Числа (доля пересвеченных пикселей, прирост яркости) считает asda_skillglow_metrics.py.
#
#   Запуск:
#     python Tools/set0_run_game.py Tools/asda_skillglow_shot.py --map /Game/Map/StagnantDesert/StagnantDesert \
#            --marker "SKILLGLOW ЗАВЕРШЕНО" --res 1280x720 --env "ASDA_SG_TAG=before" --env "ASDA_SG_MAP=desert"
#   Переменные: ASDA_SG_TAG (before/after), ASDA_SG_MAP (метка карты в имени кадра), ASDA_SG_OUT (папка), ASDA_SG_JEBS (через «;»),
#   ASDA_SG_PRESETS («0,1,2,3,4»), ASDA_SG_DIST (700), ASDA_SG_DROP (150).
#   ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
import glob
import json
import os
import shutil
import time

TAG = os.environ.get("ASDA_SG_TAG", "before")
MAPTAG = os.environ.get("ASDA_SG_MAP", "map")
OUTD = os.environ.get("ASDA_SG_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/skillglow_1104/frames")
RES = os.environ.get("ASDA_SG_RES", "1280x720")
DIST = os.environ.get("ASDA_SG_DIST", "700")
DROP = os.environ.get("ASDA_SG_DROP", "150")
PRESETS = [int(x) for x in os.environ.get("ASDA_SG_PRESETS", "0,1,2,3,4").split(",") if x.strip()]
PHASES = [0.25, 0.8]                       # секунды от рождения эффекта до кадра (одноразовые эффекты живут ~1–2 с)
JEBS = [x.strip() for x in os.environ.get("ASDA_SG_JEBS", ";".join([
    "eff_1_char_99_hblowb02-a_00",          # удар навыка воина («Роковой удар»), ×3 в ассете
    "eff_1_char_01_hexaa08-a_00",           # «Звёздный резак», ×3
    "eff_1_char_99_efireb03-a_00",          # огненный удар мага — правило map_fire ловило его как огонь карты, ×4
    "eff_1_char_99_ebateb03-a_00",          # удар навыка Хранителя у цели, ×3
    "eff_2_char_99_shocku01-a_01",          # состояние «оглушение» (шок) на цели-мобе (BuffFx: MobFx)
    "mon_5mareadamstun_ac_00",              # площадное оглушение моба, ×2.5
    "sb_md_eff_oh_h_01_00",                 # навык Хранителя (призванный), ×3
])).split(";") if x.strip()]
SSDIRS = [r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots/Windows",
          r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots/WindowsEditor",
          r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"]


def main():
    import unreal
    st = {"n": 0, "t": 0.0, "w": None, "q": [], "t0": 0.0}
    R = {"тег": TAG, "карта": MAPTAG, "кадры": {}, "пропало": []}

    def log(m):
        unreal.log("[skillglow] " + str(m))

    def cc(c):
        unreal.SystemLibrary.execute_console_command(st["w"], c)
        log("cmd: " + c)

    def at(sec, fn):
        st["q"].append([st["t"] + float(sec), fn])
        st["q"].sort(key=lambda x: x[0])

    def run_queue():
        while st["q"] and st["t"] >= st["q"][0][0]:
            st["q"].pop(0)[1]()

    def shoot():
        st["t0"] = time.time() - 0.5
        cc("HighResShot " + RES)

    def collect(name):
        best, bt = None, -1
        for d in SSDIRS:
            for p in glob.glob(d + "/*.png"):
                mt = os.path.getmtime(p)
                if mt > bt and mt > st["t0"]:
                    best, bt = p, mt
        if best:
            dst = os.path.join(OUTD, "%s_%s_%s.png" % (MAPTAG, name, TAG))
            shutil.move(best, dst)
            R["кадры"][name] = dst
        else:
            R["пропало"].append(name)
            log("КАДР НЕ ПОЯВИЛСЯ " + name)

    def program():
        t = 2.0
        for p in PRESETS:
            at(t, (lambda pp: (lambda: cc("scalability %d" % pp)))(p))
            at(t + 0.1, lambda: cc("asda.Fx.Light 1"))
            t += 3.0
            # контроль без эффекта
            at(t, shoot)
            at(t + 1.5, (lambda pp: (lambda: collect("p%d_control" % pp)))(p))
            t += 2.0
            for jeb in JEBS:
                for ph_i, ph in enumerate(PHASES):
                    at(t, (lambda j: (lambda: cc("asda.Fx.Light.Stand %s %s %s plate=0" % (j, DIST, DROP))))(jeb))
                    at(t + ph, shoot)
                    at(t + ph + 1.5, (lambda pp, j, k: (lambda: collect("p%d_%s_f%d" % (pp, j, k))))(p, jeb, ph_i))
                    at(t + ph + 1.6, lambda: cc("asda.Fx.Light.StandClear"))
                    t += ph + 2.2
                # тот же эффект без эффект-света: вклад материала отдельно от освещения пола
                at(t, lambda: cc("asda.Fx.Light 0"))
                at(t + 0.1, (lambda j: (lambda: cc("asda.Fx.Light.Stand %s %s %s plate=0" % (j, DIST, DROP))))(jeb))
                at(t + 0.1 + PHASES[0], shoot)
                at(t + 0.1 + PHASES[0] + 1.5, (lambda pp, j: (lambda: collect("p%d_%s_nolight" % (pp, j))))(p, jeb))
                at(t + 0.1 + PHASES[0] + 1.6, lambda: cc("asda.Fx.Light.StandClear"))
                at(t + 0.1 + PHASES[0] + 1.7, lambda: cc("asda.Fx.Light 1"))
                t += PHASES[0] + 2.4
        at(t + 1.0, done)

    def done():
        try:
            with open(os.path.join(OUTD, "shots_%s_%s.json" % (MAPTAG, TAG)), "w", encoding="utf-8") as f:
                json.dump(R, f, ensure_ascii=False, indent=1)
        except Exception as ex:
            log("JSON не записан: %s" % ex)
        log("SKILLGLOW ЗАВЕРШЕНО кадров=%d пропало=%d" % (len(R["кадры"]), len(R["пропало"])))
        at(2.0, lambda: cc("quit"))

    def start():
        w = None
        for o in unreal.ObjectIterator(unreal.World):
            if o.get_name() not in ("Untitled", "None"):
                w = o
        st["w"] = w
        log("мир: %s | эффекты: %s | пресеты %s | тег %s" % (w.get_name() if w else None, ", ".join(JEBS), PRESETS, TAG))
        os.makedirs(OUTD, exist_ok=True)
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
