# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# cpu_gate_trace.py — ПРЯМОЙ замер цены гейта плашки: сколько стоит тело
#   UAsdaNameplateComponent::TickComponent при гейте ДО и ПОСЛЕ Super (asda.Vis.GateBeforeSuper).
#   Доводка CPU-волны 07.09.2026 (В3 ревизии review_cpu_insights.md, отчёт cpu_fix2_report.md).
#
#   ЗАЧЕМ ОТДЕЛЬНЫЙ ПРОГОН, а не cpu_dormant_ab.py. Дельта здесь МЕНЬШЕ шума кадра на машине
#   владельца (посторонняя нагрузка ~30 % ядер): в парном замере по DeltaSeconds она тонет.
#   Трасса же меряет САМ ТАЙМЕР плашки (инклюзивное время и число вызовов за кадр) — шум чужих
#   процессов на него почти не влияет, и «сколько именно сняли с плашки» видно напрямую.
#
#   Схема ABBA (контрбалансировка порядка, М1 ревизии): области трассы g1a → g0a → g0b → g1b.
#   Сцена — та же камера волны (alpen, мост k1_a1), уровень качества задаётся ASDA_GATE_LEVEL.
#   Мир — «людный»: asda.Tick.DormantBaked 0 возвращает тик запечённым мобам, т.е. ~430 плашек,
#   как в городе на живом сервере (именно там ревизия ждёт выигрыш).
#
# Запуск:
#   python Tools/set0_run_game.py Tools/cpu_gate_trace.py --map /Game/Map/alpen/alpen
#     --arg "-trace=cpu,frame,counters" --arg "-statnamedevents"
#     --arg "-tracefile=@@ASTORY_DEV_ROOT_FWD@@/Reports/render_sets_2026-09-05/cpu_gate_L0.utrace"
#     --env ASDA_GATE_LEVEL=0 --env ASDA_GATE_TAG=gate_L0 --marker "ЗАВЕРШЕНО"
import os, json

import unreal

OUTD = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/render_sets_2026-09-05"
LEVEL = int(os.environ.get("ASDA_GATE_LEVEL", "0"))              # уровень sg.* (0 = Low: правки CPU меряем на Low)
TAG = os.environ.get("ASDA_GATE_TAG", "gate_L%d" % LEVEL)
MAP = os.environ.get("ASDA_GATE_MAP", "/Game/Map/alpen/alpen")
WARM_SEC = float(os.environ.get("ASDA_GATE_WARM", "16"))         # прогрев (стриминг мипов) до первой области
SEG_SEC = float(os.environ.get("ASDA_GATE_SEG", "10"))           # длительность одной области
SETTLE_SEC = float(os.environ.get("ASDA_GATE_SETTLE", "1.0"))    # пауза после смены ключа

CAMS = {
    "/Game/Map/alpen/alpen": {"loc": [-39800.0, 14200.0, 3600.0], "pitch": -40.0, "yaw": 45.0},
    "/Game/Map/castle_of_chaos/castle_of_chaos": {"loc": [-22553.8, 10059.3, 3328.3], "pitch": -10.0, "yaw": 101.09},
}
CAM = CAMS.get(MAP, CAMS["/Game/Map/alpen/alpen"])

# Области трассы. Каждая = имя + список консольных команд, применяемых ПЕРЕД ней (через "|").
#   Задаются через ASDA_GATE_SEGS: "имя;кмд|кмд,имя;кмд|кмд" — так одним прогоном меряются
#   и порядок гейта (GateBeforeSuper 1/0), и сам гейт целиком (Nameplates 1/0), причём в схеме ABBA.
#   Каждая область ставит ВСЕ ключи явно: срез не должен зависеть от того, что было до него.
DEFAULT_SEGS = (
    "g1a;asda.Vis.Nameplates 1|asda.Vis.GateBeforeSuper 1,"
    "g0a;asda.Vis.Nameplates 1|asda.Vis.GateBeforeSuper 0,"
    "n0a;asda.Vis.Nameplates 0,"
    "n0b;asda.Vis.Nameplates 0,"
    "g0b;asda.Vis.Nameplates 1|asda.Vis.GateBeforeSuper 0,"
    "g1b;asda.Vis.Nameplates 1|asda.Vis.GateBeforeSuper 1")
SEGMENTS = []
for _sp in os.environ.get("ASDA_GATE_SEGS", DEFAULT_SEGS).split(","):
    _nm, _cmds = _sp.split(";", 1)
    SEGMENTS.append((_nm.strip(), [c.strip() for c in _cmds.split("|") if c.strip()]))

R = {"tag": TAG, "map": MAP, "level": LEVEL, "cam": CAM, "seg_sec": SEG_SEC,
     "segments": {s[0]: s[1] for s in SEGMENTS}, "frames": {}, "census": {}, "notes": []}
st = {"n": 0, "w": None, "q": [], "collect": None}


def log(m):
    unreal.log("[gate] " + str(m))


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


def stats(vals):
    if not vals:
        return {}
    s = sorted(vals)
    n = len(s)
    return {"n": n, "avg_ms": round(1000.0 * sum(s) / n, 3), "med_ms": round(1000.0 * s[n // 2], 3),
            "min_ms": round(1000.0 * s[0], 3), "max_ms": round(1000.0 * s[-1], 3)}


find_world = PH.make_find_world(MAP)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 11 стр.)


def count_ticking_nameplates():
    """Сколько плашек РЕАЛЬНО тикает — доказательство, что сцена «людная» (ожидание ~426)."""
    n = 0
    try:
        for a in unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.Actor):
            for c in a.get_components_by_class(unreal.ActorComponent):
                try:
                    if c.get_class().get_name() == "AsdaNameplateComponent" and c.is_component_tick_enabled():
                        n += 1
                except Exception:
                    pass
    except Exception as ex:
        R["notes"].append("перепись плашек не удалась: %s" % ex)
    return n


def main():
    def phase_init():
        st["w"] = find_world()
        if st["w"] is None:
            unreal.log_error("[gate] МИР НЕ НАЙДЕН для " + MAP)
            unreal.SystemLibrary.execute_console_command(None, "quit")
            return
        os.makedirs(OUTD, exist_ok=True)
        cc("r.SetRes 1920x1080w")
        cc("t.MaxFPS 0")
        cc("r.VSync 0")
        apply_quality(LEVEL)
        cc("asda.Vis.Enable 1")          # гейт значимости включён (иначе мерить нечего)
        cc("asda.Tick.DormantBaked 0")   # «людный мир»: тик возвращён запечённым мобам (~430 плашек)
        cc("BugItGo %.1f %.1f %.1f %.3f %.3f 0" % (CAM["loc"][0], CAM["loc"][1], CAM["loc"][2],
                                                   CAM["pitch"], CAM["yaw"]))
        log("старт: карта=%s уровень sg=%d тег=%s области=%s" % (MAP, LEVEL, TAG, R["segments"]))
        after_sec(WARM_SEC, do_census)

    def do_census():
        R["census"]["ticking_nameplates"] = count_ticking_nameplates()
        log("тикающих плашек: %s" % R["census"]["ticking_nameplates"])
        after(2, schedule_segments)

    def schedule_segments():
        for name, cmds in SEGMENTS:
            for _c in cmds:
                after(1, lambda c_=_c: cc(c_))
            after_sec(SETTLE_SEC, lambda nm=name: begin_seg(nm))
            after_sec(SEG_SEC + 0.2, lambda nm=name: end_seg(nm))
        after(2, finish)

    def begin_seg(name):
        cc("Trace.RegionBegin %s" % name)
        st["collect"] = {"name": name, "t": 0.0, "dt": []}

    def end_seg(name):
        cc("Trace.RegionEnd %s" % name)
        if st["collect"] and st["collect"]["dt"]:
            R["frames"][name] = stats(st["collect"]["dt"])
        st["collect"] = None
        log("область %s закрыта: %s" % (name, R["frames"].get(name)))

    def finish():
        out = os.path.join(OUTD, "cpu_gate_trace_%s.json" % TAG)
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
            if st["collect"] is not None and st["collect"]["t"] < SEG_SEC:
                st["collect"]["dt"].append(d)
                st["collect"]["t"] += d
        except Exception:
            pass
        try:
            run_queue(dt if isinstance(dt, float) else 0.016)
        except Exception as ex:
            import traceback
            unreal.log_error("[gate] ошибка шага: %s\n%s" % (ex, traceback.format_exc()))

    unreal.register_slate_post_tick_callback(tick)


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
