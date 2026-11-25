# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_glow_check.py — ЖИВАЯ проверка пункта настроек «уровень свечения» (LEVEL OF GLOW 0..4).
#
# Что делает: в запущенной ИГРЕ (-game, путь игрока) перебирает уровни 0…4 ключом `asda.Video.Glow N`
#   и после каждого печатает ФАКТИЧЕСКИЕ ручки ореола со ВСЕХ носителей пост-обработки мира —
#   карт-объёма `PP_<карта>` и служебного `PP_Glow` (приоритет 9000, его ставит C++). Итог — таблица
#   «уровень → интенсивность/порог, которые видит рендер» (побеждает включённый unbound-объём с
#   БОЛЬШИМ приоритетом). Печатается и авто-экспозиция — доказательство, что на сцене логина она
#   осталась ЖИВОЙ (0.03…8.0) и мы её не тронули.
#
# Почему так, а не «скрипт отрапортовал»: значения читаются из АКТОРОВ живого мира, а не из ассета и
#   не из наших же переменных (закон diagnostic-must-use-real-path); путь — игрока, не PIE
#   (закон ui-check-game-path-geometry).
#
# Запуск (одна карта за прогон — параллельные экземпляры игры мешают друг другу):
#   python Tools/set0_run_game.py Tools/asda_glow_check.py --map /Game/Map/alpen/alpen \
#          --marker "GLOW-CHECK-FINISHED" --res 1280x720 --timeout 900 --idle 180 \
#          --log @@ASTORY_DEV_ROOT_FWD@@/Reports/glow_check_alpen.log
# Переменные окружения:
#   ASDA_GLOW_LEVELS="0,1,2,3,4" — какие уровни перебирать (по умолчанию все пять)
import os
import unreal

MAP = os.environ.get("ASDA_GLOW_MAP", "")            # пусто → мир берём из первого попавшегося игрового
LEVELS = [int(x) for x in os.environ.get("ASDA_GLOW_LEVELS", "0,1,2,3,4").split(",")]
MARKER = "GLOW-CHECK-FINISHED"                       # ASCII-маркер успеха для set0_run_game.py
KNOBS = ["bloom_intensity", "bloom_threshold", "bloom_size_scale",
         "lens_flare_intensity", "lens_flare_threshold",
         "auto_exposure_min_brightness", "auto_exposure_max_brightness", "auto_exposure_bias"]


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    st = {"w": None, "q": [], "n": 0}
    rows = []

    def log(m):
        unreal.log("[GLOW] %s" % m)

    def cc(cmd):
        unreal.SystemLibrary.execute_console_command(st["w"], cmd)

    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    after = PH.make_after_frames(st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 2 стр.)

    run_queue = PH.make_frame_queue_runner(st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 8 стр.)

    def find_world():
        """Игровой мир прогона. Имя карты знаем не всегда (сцена логина грузится сама) — берём
        мир любого актора, это и есть тот мир, который рисуется."""
        cands = [MAP] if MAP else []
        if not cands:
            # ★ Карту берём из КОМАНДНОЙ СТРОКИ прогона: `unreal.EditorLevelLibrary.get_game_world()`
            #   здесь звать нельзя — в режиме -game он валит процесс с EXCEPTION_ACCESS_VIOLATION
            #   (прогон логина 09.09.2026, стек EditorScriptingUtilities.dll).
            for tok in unreal.SystemLibrary.get_command_line().split():
                if tok.startswith("/Game/"):
                    cands.append(tok)
        for m in cands:
            base = m.rsplit("/", 1)[-1]
            for path in (m + "." + base, m):
                for fn in (unreal.find_object, unreal.load_object):
                    try:
                        o = fn(None, path)
                        if o and isinstance(o, unreal.World):
                            return o
                    except Exception:
                        pass
        return None

    def carriers():
        """ВСЕ носители пост-настроек мира: PP-объёмы И компоненты камер.

        Камеры печатаем ВСЕГДА, а не «только когда объёмов нет»: пост-настройки камеры движок
        накладывает ПОСЛЕ объёмов, поэтому переопределяющая ореол камера побила бы наш PP_Glow —
        это надо видеть в логе, а не предполагать. Приоритет камеры печатается как −1 (у неё его нет)."""
        out = []
        for v in unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.PostProcessVolume):
            out.append((v.get_actor_label() or v.get_name(), v, "settings",
                        float(v.get_editor_property("priority")),
                        bool(v.get_editor_property("unbound")),
                        float(v.get_editor_property("blend_weight")),
                        bool(v.get_editor_property("enabled"))))
        for a in unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.Actor):
            try:
                comp = a.get_component_by_class(unreal.CameraComponent)
            except Exception:
                comp = None
            if comp is not None:
                out.append(("камера " + a.get_name(), comp, "post_process_settings", -1.0, False,
                            float(comp.get_editor_property("post_process_blend_weight")), True))
        return out

    def snap(level):
        """Ручки со всех носителей + «что возьмёт рендер» = включённый носитель с макс. приоритетом."""
        best = None
        for label, holder, prop, prio, unb, bw, en in carriers():
            ps = holder.get_editor_property(prop)
            vals = {}
            for k in KNOBS:
                try:
                    vals[k] = float(ps.get_editor_property(k))
                except Exception:
                    vals[k] = None
                try:
                    vals[k + "*"] = bool(ps.get_editor_property("override_" + k))
                except Exception:
                    vals[k + "*"] = False
            log("уровень %d | %-24s приоритет=%7.0f unbound=%d вкл=%d вес=%.2f | %s"
                % (level, label, prio, unb, en, bw,
                   " ".join("%s=%s%s" % (k, vals[k], "*" if vals.get(k + "*") else "") for k in KNOBS)))
            # Камеру считаем «сильнее любого объёма»: движок накладывает её пост-настройки ПОСЛЕ
            #   объёмов (ULocalPlayer::CalcSceneView → OverridePostProcessSettings). Отсюда +1e9.
            eff = (prio if prop == "settings" else 1e9)
            if en and bw > 0.0 and vals.get("bloom_intensity*") and (best is None or eff > best[0]):
                best = (eff, label, vals["bloom_intensity"], vals["bloom_threshold"])
        q = unreal.SystemLibrary.get_console_variable_int_value("r.BloomQuality")
        if best:
            rows.append((level, best[1], best[2], best[3], q))
            log("уровень %d ⇒ РЕНДЕР БЕРЁТ: %s интенсивность=%.3f порог=%.3f, r.BloomQuality=%d"
                % (level, best[1], best[2], best[3], q))
        else:
            rows.append((level, "—", float("nan"), float("nan"), q))
            unreal.log_error("[GLOW] уровень %d: ореол не переопределён ни одним носителем" % level)

    def finish():
        log("=== ТАБЛИЦА «уровень свечения → что видит рендер» (мир %s) ===" % st["w"].get_name())
        for lv, who, bi, bt, q in rows:
            log("  %d → интенсивность %.3f, порог %.3f, r.BloomQuality=%d  (носитель %s)"
                % (lv, bi, bt, q, who))
        log(MARKER)
        cc("quit")

    def phase_init():
        st["w"] = find_world()
        if st["w"] is None:
            unreal.log_error("[GLOW] МИР НЕ НАЙДЕН")
            unreal.SystemLibrary.execute_console_command(None, "quit")
            return
        log("мир: %s" % st["w"].get_name())
        # Снимок ДО перебора — состояние, с которым игра стартовала (сохранённый уровень игрока).
        after(1, lambda: snap(-1))
        for lv in LEVELS:
            after(5, lambda v=lv: cc("asda.Video.Glow %d" % v))
            after(5, lambda v=lv: snap(v))
        after(5, finish)

    def tick(dt):
        st["n"] += 1
        if st["n"] == 1:
            phase_init()
            return
        if st["w"] is None:
            return
        try:
            run_queue()
        except Exception as ex:
            import traceback
            unreal.log_error("[GLOW] ошибка шага: %s\n%s" % (ex, traceback.format_exc()))

    unreal.register_slate_post_tick_callback(tick)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
