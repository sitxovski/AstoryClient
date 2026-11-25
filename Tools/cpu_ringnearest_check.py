# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# cpu_ringnearest_check.py — живая проверка дефекта В1 ревизии review_cpu_insights.md (доводка 07.09.2026):
#   дев-команда `asda.Target.RingNearest mob` ПОКАЗЫВАЕТ запечённого моба, но после волны «сна
#   запечённых» (asda.Tick.DormantBaked 1) его компоненты усыплены → нет плашки и нет позы.
#   Правка: команда зовёт AAsdaMobActor::SetDormantComponentTicks(true) рядом с SetActorHiddenInGame(false).
#
#   Проверка идёт ПУТЁМ ИГРОКА (-game, закон ui-check-game-path-geometry) и меряет ФАКТ, а не намерение:
#     1) до команды — переписываем показанных (не скрытых) запечённых мобов: их быть не должно;
#     2) команда `asda.Target.RingNearest mob`;
#     3) после команды — у показанного моба должны быть включены тик актора, тик меша и тик плашки;
#     4) снимок кадра с UI (`shot showui`) — глазами видно имя над головой.
#
# Запуск:
#   python Tools/set0_run_game.py Tools/cpu_ringnearest_check.py --map /Game/Map/alpen/alpen
#     --env ASDA_RING_TAG=ring_after --marker "ЗАВЕРШЕНО"
import os, json

import unreal

OUTD = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/render_sets_2026-09-05"
TAG = os.environ.get("ASDA_RING_TAG", "ring")
MAP = os.environ.get("ASDA_RING_MAP", "/Game/Map/alpen/alpen")
WARM_SEC = float(os.environ.get("ASDA_RING_WARM", "10"))

R = {"tag": TAG, "map": MAP, "before": {}, "after": {}, "notes": []}
st = {"n": 0, "w": None, "q": []}


def log(m):
    unreal.log("[ring] " + str(m))


def cc(c):
    unreal.SystemLibrary.execute_console_command(st["w"], c)


import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
_ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
import asda_pie_harness as PH
after = PH.make_after_frames_dict(st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 2 стр.)


after_sec = PH.make_after_seconds_dict(st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 2 стр.)


run_queue = PH.make_dict_queue_runner(st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 9 стр.)


find_world = PH.make_find_world(MAP)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 11 стр.)


def survey():
    """Показанные (не скрытые) мобы и состояние их тиков — то, что решает вопрос В1."""
    out = {"shown_mobs": [], "mobs_total": 0}
    try:
        for a in unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.Actor):
            if a.get_class().get_name() != "AsdaMobActor":
                continue
            out["mobs_total"] += 1
            # ★ ГРАБЛИ (07.09.2026): фильтровать по `is_hidden()` НЕЛЬЗЯ — в Python-API актора такого
            #   метода нет, исключение съедало ВСЕ записи, и проверка давала ложный «ПРОВАЛ».
            #   Признак «этот моб разбужен» берём по СОСТОЯНИЮ ТИКОВ: у спящего запечённого они все off.
            rec = {"name": a.get_name(), "actor_tick": bool(a.is_actor_tick_enabled()),
                   "mesh_tick": None, "plate_tick": None}
            for c in a.get_components_by_class(unreal.ActorComponent):
                cn = c.get_class().get_name()
                try:
                    if cn == "SkeletalMeshComponent" and rec["mesh_tick"] is None:
                        rec["mesh_tick"] = bool(c.is_component_tick_enabled())
                    elif cn == "AsdaNameplateComponent":
                        rec["plate_tick"] = bool(c.is_component_tick_enabled())
                except Exception:
                    pass
            if rec["actor_tick"] or rec["mesh_tick"] or rec["plate_tick"]:
                out["shown_mobs"].append(rec)   # «показанный» = разбуженный (у спящего всё off)
    except Exception as ex:
        R["notes"].append("перепись не удалась: %s" % ex)
    return out


def main():
    def phase_init():
        st["w"] = find_world()
        if st["w"] is None:
            unreal.log_error("[ring] МИР НЕ НАЙДЕН для " + MAP)
            unreal.SystemLibrary.execute_console_command(None, "quit")
            return
        os.makedirs(OUTD, exist_ok=True)
        cc("r.SetRes 1920x1080w")
        # ★ ГРАБЛИ (07.09.2026): без `?game=/Script/AsdaReborn.AsdaWorldGameMode` в -game поднимается
        #   ДЕФОЛТНЫЙ режим, AsdaPlayerController не создаётся, и команда отвечает «нет AsdaPlayerController».
        #   Тот же приём, что во всех живых проверках порта (asda_*_live_check.py).
        cc("open %s?game=/Script/AsdaReborn.AsdaWorldGameMode" % MAP)
        log("старт проверки RingNearest на %s (переезд в игровой режим)" % MAP)
        after_sec(WARM_SEC, retake_world)

    def retake_world():
        w = find_world()
        if w is not None:
            st["w"] = w
        cc("asda.Tick.DormantBaked 1")   # штатное состояние волны: запечённые спят
        after_sec(3.0, step_before)

    def step_before():
        R["before"] = survey()
        log("до команды: показанных мобов %d из %d"
            % (len(R["before"]["shown_mobs"]), R["before"]["mobs_total"]))
        after(2, step_cmd)

    def step_cmd():
        cc("asda.Target.RingNearest mob")
        after_sec(1.5, step_after)

    def step_after():
        R["after"] = survey()
        log("после команды: %s" % json.dumps(R["after"]["shown_mobs"], ensure_ascii=False))
        cc("shot showui")               # снимок кадра С интерфейсом: плашка видна глазами
        after_sec(2.0, finish)

    def finish():
        ok = any(m["actor_tick"] and m["plate_tick"] and m["mesh_tick"] for m in R["after"]["shown_mobs"])
        R["verdict"] = "OK: показанный моб разбужен (тик актора+меша+плашки)" if ok else \
                       "ПРОВАЛ: показанный моб остался усыплённым"
        log(R["verdict"])
        out = os.path.join(OUTD, "cpu_ringnearest_%s.json" % TAG)
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
            run_queue(dt if isinstance(dt, float) else 0.016)
        except Exception as ex:
            import traceback
            unreal.log_error("[ring] ошибка шага: %s\n%s" % (ex, traceback.format_exc()))

    unreal.register_slate_post_tick_callback(tick)


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
