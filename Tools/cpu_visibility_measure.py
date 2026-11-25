# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# cpu_visibility_measure.py — ВОЛНА ОПТИМИЗАЦИИ ПРОЦЕССОРА ПО ВИДИМОСТИ (07.09.2026).
#   Замер путём ИГРОКА (-game, не PIE, не editor-viewport; закон ui-check-game-path-geometry).
#   БЕЗ stat-групп: сбор статистики сам стоит 2–3 мс (cpu_gamethread_plan.md §0 п.1) — кадр меряем
#   по DeltaSeconds мира, как frame_ms_avg60 в set0_shot_game.py.
#
#   За ОДИН запуск, на ОДНОЙ камере и в ОДНОЙ сцене (закон «один фактор за прогон» соблюдён тем,
#   что фазы отличаются ровно одним ключом относительно базы):
#     фаза base   — asda.Vis.Enable 0,  asda.Clouds.UpdateEvery 1   (поведение до волны)
#     фаза plates — Enable 1, Nameplates 1, Effects 0, Clouds 1
#     фаза fx     — Enable 1, Nameplates 0, Effects 1, Clouds 1
#     фаза clouds — Enable 1, Nameplates 0, Effects 0, Clouds 4
#     фаза all    — Enable 1, Nameplates 1, Effects 1, Clouds 4
#   Затем ПРОВЕРКА РЫВКА: три рывка (влево/вправо/вверх) на 180°/90° за ~0.2 с; счётчики поп-ина
#   подсистемы (объект в НАСТОЯЩЕМ кадре, но заморожен) + лист кадров каждые 2 кадра.
#
# Запуск: python Tools/set0_run_game.py Tools/cpu_visibility_measure.py --map /Game/Map/alpen/alpen
#           --env ASDA_VIS_LEVEL=3 --env ASDA_VIS_TAG=L3 --marker "ЗАВЕРШЕНО"
import os, json, glob, shutil

import unreal

OUTD = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/render_sets_2026-09-05"
SSDIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
LEVEL = int(os.environ.get("ASDA_VIS_LEVEL", "3"))          # уровень качества sg.* (0 = Low, 3 = Epic)
TAG = os.environ.get("ASDA_VIS_TAG", "L%d" % LEVEL)
MEASURE_SEC = float(os.environ.get("ASDA_VIS_SEC", "30"))   # суммарное время замера одной фазы (сумма срезов)
SLICE_SEC = float(os.environ.get("ASDA_VIS_SLICE", "1.2"))  # длительность ОДНОГО среза фазы
SETTLE_SEC = float(os.environ.get("ASDA_VIS_SETTLE", "0.6"))# пауза после смены ключей перед сбором
CYCLES = int(round(MEASURE_SEC / SLICE_SEC))                # сколько раз повторяется вся карусель фаз
SHOTS = os.environ.get("ASDA_VIS_SHOTS", "1") == "1"        # снимать лист кадров рывка
SLICES = {}                                                 # фаза → список медиан срезов, мс
# ★ Стресс-режим (ASDA_VIS_STRESS=1): раскрыть ВСЕХ мобов карты и включить им постоянный показ имени.
#   Это ВЕРХНЯЯ ОЦЕНКА нагрузки — так выглядит людный город, где плашки реально проецируются каждый кадр
#   (в офлайн-прогоне мобы скрыты, и старый путь плашки выходит рано сам по себе — экономить нечего).
STRESS = os.environ.get("ASDA_VIS_STRESS", "0") == "1"
# Камера — та же k1_a1_0208, что во всей таблице волны рендера (set0_alpen_cams.json), чтобы числа
# сравнивались с прежними прогонами напрямую.
CAM = {"loc": [-39800.0, 14200.0, 3600.0], "pitch": -40.0, "yaw": 45.0}

PHASES = [
    ("base",   {"asda.Vis.Enable": 0, "asda.Vis.Nameplates": 1, "asda.Vis.Effects": 1, "asda.Clouds.UpdateEvery": 1}),
    ("plates", {"asda.Vis.Enable": 1, "asda.Vis.Nameplates": 1, "asda.Vis.Effects": 0, "asda.Clouds.UpdateEvery": 1}),
    ("fx",     {"asda.Vis.Enable": 1, "asda.Vis.Nameplates": 0, "asda.Vis.Effects": 1, "asda.Clouds.UpdateEvery": 1}),
    ("clouds", {"asda.Vis.Enable": 1, "asda.Vis.Nameplates": 0, "asda.Vis.Effects": 0, "asda.Clouds.UpdateEvery": 4}),
    ("all",    {"asda.Vis.Enable": 1, "asda.Vis.Nameplates": 1, "asda.Vis.Effects": 1, "asda.Clouds.UpdateEvery": 4}),
]

# Рывки: (имя, шаг по yaw за кадр, шаг по pitch за кадр, число кадров рывка)
# 180° за 12 кадров ≈ 0.2 с при 60 fps → мгновенная угловая скорость ~900 °/с.
JERKS = [
    ("right", 15.0, 0.0, 12),
    ("left", -15.0, 0.0, 12),
    ("up", 0.0, 5.0, 12),
]

R = {"level": LEVEL, "tag": TAG, "cam": CAM, "measure_sec": MEASURE_SEC, "slice_sec": SLICE_SEC, "cycles": CYCLES, "phases": {}, "jerks": {}, "notes": []}
st = {"n": 0, "w": None, "q": [], "dt": [], "collect": None}


def log(m):
    unreal.log("[vis] " + str(m))


def note(m):
    R["notes"].append(str(m))
    log("NOTE: " + str(m))


def cc(c):
    unreal.SystemLibrary.execute_console_command(st["w"], c)


# Планировщик: элемент срабатывает, когда прошли И кадры, И секунды (любое из двух может быть 0).
# Секунды нужны для окон замера: считать окно кадрами нельзя — при разном fps окно будет разной длины.
import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
_ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
import asda_pie_harness as PH
after = PH.make_after_frames_dict(st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 2 стр.)


after_sec = PH.make_after_seconds_dict(st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 2 стр.)


run_queue = PH.make_dict_queue_runner(st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 9 стр.)


def set_view(yaw, pitch):
    cc("BugItGo %.1f %.1f %.1f %.3f %.3f 0" % (CAM["loc"][0], CAM["loc"][1], CAM["loc"][2], pitch, yaw))


def new_pngs(seen):
    out = []
    for p in sorted(glob.glob(os.path.join(SSDIR, "**", "*.png"), recursive=True), key=os.path.getmtime):
        if p not in seen:
            seen.add(p)
            out.append(p)
    return out


apply_quality = PH.make_apply_quality(cc)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 6 стр.)


def stats(vals):
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


MAP = os.environ.get("ASDA_VIS_MAP", "/Game/Map/alpen/alpen")


def find_world():
    """Мир карты -game (как в set0_shot_game.py: find_object/load_object по пути карты)."""
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


def main():
    seen = set(glob.glob(os.path.join(SSDIR, "**", "*.png"), recursive=True))

    def phase_init():
        st["w"] = find_world()
        if st["w"] is None:
            unreal.log_error("[vis] МИР НЕ НАЙДЕН для " + MAP)
            unreal.SystemLibrary.execute_console_command(None, "quit")
            return
        os.makedirs(OUTD, exist_ok=True)
        log("мир: %s" % st["w"].get_name())
        cc("r.SetRes 1920x1080w")
        cc("t.MaxFPS 0")
        cc("r.VSync 0")
        apply_quality(LEVEL)
        if STRESS:
            apply_stress()
        set_view(CAM["yaw"], CAM["pitch"])
        log("старт: уровень sg=%d тег=%s окно=%.0f с" % (LEVEL, TAG, MEASURE_SEC))
        # Прогрев: стриминг мипов/DF + первый прогон компонентов
        after_sec(6.0, schedule_phases)   # прогрев: стриминг мипов/DF и первый прогон компонентов

    # ★ ЧЕРЕДУЮЩИЕСЯ СРЕЗЫ (07.09.2026). Первый прогон показал, что машина занята посторонним
    #   процессом (QZSim-Win64-Shipping, ~5.3 ядра из 8), и кадр «плывёт» в разы за минуты.
    #   Замер длинными блоками «фаза за фазой» в таких условиях меряет ДРЕЙФ, а не фазу.
    #   Поэтому фазы чередуются короткими срезами и повторяются CYCLES раз: медленный дрейф
    #   действует на все фазы одинаково, а сравнение ведётся ПОПАРНО внутри цикла (base ↔ фаза).
    def apply_stress():
        """Верхняя оценка: все мобы карты видимы, у всех плашек постоянный показ имени."""
        n = 0
        try:
            for m in unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.AsdaMobActor):
                m.set_actor_hidden_in_game(False)
                try:
                    np_ = m.get_component_by_class(unreal.AsdaNameplateComponent)
                    if np_:
                        np_.set_editor_property("always_show_name", True)
                        np_.set_editor_property("limit_by_server_ao_i", False)
                except Exception:
                    pass
                n += 1
        except Exception as ex:
            note("стресс-режим не применён: %s" % ex)
        R["stress_mobs"] = n
        log("СТРЕСС: раскрыто мобов и включён постоянный показ имени: %d" % n)

    def schedule_phases():
        for c in range(CYCLES):
            for name, keys in PHASES:
                after(1, lambda keys=keys, name=name: apply_phase(name, keys, quiet=True))
                after_sec(SETTLE_SEC, lambda name=name: start_collect(name))
                after_sec(SLICE_SEC + 0.2, lambda name=name, c=c: stop_slice(name, c))
            after(1, lambda c=c: log("цикл %d/%d готов" % (c + 1, CYCLES)))
        after(2, summarize_phases)
        after(2, lambda: cc("asda.Vis.Dump"))
        after(2, schedule_jerks)

    def apply_phase(name, keys, quiet=False):
        for k, v in keys.items():
            cc("%s %d" % (k, v))
        if not quiet:
            log("фаза %s: %s" % (name, keys))

    def start_collect(name):
        st["dt"] = []
        st["collect"] = {"name": name, "t": 0.0}

    def stop_slice(name, cycle):
        st["collect"] = None
        s = sorted(st["dt"])
        if not s:
            return
        med = 1000.0 * s[len(s) // 2]      # медиана среза — устойчива к разовым провалам чужого процесса
        SLICES.setdefault(name, []).append(round(med, 3))

    def summarize_phases():
        """Итог: медиана срезов по фазе + ПОПАРНАЯ разница с base внутри каждого цикла."""
        for name, arr in SLICES.items():
            a = sorted(arr)
            R["phases"][name] = {"slices": len(a), "med_of_slices_ms": round(a[len(a) // 2], 3),
                                 "min_ms": round(a[0], 3), "max_ms": round(a[-1], 3), "all": arr}
        base = SLICES.get("base", [])
        for name, arr in SLICES.items():
            if name == "base":
                continue
            n = min(len(base), len(arr))
            d = sorted(arr[i] - base[i] for i in range(n))
            if d:
                R["phases"][name]["paired_delta_med_ms"] = round(d[len(d) // 2], 3)
                R["phases"][name]["paired_delta_wins"] = sum(1 for x in d if x < 0)
                R["phases"][name]["paired_n"] = n
        log("ИТОГ фаз: %s" % json.dumps({k: {kk: vv for kk, vv in v.items() if kk != "all"}
                                         for k, v in R["phases"].items()}, ensure_ascii=False))

    # ── проверка рывка ─────────────────────────────────────────────────────────────────────────────
    def schedule_jerks():
        # Волна включена целиком — проверяем именно рабочую конфигурацию
        after(1, lambda: apply_phase("all", PHASES[-1][1]))
        for jname, dyaw, dpitch, nf in JERKS:
            # проход 1: чистая динамика (без снимков) — счётчики поп-ина
            after(60, lambda jn=jname: begin_jerk(jn, False))
            for i in range(nf):
                after(1, lambda i=i, dy=dyaw, dp=dpitch: set_view(CAM["yaw"] + dy * (i + 1), CAM["pitch"] + dp * (i + 1)))
            after(60, lambda jn=jname, dy=dyaw, dp=dpitch, nf=nf: end_jerk(jn, dy, dp, nf))
            after(30, lambda: set_view(CAM["yaw"], CAM["pitch"]))
            # проход 2: лист кадров (снимки каждые 2 кадра в течение ~1 с)
            if SHOTS:
                after(60, lambda jn=jname: begin_jerk(jn + "_shots", False))
                for i in range(nf):
                    after(1, lambda i=i, dy=dyaw, dp=dpitch: set_view(CAM["yaw"] + dy * (i + 1), CAM["pitch"] + dp * (i + 1)))
                    after(1, lambda: cc("HighResShot 960x540"))
                for i in range(9):
                    after(2, lambda: cc("HighResShot 960x540"))
                after(60, lambda jn=jname: collect_shots(jn, seen))
                after(30, lambda: set_view(CAM["yaw"], CAM["pitch"]))
        after(5, finish)

    def begin_jerk(jname, _unused):
        cc("asda.Vis.PopInReset")
        log("РЫВОК %s: счётчики обнулены" % jname)

    def end_jerk(jname, dyaw, dpitch, nf):
        cc("asda.Vis.Dump")   # строки «ПОП-ИН: событий=…» уходят в лог — их читает разбор
        R["jerks"][jname] = {"dyaw_per_frame": dyaw, "dpitch_per_frame": dpitch, "frames": nf,
                             "total_deg": round(abs(dyaw) * nf + abs(dpitch) * nf, 1)}
        log("РЫВОК %s завершён (%s° за %d кадров)" % (jname, R["jerks"][jname]["total_deg"], nf))

    def collect_shots(jname, seen):
        got = new_pngs(seen)
        dests = []
        for i, p in enumerate(got):
            d = os.path.join(OUTD, "cpu_vis_jerk_%s_%s_%02d.png" % (TAG, jname, i))
            try:
                shutil.copy(p, d)
                dests.append(os.path.basename(d))
            except Exception as ex:
                note("снимок не скопирован: %s" % ex)
        R["jerks"].setdefault(jname, {})["shots"] = dests
        log("РЫВОК %s: кадров %d" % (jname, len(dests)))

    def finish():
        out = os.path.join(OUTD, "cpu_visibility_%s.json" % TAG)
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
            if st["collect"] is not None:
                if st["collect"]["t"] < SLICE_SEC:
                    st["dt"].append(d)
                    st["collect"]["t"] += d
        except Exception:
            pass
        try:
            run_queue(dt if isinstance(dt, float) else 0.016)
        except Exception as ex:
            import traceback
            unreal.log_error("[vis] ошибка шага: %s\n%s" % (ex, traceback.format_exc()))

    unreal.register_slate_post_tick_callback(tick)


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
