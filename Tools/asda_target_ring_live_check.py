# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_target_ring_live_check.py — ЖИВАЯ (PIE) проверка КОЛЬЦА ПОД ВЫБРАННОЙ ЦЕЛЬЮ.
#
# ★★★ ЗАЧЕМ ИМЕННО ЖИВАЯ (закон `false-negatives-method`). Кольцо — это `AAsdaEffectActor`:
#   его геометрия строится ТИКОМ (процедурный меш, частицы/квад в мировых координатах). В
#   редакторском мире актор не тикает ⇒ офлайн-снимок показал бы пустоту и дал ложное «не работает».
#   Поэтому уровень поднимается в PIE, и кадр снимается из ЖИВОГО мира.
#
# ЧТО ПРОВЕРЯЕТСЯ (каждый пункт даёт строку-доказательство в логе):
#   1. кольцо ставится на МИРНОГО (NPC)  → эффект `friendlyselect`  (зелёное)
#   2. кольцо ставится на ВРАЖДЕБНОГО (моб) → эффект `unfriendlyselect` (красное)
#   3. снятие выделения (`asda.Target.RingNearest off`) кольцо ГАСИТ
#   4. `asda.Target.Ring 0` — кольцо не появляется вовсе (откат к 1:1 нашей сборке)
#   5. кадры: `ring_npc.png`, `ring_mob.png`, `ring_cleared.png`
#   6. правило клика по NPC (`asda.Npc.ClickRule`): дефолт «окно только по двойному», а
#      ключ отката `asda.Npc.OpenOnSingleClick 1` возвращает эталонное «с первого клика»
#   ★★★ 7. АНИМАЦИЯ ИГРАЕТ ОДИН РАЗ И КОЛЬЦО ОСТАЁТСЯ ВИДИМЫМ (задача 2026-08-21,
#      `Reports/npc_target_ring_fix2.md`): эффект создаётся ОДИН раз на выделение,
#      `endMode` в ассете = 3 (фриз одиночного квада на конечном узле), таймер эмиттера
#      упирается в период и больше не растёт, а САМА ГЕОМЕТРИЯ ПРОДОЛЖАЕТ СТРОИТЬСЯ:
#      команда `asda.Target.RingNearest dump` печатает альфу текущего кадра, полу-экстент и
#      радиус реально построенной секции — «ВИДНО / НЕ ВИДНО» доказывается фактом, а не словом.
#      Тот же дамп повторяется СИЛЬНО ПОЗЖЕ: объект обязан быть ТЕМ ЖЕ (не пересоздан в тике)
#      и по-прежнему видимым.
#   ★★★ 8. ВЫСОТА У ПОДОШВЫ: дамп печатает `Z` цели, `Z` подошвы и высоту кольца НАД ПОДОШВОЙ.
#      Проверяется на ТРЁХ целях с РАЗНОЙ посадкой модели: ближайший NPC (`opara` — модель
#      уходит на 54.8 под корень, срабатывает предохранитель), NPC-человек (`soldier` — низ
#      модели на 5.8 ниже корня, кольцо садится на подошву) и моб (3.0).
#   ★★★ 9. СМЕНА ЦЕЛИ: NPC → моб → NPC. На каждом шаге в мире РОВНО ОДНО кольцо, объект НОВЫЙ
#      (значит, эффект действительно переигрался), у прежней цели кольца не осталось.
#
# ЗАМЕДЛЕНИЯ БОЛЬШЕ НЕТ — И ЭТО СЛЕДСТВИЕ ПРАВКИ. Пока кольцо было разовым (жило 0.0672 + 0.20 =
#   0.267 с), кадр приходилось ловить глобальным замедлением ×10 и ожиданием ПО ВОЗРАСТУ актора,
#   иначе `HighResShot` снимал пустую траву (ложное «не работает», [[false-negatives-method]]).
#   Теперь кольцо ЗАМИРАЕТ на конечном диаметре и остаётся на земле, поэтому кадр снимается на
#   НОРМАЛЬНОЙ скорости — но ПОСЛЕ того, как проходка гарантированно закончилась: ожидание идёт
#   по ФАКТУ (`flash_done`: возраст кольца превысил `StartDelay + life`), а не по счёту тиков.
#
# КАМЕРА. ★ ЗАМЕРЕНО, А НЕ УГАДАНО: сервера нет ⇒ павна нет, и `PlayerCameraManager` остаётся в
#   (0,0,0) — перенос САМОГО контроллера (`set_actor_location`) вид НЕ двигает (первый прогон:
#   «контроллер в (-23850,12150,1296), камера-менеджер в (0,0,0)» и кадр с пустым небом).
#   Поэтому в РЕДАКТОРСКИЙ уровень до старта PIE кладётся обычный `CameraActor` (PIE дублирует
#   уровень вместе с ним), в игре он становится `SetViewTarget`, а после прогона удаляется —
#   уровень НЕ сохраняется. В игровой код «тестовой камеры» не добавлено ни строки.
#
# СНИМОК. ★ `HighResShot`, а НЕ `shot showui`: `shot` идёт через глобальный `FScreenshotRequest`,
#   и headless-редактор исполняет его для СВОЕГО главного окна (первый прогон дал кадр редактора
#   с деревом акторов). `HighResShot` обрабатывает `UGameViewportClient` того мира, в котором
#   команда выполнена, то есть PIE-вьюпорт.
#
# ЗАПУСК: Tools\run_headless_target_ring.bat
#   ★ Нужен ТИК редактора (`-ExecCmds="py …"`, а НЕ `-ExecutePythonScript`): второй завершает
#     редактор сразу после скрипта, и машина состояний на `register_slate_post_tick_callback`
#     не сделает ни одного шага. ★ Нужен РЕНДЕР (без `-NullRHI`) — иначе кадров не будет.
import glob
import os
import shutil

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/target_ring_live"
SS_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
LEVEL = "/Game/Map/alpen/alpen"
PROBE_LABEL = "RingProbeCam"   # метка камеры-зонда (см. блок КАМЕРА в шапке)

WARMUP = 300
TRAVEL_SETTLE = 400   # переезд уровня: стриминг + BeginPlay мира
SETTLE = 90           # кольцу нужно несколько кадров тика, чтобы построить меш
LATE = 300            # ★ «сильно позже»: доказать, что эффект ПОГАШЕН, а не крутится
MAX_TICKS = 40000


def log(m):
    import unreal
    unreal.log("[кольцо-живое] " + str(m))


def main():
    import unreal

    if os.path.isdir(SS_DIR):
        shutil.rmtree(SS_DIR, ignore_errors=True)
    if os.path.isdir(OUT_DIR):
        shutil.rmtree(OUT_DIR, ignore_errors=True)
    os.makedirs(OUT_DIR)

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

    unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
    log("уровень %s загружен" % LEVEL)
    # ★ Камера-зонд кладётся ДО старта PIE: PIE дублирует редакторский уровень, и в игровом мире
    #   появится её копия. Уровень не сохраняется, актор удаляется в finish().
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    probe = eas.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    probe.set_actor_label(PROBE_LABEL)
    log("камера-зонд «%s» добавлена в редакторский уровень" % PROBE_LABEL)
    les.editor_request_begin_play()
    log("PIE запущен")

    st = {"tick": 0, "step": -1, "next_at": WARMUP, "handle": None, "busy": False,
          "shots": [], "gate": None}

    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    world = PH.make_game_world(ues)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 5 стр.)

    cmd = PH.make_console_cmd(world, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 6 стр.)

    def take_shot(tag):
        # ★ `HighResShot`, А НЕ `shot showui`. `shot` идёт через `FScreenshotRequest` — глобальный
        #   запрос, который headless-редактор исполняет для СВОЕГО главного окна (проверено: первый
        #   прогон дал кадр редактора с пустым небом, а не вид игровой камеры). `HighResShot`
        #   обрабатывает `UGameViewportClient` ТОГО мира, в котором выполнена команда, — то есть
        #   PIE-вьюпорт. Интерфейс нам здесь не нужен: проверяется трёхмерное кольцо.
        cmd("HighResShot 1280x720")
        st["shots"].append(tag)

    def dump_camera():
        w = world()
        pc = unreal.GameplayStatics.get_player_controller(w, 0)
        cam = unreal.GameplayStatics.get_player_camera_manager(w, 0)
        if pc:
            l = pc.get_actor_location()
            log("контроллер в (%.0f,%.0f,%.0f)" % (l.x, l.y, l.z))
        if cam:
            l = cam.get_camera_location()
            r = cam.get_camera_rotation()
            log("камера-менеджер в (%.0f,%.0f,%.0f), поворот (%.0f,%.0f,%.0f)"
                % (l.x, l.y, l.z, r.pitch, r.yaw, r.roll))

    harvest = PH.make_harvest(SS_DIR, OUT_DIR, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 10 стр.)

    def ring_actors():
        """Живые эффект-акторы кольца в PIE-мире: [(имя .jeb, позиция, имя ОБЪЕКТА)].

        ★ Имя объекта (`get_name()`) нужно для пункта 8: если после смены цели оно ДРУГОЕ —
          эффект действительно пересоздан и переигран, а не «остался старый».
        """
        w = world()
        out = []
        if not w:
            return out
        for a in unreal.GameplayStatics.get_all_actors_with_tag(w, "TargetRingFx"):
            try:
                name = a.get_editor_property("effect_name")
            except Exception:
                name = "?"
            loc = a.get_actor_location()
            out.append((str(name), (loc.x, loc.y, loc.z), a.get_name()))
        return out

    def dump_rings(tag):
        rs = ring_actors()
        log("[%s] колец в мире: %d" % (tag, len(rs)))
        for name, loc, obj in rs:
            log("[%s] кольцо «%s» (%s) в (%.0f, %.0f, %.0f)" % (tag, name, obj, loc[0], loc[1], loc[2]))

    def probe_cam():
        """Копия камеры-зонда В ИГРОВОМ мире (PIE продублировал её из редакторского уровня)."""
        w = world()
        if not w:
            return None
        for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.CameraActor):
            return a
        return None

    def look_at_target():
        """Навести камеру-зонд на кольцо и сделать её видом игрока."""
        w = world()
        pc = unreal.GameplayStatics.get_player_controller(w, 0)
        cam = probe_cam()
        if not pc or not cam:
            log("⚠ нет контроллера или камеры-зонда — кадр будет от начала координат")
            return
        rs = ring_actors()
        if not rs:
            log("⚠ кольца нет — камеру не двигаю")
            return
        cx, cy, cz = rs[0][1]
        centre = unreal.Vector(cx, cy, cz)
        # 400 юнитов назад-вбок и 260 вверх: кольцо (полу-экстент до 150) занимает середину кадра.
        eye = unreal.Vector(cx - 400.0, cy - 400.0, cz + 260.0)
        rot = unreal.MathLibrary.find_look_at_rotation(eye, centre)
        cam.set_actor_location_and_rotation(eye, rot, False, False)
        pc.set_view_target_with_blend(cam, 0.0)
        log("камера-зонд: (%.0f,%.0f,%.0f) → цель (%.0f,%.0f,%.0f)" % (eye.x, eye.y, eye.z, cx, cy, cz))

    # ── шаги ────────────────────────────────────────────────────────────────────────────────────
    def s_travel():
        # ★ У карт мира в порту НЕТ своего GameMode: PIE поднимает `GameModeBase`, и
        #   `AAsdaPlayerController` не создаётся. Переезжаем в тот же уровень с ЯВНЫМ режимом.
        cmd("open /Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode")

    def s_ring_on():
        cmd("asda.Target.Ring 1")

    # ★ Выбор + наведение камеры + дамп В ОДНОМ шаге: кадр должен смотреть именно на кольцо,
    #   а дамп — относиться к тому же моменту, что и наведение.
    def s_npc():
        cmd("asda.Target.RingNearest npc")
        look_at_target()
        dump_rings("NPC")

    # ★★★ ОЖИДАНИЕ ПО ФАКТУ, А НЕ ПО СЧЁТУ ТИКОВ (закон [[false-negatives-method]]).
    #   Кадр «стало» должен показывать УДЕРЖАННЫЙ кадр, то есть сниматься ПОСЛЕ того, как
    #   единственная проходка квада закончилась: `StartDelay` (0.0672 с) + `life` (0.20 с).
    #   Счёт тиков сюда не годится — headless-PIE идёт ~18 к/с и шаг по времени плавает;
    #   спрашиваем ВОЗРАСТ самого актора-кольца.
    START_DELAY = 0.0672
    LIFE = 0.20

    def flash_done():
        w = world()
        if not w:
            return False
        for a in unreal.GameplayStatics.get_all_actors_with_tag(w, "TargetRingFx"):
            age = a.get_game_time_since_creation()
            if age >= START_DELAY + LIFE:
                log("проходка закончена: возраст кольца %.3f c (нужно >= %.3f)"
                    % (age, START_DELAY + LIFE))
                return True
        return False

    def s_npc_shot():
        dump_camera()
        take_shot("ring_npc")

    def s_npc_get():
        harvest("ring_npc")

    # ★★★ ПУНКТ 7: тот же объект спустя LATE тиков — обязан быть ТЕМ ЖЕ, ЗАМЕРШИМ на конечном
    #   кадре и ПО-ПРЕЖНЕМУ ВИДИМЫМ (дамп печатает альфу, полу-экстент и радиус геометрии).
    def s_npc_late():
        cmd("asda.Target.RingNearest dump")
        dump_rings("NPC спустя паузу")

    def s_npc_late_shot():
        take_shot("ring_npc_held")

    def s_npc_late_get():
        harvest("ring_npc_held")

    # ★★★ NPC НОРМАЛЬНОГО РОСТА — ОТДЕЛЬНЫМ ШАГОМ (задача: «проверь на NPC разного роста»).
    #   Ближайший NPC на alpen — `opara`, у которого низ модели уходит на 54.8 ПОД корень (модель
    #   и правда наполовину утоплена в землю), поэтому там срабатывает предохранитель и кольцо
    #   садится на корень. На такой цели посадку «на подошву» просто не увидеть. Берём человека
    #   (`soldier`, низ модели на 5.8 ниже корня) — у него кольцо обязано встать на 5.8 НИЖЕ, чем
    #   стояло раньше. Фильтр по имени — второй аргумент команды.
    def s_npc_tall():
        cmd("asda.Target.RingNearest npc soldier")
        look_at_target()
        dump_rings("NPC-человек")

    def s_npc_tall_shot():
        take_shot("ring_npc_tall")

    def s_npc_tall_get():
        harvest("ring_npc_tall")

    def s_mob():
        cmd("asda.Target.RingNearest mob")
        look_at_target()
        dump_rings("моб")

    def s_mob_shot():
        dump_camera()
        take_shot("ring_mob")

    def s_mob_get():
        harvest("ring_mob")

    def s_mob_late():
        cmd("asda.Target.RingNearest dump")
        dump_rings("моб спустя паузу")

    def s_mob_late_shot():
        take_shot("ring_mob_held")

    def s_mob_late_get():
        harvest("ring_mob_held")

    # ★★★ ПУНКТ 8: возврат на NPC — кольцо должно быть НОВЫМ объектом и снова сыграть.
    def s_npc_again():
        cmd("asda.Target.RingNearest npc")
        dump_rings("возврат на NPC")

    def s_npc_again_late():
        cmd("asda.Target.RingNearest dump")

    def s_clear():
        cmd("asda.Target.RingNearest off")

    def s_clear_dump():
        dump_rings("после снятия")

    def s_clear_shot():
        take_shot("ring_cleared")

    def s_clear_get():
        harvest("ring_cleared")

    def s_ring_off():
        cmd("asda.Target.Ring 0")

    def s_npc_off():
        cmd("asda.Target.RingNearest npc")

    def s_off_dump():
        dump_rings("Ring 0")

    # ── пункт 2 задания: окно разговора с NPC открывает ТОЛЬКО двойной клик ──
    def s_rule_default():
        cmd("asda.Npc.ClickRule")          # дефолт 0: одиночный НЕТ, двойной ДА

    def s_rule_etalon_on():
        cmd("asda.Npc.OpenOnSingleClick 1")

    def s_rule_etalon():
        cmd("asda.Npc.ClickRule")          # ключ отката 1: одиночный ДА (эталон)

    def s_rule_back():
        cmd("asda.Npc.OpenOnSingleClick 0")

    def s_rule_back_dump():
        cmd("asda.Npc.ClickRule")

    # ★ План = пары (шаг, ожидание ПОСЛЕ него). Ожидание — либо число тиков, либо
    #   ПРЕДИКАТ (`flash_visible` = «дождись, пока вспышка реально видна»).
    #   LATE — «сильно позже» (доказать, что эффект погашен и не пересоздан).
    PLAN = [(s_travel, TRAVEL_SETTLE),
            (s_ring_on, SETTLE),
            # NPC: выбрать → дождаться конца проходки → кадр «удержанного» кольца → и ещё раз
            #      СИЛЬНО ПОЗЖЕ (LATE), чтобы доказать, что оно никуда не делось.
            (s_npc, flash_done), (s_npc_shot, SETTLE), (s_npc_get, LATE),
            (s_npc_late, SETTLE), (s_npc_late_shot, SETTLE), (s_npc_late_get, SETTLE),
            (s_npc_tall, flash_done), (s_npc_tall_shot, SETTLE), (s_npc_tall_get, SETTLE),
            (s_mob, flash_done), (s_mob_shot, SETTLE), (s_mob_get, LATE),
            (s_mob_late, SETTLE), (s_mob_late_shot, SETTLE), (s_mob_late_get, SETTLE),
            (s_npc_again, LATE), (s_npc_again_late, SETTLE),
            (s_clear, SETTLE), (s_clear_dump, SETTLE), (s_clear_shot, SETTLE), (s_clear_get, SETTLE),
            (s_ring_off, SETTLE), (s_npc_off, SETTLE), (s_off_dump, SETTLE),
            (s_rule_default, SETTLE), (s_rule_etalon_on, SETTLE), (s_rule_etalon, SETTLE),
            (s_rule_back, SETTLE), (s_rule_back_dump, SETTLE)]

    def finish():
        if st["handle"] is not None:
            unreal.unregister_slate_post_tick_callback(st["handle"])
            st["handle"] = None
        try:
            les.editor_request_end_play()
        except Exception:
            pass
        # Камера-зонд не должна остаться в уровне: удаляем её из РЕДАКТОРСКОГО мира.
        #   Уровень при этом НЕ сохраняется — на диске он не менялся ни на байт.
        try:
            for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
                if a.get_actor_label() == PROBE_LABEL:
                    unreal.get_editor_subsystem(unreal.EditorActorSubsystem).destroy_actor(a)
                    log("камера-зонд удалена из редакторского уровня")
        except Exception as e:
            log("не удалось убрать камеру-зонд: %s" % e)
        log("=== готово, снимков запрошено %d ===" % len(st["shots"]))
        unreal.SystemLibrary.quit_editor()

    def on_tick(_dt):
        if st["busy"]:
            return
        st["busy"] = True
        try:
            st["tick"] += 1
            if st["tick"] > MAX_TICKS:
                unreal.log_error("[кольцо-живое] предел тиков")
                finish()
                return
            if st["tick"] < st["next_at"]:
                return
            # ★ Ожидание ПО УСЛОВИЮ: если у шага вместо числа тиков стоит предикат, следующий шаг
            #   не начнётся, пока предикат не скажет «да» (см. `flash_visible`).
            if st["gate"] is not None:
                if st["gate"]():
                    st["gate"] = None
                else:
                    st["next_at"] = st["tick"] + 1
                    return
            st["step"] += 1
            if st["step"] >= len(PLAN):
                finish()
                return
            fn, wait = PLAN[st["step"]]
            fn()
            if callable(wait):
                st["gate"] = wait
                st["next_at"] = st["tick"] + 1
            else:
                st["gate"] = None
                st["next_at"] = st["tick"] + wait
        except Exception:
            import traceback
            unreal.log_error("[кольцо-живое] ошибка:\n" + traceback.format_exc())
            finish()
        finally:
            st["busy"] = False

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    log("tick-callback зарегистрирован")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
