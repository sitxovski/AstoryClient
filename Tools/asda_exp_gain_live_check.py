# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_exp_gain_live_check.py — ЖИВАЯ (PIE) проверка ПОКАЗА ПОЛУЧЕНИЯ ОПЫТА (опкод 4038).
#
# ★★★ ЗАЧЕМ ИМЕННО ЖИВАЯ (закон `false-negatives-method`). Всплывающая надпись — это Slate-виджет
#   в канвасе вьюпорта, который живёт 1200 мс и строится ТИКОМ подсистемы: офлайн-дамп ассетов
#   её не увидит вовсе и даст ложное «не работает». Кроме того, ровно здесь пряталась исходная
#   ошибка: стили 0x13/0x14 не были заведены в `StyleParams`, и канал МОЛЧА гасил спавн.
#
# ЧТО ПРОВЕРЯЕТСЯ (каждый пункт даёт строку-доказательство в логе или кадр):
#   1. ветка B/C (обычный опыт, убийство моба): надпись «ОПЫТ+» + число ПРАВЕЕ персонажа
#      — `asda.Hud.TestExpGain 12345 0` → кадр `exp_gain_normal.png`;
#   2. ветка A (наградной опыт, квест): та же пара, смещённая влево на 150 px (эталонная
#      асимметрия) — `asda.Hud.TestExpGain 217 1` → кадр `exp_gain_reward.png`;
#   3. строки чата: `asda.Msg.Verbose 1` печатает КАЖДУЮ отправку с текстом и цветом —
#      ожидаем «Progress → Системный чат … «Получен опыт: N.»» и, для награды, вторую
#      строку «Получен опыт. Получены деньги.» цветом FF8000;
#   4. счётчики маршрутизатора: `asda.Msg.Dump` — категория Progress должна показать отправки.
#
# ПАВН. Сервера здесь нет, поэтому `UAsdaWorldSubsystem` персонажа не спавнит и `PC->GetPawn()`
#   пуст — а надпись рисуется НАД АКТОРОМ. Ставим обычный `DefaultPawn` и вселяем в него
#   контроллер: это ровно тот объект, который в бою вернёт `GetPawn()`. В игровой код ради теста
#   не добавлено ни строки.
#
# КАМЕРА. `PlayerCameraManager` без павна остаётся в (0,0,0), а перенос контроллера вид не двигает
#   (грабля из `asda_target_ring_live_check.py`). Поэтому в РЕДАКТОРСКИЙ уровень до старта PIE
#   кладётся `CameraActor`; PIE дублирует его, в игре он становится `SetViewTarget`. Павн поднят
#   высоко над картой: фон = небо, надпись читается без помех.
#
# СНИМОК. ★ ТОЛЬКО `shot showui` (= `FScreenshotRequest::RequestScreenshot(bShowUI = true)`):
#   `HighResShot` слой Slate в PNG НЕ пишет, а здесь проверяется именно он.
#
# ЗАПУСК: Tools\run_headless_exp_gain.bat
#   ★ Нужен ТИК редактора (`-ExecCmds="py …"`) и РЕНДЕР (без `-NullRHI`).
import glob
import os
import shutil

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/exp_gain_live"
SS_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
LEVEL = "/Game/Map/alpen/alpen"
PROBE_LABEL = "ExpProbeCam"
PAWN_LABEL = "ExpProbePawn"

WARMUP = 300
TRAVEL_SETTLE = 400   # переезд уровня: стриминг + BeginPlay мира
SETTLE = 40           # обычный шаг
SHORT = 6             # ★ между «показать надпись» и «снять кадр»: надпись живёт 1200 мс,
                      #   пик масштаба на 50..90 мс — снимаем сразу, пока она крупная
MAX_TICKS = 30000

PAWN_LOC = (0.0, 0.0, 30000.0)   # высоко над картой: чистое небо за надписью


def log(m):
    import unreal
    unreal.log("[опыт-живой] " + str(m))


def main():
    import unreal

    if os.path.isdir(SS_DIR):
        shutil.rmtree(SS_DIR, ignore_errors=True)
    if os.path.isdir(OUT_DIR):
        shutil.rmtree(OUT_DIR, ignore_errors=True)
    os.makedirs(OUT_DIR)

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
    log("уровень %s загружен" % LEVEL)
    probe = eas.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    probe.set_actor_label(PROBE_LABEL)
    log("камера-зонд «%s» добавлена в редакторский уровень" % PROBE_LABEL)
    # ★ ПАВН ТОЖЕ КЛАДЁТСЯ В РЕДАКТОРСКИЙ УРОВЕНЬ (тот же приём, что и камера-зонд).
    #   Спавнить его прямо в PIE-мир из Python НЕЛЬЗЯ: `EditorActorSubsystem` работает с
    #   РЕДАКТОРСКИМ миром, а `GameplayStatics.begin_deferred_actor_spawn_from_class` в Python
    #   не выставлен (BlueprintInternalUseOnly) — проверено прогоном (AttributeError).
    pawn_probe = eas.spawn_actor_from_class(unreal.DefaultPawn, unreal.Vector(*PAWN_LOC),
                                            unreal.Rotator(0, 0, 0))
    pawn_probe.set_actor_label(PAWN_LABEL)
    log("павн-зонд «%s» добавлен в редакторский уровень" % PAWN_LABEL)
    les.editor_request_begin_play()
    log("PIE запущен")

    st = {"tick": 0, "step": -1, "next_at": WARMUP, "handle": None, "busy": False,
          "shots": [], "pawn": None}

    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    world = PH.make_game_world(ues)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 5 стр.)

    cmd = PH.make_console_cmd(world, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 6 стр.)

    take_shot = PH.make_take_shot(cmd, st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 3 стр.)

    harvest = PH.make_harvest(SS_DIR, OUT_DIR, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 10 стр.)

    probe_cam = PH.make_probe_camera(world)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 7 стр.)

    def spawn_and_possess():
        """Найти павн-зонд в игровом мире, вселить контроллер, навести камеру.

        Это ровно тот объект, который в бою вернёт `PC->GetPawn()` — то есть проверяется
        настоящий путь показа, а не отдельная тестовая ветка.
        """
        w = world()
        pc = unreal.GameplayStatics.get_player_controller(w, 0)
        if not pc:
            raise RuntimeError("нет AsdaPlayerController — режим не поднялся")
        loc = unreal.Vector(*PAWN_LOC)
        pawn = None
        for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.DefaultPawn):
            pawn = a
            break
        if not pawn:
            raise RuntimeError("павн-зонд не продублировался в PIE-мир")
        # Позицию дожимаем явно: она — единственное, от чего зависит проекция надписи в экран.
        pawn.set_actor_location(loc, False, False)
        st["pawn"] = pawn
        pc.possess(pawn)
        log("павн %s в (%.0f,%.0f,%.0f) вселён" % (pawn.get_name(), loc.x, loc.y, loc.z))
        cam = probe_cam()
        if cam:
            eye = unreal.Vector(loc.x - 600.0, loc.y - 600.0, loc.z + 150.0)
            rot = unreal.MathLibrary.find_look_at_rotation(eye, loc)
            cam.set_actor_location_and_rotation(eye, rot, False, False)
            pc.set_view_target_with_blend(cam, 0.0)
            log("камера-зонд: (%.0f,%.0f,%.0f) → павн" % (eye.x, eye.y, eye.z))
        else:
            log("⚠ камеры-зонда в игровом мире нет — кадр будет от начала координат")

    def dump_floating():
        """Сколько надписей живёт прямо сейчас (доказательство, что спавн НЕ погашен стилем)."""
        w = world()
        pc = unreal.GameplayStatics.get_player_controller(w, 0) if w else None
        log("павн контроллера: %s" % (pc.get_controlled_pawn().get_name()
                                      if pc and pc.get_controlled_pawn() else "НЕТ"))

    # ── план: (действие, пауза после него в тиках) ──────────────────────────────────────────────
    PLAN = [
        # ★ У карт мира в порту нет своего GameMode: PIE поднимает GameModeBase, и
        #   AAsdaPlayerController не создаётся. Переезжаем в тот же уровень с ЯВНЫМ режимом.
        (lambda: cmd("open /Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode"), TRAVEL_SETTLE),
        (lambda: cmd("asda.Msg.Verbose 1"), 5),
        (spawn_and_possess, SETTLE),
        (dump_floating, 5),
        # 1) ОБЫЧНЫЙ опыт (убийство моба/рыбалка/крафт): надпись правее персонажа
        (lambda: cmd("asda.Hud.TestExpGain 12345 0"), SHORT),
        (lambda: take_shot("exp_gain_normal"), SETTLE),
        (lambda: harvest("exp_gain_normal"), 5),
        # 2) НАГРАДНОЙ опыт (квест/кампвар): эталонное смещение на 150 px влево + вторая строка чата
        (lambda: cmd("asda.Hud.TestExpGain 217 1"), SHORT),
        (lambda: take_shot("exp_gain_reward"), SETTLE),
        (lambda: harvest("exp_gain_reward"), 5),
        # 3) ветка «сущности нет» (renderId, которого в мире не существует): текста нет, лог есть
        (lambda: cmd("asda.Hud.TestExpGain 42 0 9999"), SETTLE),
        # 4) счётчики маршрутизатора: категория Progress обязана показать отправки
        (lambda: cmd("asda.Msg.Dump"), SETTLE),
        # 5) процент опыта в HUD: полный дамп контролов бара + расчёт
        (lambda: cmd("asda.Hud.TestExp 6 1025"), SETTLE),
        (lambda: take_shot("exp_bar_percent"), SETTLE),
        (lambda: harvest("exp_bar_percent"), 5),
    ]

    def finish():
        if st["handle"]:
            unreal.unregister_slate_post_tick_callback(st["handle"])
            st["handle"] = None
        try:
            les.editor_request_end_play()
        except Exception:
            pass
        for a in (probe, pawn_probe):   # уровень НЕ сохраняем, зонды убираем
            try:
                eas.destroy_actor(a)
            except Exception:
                pass
        log("=== готово, снимков запрошено %d ===" % len(st["shots"]))
        unreal.SystemLibrary.quit_editor()

    def on_tick(_dt):
        if st["busy"]:
            return
        st["busy"] = True
        try:
            st["tick"] += 1
            if st["tick"] > MAX_TICKS:
                unreal.log_error("[опыт-живой] предел тиков")
                finish()
                return
            if st["tick"] < st["next_at"]:
                return
            st["step"] += 1
            if st["step"] >= len(PLAN):
                finish()
                return
            fn, wait = PLAN[st["step"]]
            fn()
            st["next_at"] = st["tick"] + wait
        except Exception:
            import traceback
            unreal.log_error("[опыт-живой] ошибка:\n" + traceback.format_exc())
            finish()
        finally:
            st["busy"] = False

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    log("tick-callback зарегистрирован")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
