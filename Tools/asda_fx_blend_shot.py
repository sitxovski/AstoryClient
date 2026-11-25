# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_blend_shot.py — КАДРЫ ЭФФЕКТОВ ДО/ПОСЛЕ правки бленда (10.10.2026, отчёт `Reports/owner_1010/fx_blend_fix.md`).
#
#   Путь игрока (-game): первой командой `r.SetRes 1024x768w`, затем на каждый эффект — стенд
#   `asda.Fx.Light.Stand <база .jeb>` (серая площадка со стенкой, эффект на ней) и серия кадров `HighResShot` в
#   заданные моменты после спавна. Кадры копируются в OUTD как `<jeb>_t<мс>_<тег>.png`.
#   ★ Каталог снимков ищется по маске `Saved*/Screenshots` — с `-saveddirsuffix` движок пишет в `Saved_<суффикс>`.
#   ★ Кадр — консольный `HighResShot` (`AutomationLibrary.take_high_res_screenshot()` в -game -RenderOffscreen файл
#     не создаёт, см. `asda_fx_skill_shot.py`).
#
#   Запуск (пример):
#     python Tools/set0_run_game.py Tools/asda_fx_blend_shot.py --map "/Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode" \
#        --res 1024x768 --marker "FXBLEND-SHOT ЗАВЕРШЕНО" --arg -AsdaLang=ru --arg -saveddirsuffix=_fxblend_b1 \
#        --env ASDA_FXBS_TAG=b1 --env "ASDA_FXBS_JEBS=eff_lucky_a_00;eff_2_char_99_regdwn01-a_00"
# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
import os, glob, shutil, json

TAG = os.environ.get("ASDA_FXBS_TAG", "b1")                                              # тег прогона (b1/b2 — до, a1 — после)
OUTD = os.environ.get("ASDA_FXBS_OUTD", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/owner_1010/fx_blend_fix/shots")   # куда класть кадры
RES = os.environ.get("ASDA_FXBS_RES", "1024x768")                                       # размер HighResShot
DIST = os.environ.get("ASDA_FXBS_DIST", "700")                                          # дистанция стенда, юн
DROP = os.environ.get("ASDA_FXBS_DROP", "150")                                          # пол стенда ниже камеры, юн
TIMES = [float(x) for x in os.environ.get("ASDA_FXBS_TIMES", "0.4;1.0;1.8").split(";") if x.strip()]   # моменты кадров, с
JEBS = [x.strip() for x in os.environ.get("ASDA_FXBS_JEBS", "").split(";") if x.strip()]              # эффекты
PROJ = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient"                                                    # корень проекта (Saved*)


def main():
    """Поставить программу съёмки на тик Slate; по окончании — `quit`."""
    import unreal
    st = {"n": 0, "t": 0.0, "w": None, "q": []}   # состояние: кадр, время, мир, очередь действий
    R = {"тег": TAG, "кадры": {}, "пропало": []}            # итог для JSON

    def log(m):
        """Строка журнала с тегом съёмки."""
        unreal.log_warning("[fxblend-shot] " + str(m))

    def cc(c):
        """Консольная команда в мире игры."""
        unreal.SystemLibrary.execute_console_command(st["w"], c)
        log("cmd: " + c)

    def at(sec, fn):
        """Поставить действие на абсолютное время `sec` от старта."""
        st["q"].append([float(sec), fn])

    def shot_name(key):
        """Имя файла кадра (уникальное: эффект + момент + тег)."""
        return "fxbs_%s_%s" % (key, TAG)

    def shoot(key):
        """Запросить кадр С ИМЕНЕМ (`filename=`): самый свежий файл по времени путал соседние кадры одной серии."""
        cc("HighResShot %s filename=%s" % (RES, shot_name(key)))

    def collect(key):
        """Найти кадр по имени (каталог Saved*/Screenshots) и скопировать в OUTD как `<key>_<тег>.png`."""
        nm = shot_name(key)
        found = [p for p in glob.glob(PROJ + "/Saved*/Screenshots/**/%s*.png" % nm, recursive=True)]
        found += [p for p in glob.glob(PROJ + "/Saved*/**/%s*.png" % nm, recursive=True) if p not in found]
        best = max(found, key=os.path.getmtime) if found else None
        if best:
            dst = os.path.join(OUTD, "%s_%s.png" % (key, TAG))
            shutil.copy(best, dst)
            R["кадры"][key] = dst
            log("СОХРАНЁН -> " + dst)
        else:
            R["пропало"].append(key)
            log("КАДР НЕ ПОЯВИЛСЯ: " + key)

    def program():
        """Расписание: стенд → кадры в моменты TIMES → снос стенда; эффекты по очереди."""
        t = 3.0
        for jeb in JEBS:
            at(t, (lambda j: (lambda: cc("asda.Fx.Light.Stand %s %s %s" % (j, DIST, DROP))))(jeb))
            last = t
            for ts in TIMES:
                key = "%s_t%04d" % (jeb, int(round(ts * 1000)))
                at(t + ts, (lambda k: (lambda: shoot(k)))(key))
                at(t + ts + 0.9, (lambda k: (lambda: collect(k)))(key))
                last = t + ts + 0.9
            at(last + 0.2, lambda: cc("asda.Fx.Light.StandClear"))
            t = last + 0.8
        at(t + 1.0, done)

    def done():
        """Итоговый JSON + маркер + выход."""
        try:
            with open(os.path.join(OUTD, "shots_%s.json" % TAG), "w", encoding="utf-8") as f:
                json.dump(R, f, ensure_ascii=False, indent=1)
        except Exception as ex:
            log("JSON не записан: %s" % ex)
        log("FXBLEND-SHOT ЗАВЕРШЕНО: кадров %d, пропало %d" % (len(R["кадры"]), len(R["пропало"])))
        st["q"].append([st["t"] + 2.0, lambda: cc("quit")])

    def start():
        """Найти мир игры, выставить разрешение ПЕРВОЙ командой и запустить программу."""
        w = None
        for o in unreal.ObjectIterator(unreal.World):
            if o.get_name() not in ("Untitled", "None"):
                w = o
        st["w"] = w
        cc("r.SetRes 1024x768w")
        cc("t.MaxFPS 60")
        log("мир: %s | эффектов %d | тег %s" % (w.get_name() if w else None, len(JEBS), TAG))
        if not os.path.isdir(OUTD):
            os.makedirs(OUTD)
        program()

    def tick(dt):
        """Тик Slate: время, очередь действий."""
        st["n"] += 1
        st["t"] += float(dt)
        if st["n"] == 1:
            start()
            return
        if st["w"] is None:
            return
        st["q"].sort(key=lambda x: x[0])
        while st["q"] and st["t"] >= st["q"][0][0]:
            try:
                st["q"].pop(0)[1]()
            except Exception as ex:
                log("ОШИБКА: %s" % ex)

    unreal.register_slate_post_tick_callback(tick)


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
