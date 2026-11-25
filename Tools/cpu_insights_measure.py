# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# cpu_insights_measure.py — сцена для профиля Unreal Insights (волна «где 7–9 мс пола CPU», 07.09.2026).
#   Путём ИГРОКА (-game, не PIE; закон ui-check-game-path-geometry). Скрыто (-RenderOffscreen).
#   Скрипт НИЧЕГО не правит в проекте: только ставит камеру, размечает области трассы и считает факты.
#
#   Что делает за прогон:
#     1) прогрев (стриминг мипов/DF, первый прогон компонентов) — вне областей трассы;
#     2) область трассы "stand"  — стоя у моста k1_a1, N секунд;
#     3) область трассы "walk"   — проход по прямой, N секунд;
#     4) перепись ФАКТИЧЕСКИ тикающих объектов карты (акторы и компоненты) — по классам;
#     5) JSON с мс/кадр (DeltaSeconds, без stat-групп) и переписью.
#   Области трассы (Trace.RegionBegin/RegionEnd) нужны, чтобы разбор Insights считал ТОЛЬКО окно
#   замера: экспорт умеет -region=<имя> (TimingProfilerManager.cpp:903 ExportTimerStatistics).
#
# Запуск:
#   python Tools/set0_run_game.py Tools/cpu_insights_measure.py --map /Game/Map/alpen/alpen
#     --arg "-trace=cpu,frame,gpu,counters" --arg "-statnamedevents"
#     --arg "-tracefile=@@ASTORY_DEV_ROOT_FWD@@/Reports/render_sets_2026-09-05/cpu_alpen_L3.utrace"
#     --env ASDA_CPU_LEVEL=3 --env ASDA_CPU_TAG=alpen_L3 --marker "ЗАВЕРШЕНО"
import os, json

import unreal

OUTD = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/render_sets_2026-09-05"          # каталог отчётов волны
LEVEL = int(os.environ.get("ASDA_CPU_LEVEL", "3"))                # уровень качества sg.* (0 = Low, 3 = Epic)
TAG = os.environ.get("ASDA_CPU_TAG", "L%d" % LEVEL)               # тег прогона в именах файлов
MAP = os.environ.get("ASDA_CPU_MAP", "/Game/Map/alpen/alpen")     # карта прогона
STAND_SEC = float(os.environ.get("ASDA_CPU_STAND", "20"))         # длительность области "stand"
WALK_SEC = float(os.environ.get("ASDA_CPU_WALK", "12"))           # длительность области "walk"
WARM_SEC = float(os.environ.get("ASDA_CPU_WARM", "8"))            # прогрев до первой области
WALK_SPEED = float(os.environ.get("ASDA_CPU_WALKSPD", "700"))     # скорость прохода, юнитов/с

# Камера — та же, что во всей таблице волны рендера (людное место у моста k1_a1_0208),
# чтобы числа сравнивались с прежними прогонами напрямую (set0_alpen_cams.json).
CAMS = {
    "/Game/Map/alpen/alpen": {"loc": [-39800.0, 14200.0, 3600.0], "pitch": -40.0, "yaw": 45.0},
    # chaos: камера k7_c1_entry из set0_chaos_cams.json (та же, что во всей таблице волны)
    "/Game/Map/castle_of_chaos/castle_of_chaos": {"loc": [-22553.8, 10059.3, 3328.3], "pitch": -10.0, "yaw": 101.09},
}
CAM = CAMS.get(MAP, CAMS["/Game/Map/alpen/alpen"])

R = {"tag": TAG, "map": MAP, "level": LEVEL, "cam": CAM, "regions": {}, "census": {}, "notes": []}
st = {"n": 0, "w": None, "q": [], "collect": None, "walk": None}   # состояние планировщика


def log(m):
    """Печать в лог прогона с общим префиксом (по нему потом ищем в логе)."""
    unreal.log("[cpu] " + str(m))


def note(m):
    """Замечание, которое попадёт и в лог, и в JSON отчёта."""
    R["notes"].append(str(m))
    log("NOTE: " + str(m))


def cc(c):
    """Консольная команда в мире прогона."""
    unreal.SystemLibrary.execute_console_command(st["w"], c)


def after(frames, fn):
    """Отложить на N кадров (кадры нужны там, где важен порядок применения ключей)."""
    st["q"].append({"f": int(frames), "s": 0.0, "fn": fn})


def after_sec(secs, fn):
    """Отложить на N секунд (окна замера считаем секундами, а не кадрами: fps плавает)."""
    st["q"].append({"f": 1, "s": float(secs), "fn": fn})


def run_queue(dt):
    """Один шаг планировщика: элемент срабатывает, когда истекли И кадры, И секунды."""
    if not st["q"]:
        return
    it = st["q"][0]
    it["f"] -= 1
    it["s"] -= dt
    if it["f"] <= 0 and it["s"] <= 0.0:
        st["q"].pop(0)
        it["fn"]()


def set_view(loc, yaw, pitch):
    """Телепорт вида игрока (BugItGo — штатный путь -game, как во всех прогонах волны)."""
    cc("BugItGo %.1f %.1f %.1f %.3f %.3f 0" % (loc[0], loc[1], loc[2], pitch, yaw))


def apply_quality(level):
    """Уровень качества по всем группам sg.* (тот же список, что в cpu_visibility_measure.py)."""
    for g in ("ViewDistanceQuality", "AntiAliasingQuality", "ShadowQuality", "GlobalIlluminationQuality",
              "ReflectionQuality", "PostProcessQuality", "TextureQuality", "EffectsQuality",
              "FoliageQuality", "ShadingQuality"):
        cc("sg.%s %d" % (g, level))
    cc("sg.ResolutionQuality 100")


def stats(vals):
    """Сводка по списку DeltaSeconds → миллисекунды кадра."""
    if not vals:
        return {}
    s = sorted(vals)
    n = len(s)
    return {"n": n,
            "avg_ms": round(1000.0 * sum(s) / n, 3),
            "med_ms": round(1000.0 * s[n // 2], 3),
            "p95_ms": round(1000.0 * s[min(n - 1, int(n * 0.95))], 3),
            "min_ms": round(1000.0 * s[0], 3),
            "max_ms": round(1000.0 * s[-1], 3)}


def find_world():
    """Мир карты -game (тот же приём, что в set0_shot_game.py: find_object/load_object по пути карты)."""
    base = MAP.rsplit("/", 1)[-1]
    for path in (MAP + "." + base, MAP):
        for fn in (unreal.find_object, unreal.load_object):
            try:
                o = fn(None, path)
                if o and isinstance(o, unreal.World):
                    return o
            except Exception:
                pass
    return None


def census():
    """ПЕРЕПИСЬ ПО ФАКТУ: сколько акторов и компонентов реально тикают на этой карте.

    Считаем не по коду (bCanEverTick в конструкторе), а по состоянию живого мира:
    IsActorTickEnabled / IsComponentTickEnabled. Отдельно — интервал тика (>0 значит «реже кадра»).
    """
    acts = {}      # класс актора → [всего, тикают]
    comps = {}     # класс компонента → [всего, тикают, с интервалом>0]
    total_a = 0
    total_ticking_a = 0
    total_c = 0
    total_ticking_c = 0
    try:
        all_actors = unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.Actor)
    except Exception as ex:
        note("перепись акторов не удалась: %s" % ex)
        return
    for a in all_actors:
        total_a += 1
        cn = a.get_class().get_name()
        e = acts.setdefault(cn, [0, 0])
        e[0] += 1
        try:
            if a.is_actor_tick_enabled():
                e[1] += 1
                total_ticking_a += 1
        except Exception:
            pass
        try:
            for c in a.get_components_by_class(unreal.ActorComponent):
                total_c += 1
                ccn = c.get_class().get_name()
                ce = comps.setdefault(ccn, [0, 0, 0])
                ce[0] += 1
                try:
                    if c.is_component_tick_enabled():
                        ce[1] += 1
                        total_ticking_c += 1
                except Exception:
                    pass
        except Exception:
            pass
    R["census"] = {
        "actors_total": total_a,
        "actors_ticking": total_ticking_a,
        "components_total": total_c,
        "components_ticking": total_ticking_c,
        # топ-40 по числу тикающих — чтобы JSON не распухал
        "actors_by_class": dict(sorted(acts.items(), key=lambda kv: -kv[1][1])[:40]),
        "components_by_class": dict(sorted(comps.items(), key=lambda kv: -kv[1][1])[:40]),
    }
    log("перепись: акторов %d (тикают %d), компонентов %d (тикают %d)"
        % (total_a, total_ticking_a, total_c, total_ticking_c))


def main():
    def phase_init():
        st["w"] = find_world()
        if st["w"] is None:
            unreal.log_error("[cpu] МИР НЕ НАЙДЕН для " + MAP)
            unreal.SystemLibrary.execute_console_command(None, "quit")
            return
        os.makedirs(OUTD, exist_ok=True)
        log("мир: %s" % st["w"].get_name())
        cc("r.SetRes 1920x1080w")
        cc("t.MaxFPS 0")
        cc("r.VSync 0")
        apply_quality(LEVEL)
        set_view(CAM["loc"], CAM["yaw"], CAM["pitch"])
        log("старт: карта=%s уровень sg=%d тег=%s" % (MAP, LEVEL, TAG))
        after_sec(WARM_SEC, do_census)

    def do_census():
        census()
        after(2, begin_stand)

    def begin_stand():
        st["collect"] = {"name": "stand", "dt": []}
        cc("Trace.RegionBegin stand")
        log("область stand: начало")
        after_sec(STAND_SEC, end_stand)

    def end_stand():
        cc("Trace.RegionEnd stand")
        R["regions"]["stand"] = stats(st["collect"]["dt"])
        st["collect"] = None
        log("область stand: конец %s" % R["regions"]["stand"])
        after(2, begin_walk)

    def begin_walk():
        st["collect"] = {"name": "walk", "dt": []}
        st["walk"] = {"x": CAM["loc"][0], "y": CAM["loc"][1], "z": CAM["loc"][2]}
        cc("Trace.RegionBegin walk")
        log("область walk: начало")
        after_sec(WALK_SEC, end_walk)

    def end_walk():
        cc("Trace.RegionEnd walk")
        R["regions"]["walk"] = stats(st["collect"]["dt"])
        st["collect"] = None
        st["walk"] = None
        log("область walk: конец %s" % R["regions"]["walk"])
        after(2, finish)

    def finish():
        out = os.path.join(OUTD, "cpu_insights_%s.json" % TAG)
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
        d = 0.016
        try:
            d = unreal.GameplayStatics.get_world_delta_seconds(st["w"])
            if st["collect"] is not None:
                st["collect"]["dt"].append(d)
        except Exception:
            pass
        # Проход: сдвигаем вид по прямой (движение камеры даёт настоящую пер-кадровую работу
        # видимости/стриминга; настоящего CharacterMovement в офлайн-прогоне нет — отмечено в отчёте).
        if st["walk"] is not None:
            st["walk"]["x"] += WALK_SPEED * d * 0.7071
            st["walk"]["y"] += WALK_SPEED * d * 0.7071
            set_view([st["walk"]["x"], st["walk"]["y"], st["walk"]["z"]], CAM["yaw"], CAM["pitch"])
        try:
            run_queue(dt if isinstance(dt, float) else 0.016)
        except Exception as ex:
            import traceback
            unreal.log_error("[cpu] ошибка шага: %s\n%s" % (ex, traceback.format_exc()))

    unreal.register_slate_post_tick_callback(tick)


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
