# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# cpu_dormant_ab.py — замер «до/после» правки «сон запечённых мобов» (asda.Tick.DormantBaked),
#   волна CPU 07.09.2026. Путём ИГРОКА (-game, скрыто), БЕЗ stat-групп и БЕЗ трассы:
#   сбор статистики и Insights сами стоят игровому потоку несколько мс.
#
#   ★ ЧЕРЕДУЮЩИЕСЯ СРЕЗЫ. У владельца на машине бывает посторонняя нагрузка, и кадр «плывёт»
#   за минуты. Поэтому две фазы (0 = как было, 1 = правка) чередуются короткими срезами и
#   повторяются CYCLES раз: медленный дрейф действует на обе одинаково, сравнение — ПОПАРНО
#   внутри цикла. Метод скопирован с cpu_visibility_measure.py (та же волна, тот же владелец).
#
# Запуск:
#   python Tools/set0_run_game.py Tools/cpu_dormant_ab.py --map /Game/Map/alpen/alpen
#     --env ASDA_AB_LEVEL=3 --env ASDA_AB_TAG=alpen_L3 --marker "ЗАВЕРШЕНО"
import os, json

import unreal

OUTD = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/render_sets_2026-09-05"
LEVEL = int(os.environ.get("ASDA_AB_LEVEL", "3"))            # уровень качества sg.*
TAG = os.environ.get("ASDA_AB_TAG", "L%d" % LEVEL)           # тег в имени JSON
MAP = os.environ.get("ASDA_AB_MAP", "/Game/Map/alpen/alpen") # карта
SLICE_SEC = float(os.environ.get("ASDA_AB_SLICE", "1.2"))    # длительность одного среза
SETTLE_SEC = float(os.environ.get("ASDA_AB_SETTLE", "0.6"))  # пауза после смены ключа
CYCLES = int(os.environ.get("ASDA_AB_CYCLES", "20"))         # сколько раз повторить пару фаз
# ★ ДОВОДКА 07.09.2026: ключ ЗАДАЁТСЯ (М1 ревизии). Скрипт мерит любой булев ключ «до/после» тем же методом:
#   asda.Tick.DormantBaked (волна сна запечённых) или asda.Vis.GateBeforeSuper (место гейта плашки).
CVAR = os.environ.get("ASDA_AB_CVAR", "asda.Tick.DormantBaked")
# Строки консоли, выполняемые ОДИН РАЗ после прогрева (напр. "asda.Tick.DormantBaked 0" — модель людного города).
PRECMDS = [c for c in os.environ.get("ASDA_AB_PRE", "").split(";") if c.strip()]
WARM_SEC = float(os.environ.get("ASDA_AB_WARM", "8"))        # прогрев (стриминг мипов/DF)

CAMS = {
    "/Game/Map/alpen/alpen": {"loc": [-39800.0, 14200.0, 3600.0], "pitch": -40.0, "yaw": 45.0},
    "/Game/Map/castle_of_chaos/castle_of_chaos": {"loc": [-22553.8, 10059.3, 3328.3], "pitch": -10.0, "yaw": 101.09},
}
CAM = CAMS.get(MAP, CAMS["/Game/Map/alpen/alpen"])

# Фазы отличаются РОВНО одним ключом.
PHASES = [("off", 0), ("on", 1)]
# ★ ДОВОДКА 07.09.2026 (М1 ревизии review_cpu_insights.md): КОНТРБАЛАНСИРОВКА ПОРЯДКА (схема ABBA).
#   До доводки внутри каждого цикла порядок был всегда «off → on», поэтому при монотонном дрейфе машины
#   (прогрев, стриминг, чужая нагрузка) фаза «on» систематически мерилась ПОЗЖЕ «off» — систематический
#   сдвиг, который на малых дельтах неотличим от эффекта. Теперь чётные циклы идут «off → on», нечётные —
#   «on → off»: линейный дрейф внутри пары гасится, ведь его знак меняется вместе с порядком.
def phases_for_cycle(c):
    return PHASES if (c % 2 == 0) else list(reversed(PHASES))

R = {"tag": TAG, "map": MAP, "level": LEVEL, "cam": CAM, "slice_sec": SLICE_SEC,
     "cycles": CYCLES, "cvar": CVAR, "pre_cmds": PRECMDS, "order": "ABBA (контрбалансировка, доводка 07.09.2026)",
     "phases": {}, "pairs": [], "notes": []}
SL = {"off": [], "on": []}                                   # фаза → медианы срезов, мс
st = {"n": 0, "w": None, "q": [], "collect": None}


def log(m):
    unreal.log("[ab] " + str(m))


def cc(c):
    unreal.SystemLibrary.execute_console_command(st["w"], c)


import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
_ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
import asda_pie_harness as PH
after = PH.make_after_frames_dict(st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 2 стр.)


after_sec = PH.make_after_seconds_dict(st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 2 стр.)


run_queue = PH.make_dict_queue_runner(st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 9 стр.)


apply_quality = PH.make_apply_quality(cc)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 6 стр.)


def med(vals):
    s = sorted(vals)
    return s[len(s) // 2] if s else 0.0


def stats(vals):
    if not vals:
        return {}
    s = sorted(vals)
    n = len(s)
    return {"n": n, "avg_ms": round(sum(s) / n, 3), "med_ms": round(s[n // 2], 3),
            "min_ms": round(s[0], 3), "max_ms": round(s[-1], 3)}


find_world = PH.make_find_world(MAP)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 11 стр.)


def count_ticking():
    """Сколько компонентов реально тикает СЕЙЧАС — доказательство, что ключ подействовал."""
    n = 0
    try:
        for a in unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.Actor):
            for c in a.get_components_by_class(unreal.ActorComponent):
                try:
                    if c.is_component_tick_enabled():
                        n += 1
                except Exception:
                    pass
    except Exception as ex:
        R["notes"].append("подсчёт тикающих не удался: %s" % ex)
    return n


def main():
    def phase_init():
        st["w"] = find_world()
        if st["w"] is None:
            unreal.log_error("[ab] МИР НЕ НАЙДЕН для " + MAP)
            unreal.SystemLibrary.execute_console_command(None, "quit")
            return
        os.makedirs(OUTD, exist_ok=True)
        cc("r.SetRes 1920x1080w")
        cc("t.MaxFPS 0")
        cc("r.VSync 0")
        apply_quality(LEVEL)
        cc("BugItGo %.1f %.1f %.1f %.3f %.3f 0" % (CAM["loc"][0], CAM["loc"][1], CAM["loc"][2],
                                                   CAM["pitch"], CAM["yaw"]))
        log("старт: карта=%s уровень sg=%d тег=%s циклов=%d ключ=%s" % (MAP, LEVEL, TAG, CYCLES, CVAR))
        after_sec(WARM_SEC, count_and_schedule)

    def count_and_schedule():
        # Разовые команды сцены (напр. вернуть тик запечённым мобам — модель людного города).
        for c in PRECMDS:
            after(1, lambda c_=c.strip(): cc(c_))
        if PRECMDS:
            after(30, lambda: log("разовые команды применены: %s" % PRECMDS))
        # Доказательство действия ключа: считаем тикающие компоненты в обоих положениях.
        cc("%s 0" % CVAR)
        after(3, lambda: R.setdefault("ticking_components", {}).update({"off": count_ticking()}))
        after(1, lambda: cc("%s 1" % CVAR))
        after(3, lambda: R.setdefault("ticking_components", {}).update({"on": count_ticking()}))
        after(1, lambda: log("тикающих компонентов: %s" % R.get("ticking_components")))
        after(1, schedule_phases)

    def schedule_phases():
        for c in range(CYCLES):
            for name, val in phases_for_cycle(c):
                after(1, lambda v=val: cc("%s %d" % (CVAR, v)))
                after_sec(SETTLE_SEC, lambda nm=name: start_collect(nm))
                after_sec(SLICE_SEC + 0.2, lambda nm=name, cc_=c: stop_slice(nm, cc_))
        after(2, summarize)

    def start_collect(name):
        st["collect"] = {"name": name, "t": 0.0, "dt": []}

    def stop_slice(name, cyc):
        if st["collect"] and st["collect"]["dt"]:
            SL[name].append(1000.0 * med(st["collect"]["dt"]))
        st["collect"] = None

    def summarize():
        for k in ("off", "on"):
            R["phases"][k] = stats(SL[k])
        # ★ Доводка 07.09.2026: сохраняем СЫРЫЕ медианы срезов по фазам — без них нельзя увидеть
        #   дрейф машины по ходу прогона (ровно он и путал мелкие дельты), а вердикт «шум/эффект»
        #   должен быть проверяем по числам, а не по итоговой медиане.
        R["slices"] = {k: [round(v, 3) for v in SL[k]] for k in ("off", "on")}
        wins = 0
        n = min(len(SL["off"]), len(SL["on"]))
        deltas = []
        for i in range(n):
            d = SL["on"][i] - SL["off"][i]
            deltas.append(round(d, 3))
            if d < 0:
                wins += 1
        R["pairs"] = deltas
        R["paired"] = {"n": n, "wins_for_on": wins,
                       "median_delta_ms": round(med(deltas), 3) if deltas else 0.0,
                       "mean_delta_ms": round(sum(deltas) / n, 3) if n else 0.0}
        log("ПАРНО: срезов %d, «правка лучше» в %d, медиана дельты %.3f мс"
            % (n, wins, R["paired"]["median_delta_ms"]))
        after(2, finish)

    def finish():
        out = os.path.join(OUTD, "cpu_dormant_ab_%s.json" % TAG)
        with open(out, "w", encoding="utf-8") as f:
            json.dump(R, f, ensure_ascii=False, indent=1)
        log("сохранено -> %s" % out)
        log("ЗАВЕРШЕНО")
        cc("quit")

    def tick(dt):
        st["n"] += 1
        if st["n"] == 1:
            phase_init()
            return
        try:
            d = unreal.GameplayStatics.get_world_delta_seconds(st["w"])
            if st["collect"] is not None and st["collect"]["t"] < SLICE_SEC:
                st["collect"]["dt"].append(d)
                st["collect"]["t"] += d
        except Exception:
            pass
        try:
            run_queue(dt if isinstance(dt, float) else 0.016)
        except Exception as ex:
            import traceback
            unreal.log_error("[ab] ошибка шага: %s\n%s" % (ex, traceback.format_exc()))

    unreal.register_slate_post_tick_callback(tick)


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
