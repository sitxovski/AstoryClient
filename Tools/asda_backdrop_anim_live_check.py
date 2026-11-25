# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_backdrop_anim_live_check.py — ЖИВАЯ (PIE) проверка анимации растительности login-задника.
#
# ★★★ ЗАЧЕМ ЖИВАЯ (закон [[false-negatives-method]]): наличие DataAsset и тегов в уровне ничего не
#   доказывает. Реально ли дирижёр нашёл элементы, реально ли двигает их трансформ и совпадает ли
#   движение с ЭКСПОРТИРОВАННЫМИ данными — видно только в запущенном мире.
#
# ЧТО ПРОВЕРЯЕТСЯ (три независимых утверждения):
#   1. ассеты на месте: DA_LoginBackdropAnim существует, треки НЕ пусты (632 × 201 ключ);
#   2. дирижёр привязал элементы (GetBoundElementCount > 0) и они РЕАЛЬНО ДВИГАЮТСЯ:
#      относительная позиция контрольных элементов меняется между отсчётами;
#   3. движение — ИМЕННО ОРИГИНАЛЬНОЕ: живая позиция каждого отсчёта совпадает (с точностью до
#      интерполяции между соседними ключами) с одним из 201 ключа его трека, посчитанного офлайн.
#      Именно этот пункт отличает «что-то шевелится» от «шевелится как в оригинале».
#
# ЗАПУСК (нужен тик редактора → --exec-cmds):
#   python Tools/run_headless_safe.py Tools/asda_backdrop_anim_live_check.py --exec-cmds \
#          --log @@ASTORY_DEV_ROOT_FWD@@/Reports/backdrop_anim_live.log
import json
import os

LEVEL = "/Game/Map/Login"
DA_PATH = "/Game/AsdaAssets/Config/DA_LoginBackdropAnim"
ANIM_JSON = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         "..", "RawAssets", "backdrop", "backdrop_anim.json")
ELEM_TAG = "AsdaBackdropElem"
ANIMATOR_LABEL = "LoginBackdropAnimator"

WARMUP = 120        # тиков до первого отсчёта (PIE должен подняться)
GAP = 45            # тиков между отсчётами
SAMPLES = 5         # сколько отсчётов снять при ВКЛЮЧЕННОЙ анимации
ROLLBACK_SAMPLES = 2  # сколько отсчётов снять после ключа отката `asda.LoginBackdropAnim 0`
NUM_PROBE = 8       # сколько контрольных элементов отслеживать
MAX_TICKS = 20000

# Допуск совпадения с офлайн-треком, см. Сравниваем с ЛОМАНОЙ трека (отрезок между соседними
# ключами), а не с самими ключами: живой кадр почти всегда попадает МЕЖДУ ключами, и расстояние до
# ближайшего ключа там законно достигает полушага (у быстрых элементов это единицы сантиметров —
# первый прогон 24.08.2026 на этом дал ложное «не совпадает»). До ломаной же расстояние ~0.
# Сцена ~500 000 см, экспорт округлён до 0.001 см → 0.2 см с запасом.
TOL_CM = 0.2


def log(m):
    import unreal
    unreal.log("[задник-анимация-живой] " + str(m))


def main():
    import unreal

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    EAL = unreal.EditorAssetLibrary

    # ---- 1. ассеты ----
    if not EAL.does_asset_exist(DA_PATH):
        log("!! НЕТ ассета %s — прогоните asda_build_backdrop_anim.py" % DA_PATH)
        log("ЗАВЕРШЕНО")
        return
    da = EAL.load_asset(DA_PATH)
    tracks = da.get_editor_property("tracks")
    num_keys = da.get_editor_property("num_keys")
    duration = da.get_editor_property("duration")
    fps = da.get_editor_property("frame_rate")
    nonempty = sum(1 for t in tracks if len(t.get_editor_property("keys")) >= num_keys * 7)
    log("DataAsset: клип=%s треков=%d (непустых=%d) ключей=%d fps=%.0f длительность=%.4f"
        % (da.get_editor_property("source_clip"), len(tracks), nonempty, num_keys, fps, duration))
    log("  неподвижных по оригиналу=%d, без кости=%d"
        % (len(da.get_editor_property("static_elements")), len(da.get_editor_property("unbound_elements"))))
    if nonempty == 0:
        log("!! ВСЕ ТРЕКИ ПУСТЫ")
        log("ЗАВЕРШЕНО")
        return

    # ---- офлайн-эталон траекторий (из JSON экспортёра) ----
    with open(ANIM_JSON, "r", encoding="utf-8") as f:
        j = json.load(f)
    off = {t["element"]: t["keys"] for t in j["tracks"]}

    # ---- 2. базовые (авторские) трансформы — читаем ДО запуска PIE, из редакторского мира ----
    unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
    base = {}
    for a in eas.get_all_level_actors():
        tags = [str(t) for t in a.get_editor_property("tags")]
        if ELEM_TAG not in tags:
            continue
        nm = next((t for t in tags if t != ELEM_TAG), None)
        rc = a.get_editor_property("root_component")
        if nm and rc:
            loc = rc.get_editor_property("relative_location")
            mob = str(rc.get_editor_property("mobility")).split(".")[-1]
            base[nm] = (loc, mob)
    log("в уровне помечено элементов: %d" % len(base))
    movable = sum(1 for v in base.values() if "MOVABLE" in v[1].upper())
    log("из них Movable: %d (остальные UE двигать не даст)" % movable)

    probe = [n for n in sorted(base.keys()) if n in off][:NUM_PROBE]
    if not probe:
        log("!! ни один помеченный элемент не имеет трека")
        log("ЗАВЕРШЕНО")
        return
    log("контрольные элементы: %s" % ", ".join(probe))

    # ---- 3. PIE ----
    les.editor_request_begin_play()
    log("PIE запущен")

    st = {"tick": 0, "n": 0, "next_at": WARMUP, "handle": None, "hist": {p: [] for p in probe},
          "bound": -1,
          # фаза отката: после основных отсчётов выключаем анимацию cvar-ключом и проверяем,
          # что элементы ВЕРНУЛИСЬ на авторские места и больше не шевелятся
          "rolled": False, "hist_off": {p: [] for p in probe}}

    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    world = PH.make_game_world(ues)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 5 стр.)

    def sample():
        w = world()
        if not w:
            log("!! нет игрового мира")
            return
        if st["bound"] < 0:
            for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.AsdaBackdropAnimator):
                st["bound"] = a.get_bound_element_count()
                # ★ UE-python отбрасывает префикс `b` у bool-свойств: bEnabled → enabled.
                log("дирижёр найден: привязано элементов = %d, bEnabled=%s, PlayRate=%.2f, Amplitude=%.2f"
                    % (st["bound"], a.get_editor_property("enabled"),
                       a.get_editor_property("play_rate"), a.get_editor_property("amplitude")))
        dest = st["hist_off"] if st["rolled"] else st["hist"]
        found = 0
        for a in unreal.GameplayStatics.get_all_actors_with_tag(w, unreal.Name(ELEM_TAG)):
            tags = [str(t) for t in a.get_editor_property("tags")]
            nm = next((t for t in tags if t != ELEM_TAG), None)
            if nm in dest:
                rc = a.get_editor_property("root_component")
                loc = rc.get_editor_property("relative_location")
                dest[nm].append((loc.x, loc.y, loc.z))
                found += 1
        st["n"] += 1
        log("отсчёт %d/%d%s: снято элементов %d"
            % (st["n"], SAMPLES + ROLLBACK_SAMPLES, " (откат)" if st["rolled"] else "", found))

    def rollback():
        """Ключ отката: cvar `asda.LoginBackdropAnim 0` должен вернуть авторскую статику."""
        w = world()
        if not w:
            return
        unreal.SystemLibrary.execute_console_command(w, "asda.LoginBackdropAnim 0")
        st["rolled"] = True
        log("→ asda.LoginBackdropAnim 0 (проверка ключа отката)")

    def report():
        log("── ИТОГ ЖИВОГО ПРОГОНА ──")
        moved = 0
        matched = 0
        for nm in probe:
            hist = st["hist"].get(nm) or []
            if len(hist) < 2:
                log("%-22s ОТСЧЁТОВ НЕТ" % nm)
                continue
            b = base[nm][0]
            keys = off[nm]
            # смещение между отсчётами (доказательство «двигается»)
            span = max(max(abs(h[i] - g[i]) for i in range(3)) for h in hist for g in hist)
            # совпадение с офлайн-траекторией (доказательство «двигается как в оригинале»).
            # Трек — ЛОМАНАЯ: дирижёр линейно интерполирует позицию между соседними ключами,
            # поэтому живая точка обязана лежать НА ОТРЕЗКЕ [P_k, P_k+1], а не в самом ключе.
            pts = [(b.x + keys[k * 7], b.y + keys[k * 7 + 1], b.z + keys[k * 7 + 2])
                   for k in range(num_keys)]

            def dist_to_seg(p, a0, a1):
                """Евклидово расстояние от точки до отрезка [a0, a1]."""
                vx, vy, vz = a1[0] - a0[0], a1[1] - a0[1], a1[2] - a0[2]
                wx, wy, wz = p[0] - a0[0], p[1] - a0[1], p[2] - a0[2]
                vv = vx * vx + vy * vy + vz * vz
                t = 0.0 if vv <= 1e-12 else max(0.0, min(1.0, (wx * vx + wy * vy + wz * vz) / vv))
                dx, dy, dz = wx - t * vx, wy - t * vy, wz - t * vz
                return (dx * dx + dy * dy + dz * dz) ** 0.5

            best_all = []
            for h in hist:
                bd, bk = 1e30, -1
                for k in range(num_keys - 1):
                    d = dist_to_seg(h, pts[k], pts[k + 1])
                    if d < bd:
                        bd, bk = d, k
                best_all.append((bd, bk))
            worst = max(d for d, _ in best_all)
            kidx = [k for _, k in best_all]
            ok_move = span > 0.5
            ok_match = worst <= TOL_CM
            moved += 1 if ok_move else 0
            matched += 1 if ok_match else 0
            log("%-22s размах=%8.2f см  макс.откл.от трека=%6.3f см  ближайшие ключи=%s  %s"
                % (nm, span, worst, kidx,
                   ("ДВИЖЕТСЯ+СОВПАДАЕТ" if (ok_move and ok_match)
                    else ("СТОИТ" if not ok_move else "НЕ СОВПАДАЕТ С ТРЕКОМ"))))
        log("движутся: %d/%d, совпадают с оригинальным треком: %d/%d"
            % (moved, len(probe), matched, len(probe)))
        # ---- ключ отката: после `asda.LoginBackdropAnim 0` всё обязано стоять НА АВТОРСКИХ местах ----
        restored = 0
        for nm in probe:
            h = st["hist_off"].get(nm) or []
            if len(h) < 2:
                continue
            b = base[nm][0]
            dev = max(max(abs(p[0] - b.x), abs(p[1] - b.y), abs(p[2] - b.z)) for p in h)
            jitter = max(max(abs(p[i] - q[i]) for i in range(3)) for p in h for q in h)
            if dev <= TOL_CM and jitter <= TOL_CM:
                restored += 1
            else:
                log("%-22s ОТКАТ НЕ СРАБОТАЛ: откл.от авторской=%.3f см, дрожание=%.3f см"
                    % (nm, dev, jitter))
        log("после `asda.LoginBackdropAnim 0` вернулись на авторские места и замерли: %d/%d"
            % (restored, len(probe)))
        log("привязано дирижёром: %d (в данных треков %d)" % (st["bound"], len(tracks)))
        verdict = (st["bound"] > 0 and moved == len(probe) and matched == len(probe)
                   and restored == len(probe))
        log("ВЕРДИКТ: %s" % ("анимация задника РАБОТАЕТ и соответствует оригиналу"
                             if verdict else "ЕСТЬ ПРОБЛЕМА — см. строки выше"))

    def stop():
        if st["handle"]:
            unreal.unregister_slate_post_tick_callback(st["handle"])
            st["handle"] = None
        report()
        try:
            les.editor_request_end_play()
        except Exception:
            pass
        log("ЗАВЕРШЕНО")

    def tick(dt):
        st["tick"] += 1
        if st["tick"] > MAX_TICKS:
            log("!! таймаут")
            stop()
            return
        if st["tick"] < st["next_at"]:
            return
        if st["n"] >= SAMPLES + ROLLBACK_SAMPLES:
            stop()
            return
        try:
            if st["n"] == SAMPLES and not st["rolled"]:
                # ★ ГРАБЛЯ (первый прогон 24.08.2026): снимать отсчёт В ТОМ ЖЕ тике, где подана
                #   консольная команда, нельзя — Tick дирижёра этого кадра уже отработал со старым
                #   значением cvar, и первый «откатный» отсчёт показывал ещё анимированную позу
                #   (ложное «откат не сработал»). Пропускаем интервал.
                rollback()
                st["next_at"] = st["tick"] + GAP
                return
            sample()
        except Exception as e:
            log("!! отсчёт: %s" % e)
        st["next_at"] = st["tick"] + GAP

    st["handle"] = unreal.register_slate_post_tick_callback(tick)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
