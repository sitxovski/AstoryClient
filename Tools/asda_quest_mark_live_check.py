# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_quest_mark_live_check.py — ЖИВАЯ (PIE) проверка ВЫСОТЫ КВЕСТОВОЙ МЕТКИ над NPC.
#
# ★★★ ЗАЧЕМ ИМЕННО ЖИВАЯ (закон `false-negatives-method`). Плашка имени в ИГРЕ рисуется не
#   компонентом, а отдельным экранным виджетом с РУЧНОЙ проекцией (`UpdateScreenPosition`), и
#   именно от неё считается высота метки. В редакторском мире плашка — обычный World-space виджет
#   на якорной точке, то есть офлайн-снимок показал бы ДРУГУЮ геометрию и доказывал бы не то.
#
# ★★★ ЛОВУШКА КАДРА В HEADLESS-PIE (проверено замером, а не на глаз). В headless-редакторе
#   ТРЁХМЕРНАЯ картинка вписывается в виджет вьюпорта С ПОЛЯМИ (замер: виджет 2038x783, картинка
#   1401x783 по центру), а слой Slate (плашка имени) рисуется по ВСЕЙ ширине виджета. Поэтому НА
#   СНИМКЕ метка (объект мира) и имя (интерфейс) стоят в РАЗНЫХ масштабах и кажутся разъехавшимися,
#   хотя расчёт верен. В игровом окне (размер вьюпорта = размеру кадра) полей нет и слои совпадают.
#   ⇒ ГЛАВНОЕ ДОКАЗАТЕЛЬСТВО — ЧИСЛА дампа, а не картинка: там рядом печатается и расчётный верх
#   строки имени, и ФАКТИЧЕСКАЯ геометрия виджета (`GetNameAbsRect`), и они сходятся до 0.2 пункта.
#
# ЧТО ПРОВЕРЯЕТСЯ (каждый пункт даёт строку-доказательство в логе):
#   1. метка стоит НАД строкой имени — `Asda.Quest.MarkHeightDump` печатает ЗАЗОР В ПУНКТАХ
#      между верхом строки имени и низом метки (задание владельца: «над именами, ±10 пунктов»);
#   2. зазор ОДИНАКОВ у NPC РАЗНОГО РОСТА — дамп идёт по всем видимым NPC кадра сразу
#      (alpen: от `Soul Statue` с якорем 70 до `Moving Crystal` с якорем 800);
#   3. зазор держится НА РАЗНОМ УДАЛЕНИИ КАМЕРЫ — дамп повторяется с трёх дистанций
#      (близко / средне / далеко), потому что правило экранное и обязано это переживать;
#   4. кадры `shot showui` — метка и имя в одном кадре (только `shot showui` кладёт в PNG слой
#      Slate, `HighResShot` рисует одну сцену).
#
# БЕЗ СЕРВЕРА: запечённые NPC в игре скрыты до пакета 4067, а состояние квестов пусто ⇒ настоящих
#   меток нет. Поэтому используются две ДИАГНОСТИЧЕСКИЕ команды порта: `Asda.Npc.ShowBaked 1`
#   (показать запечённых — ровно то же, что делает удочерение) и `Asda.Quest.MarkAll 1`
#   (принудительная метка всем NPC карты). Игровой путь ими не меняется.
#
# ЗАПУСК: Tools\run_headless_quest_mark.bat
#   ★ Нужен ТИК редактора (`-ExecCmds="py …"`, а не -ExecutePythonScript) и РЕНДЕР (без -NullRHI).
import glob
import os
import shutil

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/quest_mark_live"
SS_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
LEVEL = "/Game/Map/alpen/alpen"
PROBE_LABEL = "QuestMarkProbeCam"

WARMUP = 300
TRAVEL_SETTLE = 400   # переезд уровня: стриминг + BeginPlay мира
SETTLE = 60
MAX_TICKS = 40000


def log(m):
    import unreal
    unreal.log("[метка-живая] " + str(m))


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
    # ★ Камера-зонд кладётся ДО старта PIE: PIE дублирует редакторский уровень вместе с ней.
    #   Уровень НЕ сохраняется, актор удаляется в finish().
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    probe = eas.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    probe.set_actor_label(PROBE_LABEL)
    log("камера-зонд «%s» добавлена" % PROBE_LABEL)
    les.editor_request_begin_play()
    log("PIE запущен")

    st = {"tick": 0, "step": -1, "next_at": WARMUP, "handle": None, "busy": False, "shots": []}

    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    world = PH.make_game_world(ues)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 5 стр.)

    cmd = PH.make_console_cmd(world, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 6 стр.)

    take_shot = PH.make_take_shot(cmd, st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 6 стр.)

    harvest = PH.make_harvest(SS_DIR, OUT_DIR, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 10 стр.)

    probe_cam = PH.make_probe_camera(world)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 7 стр.)

    def npc_actors():
        w = world()
        if not w:
            return []
        cls = unreal.load_class(None, "/Script/AsdaReborn.AsdaNpcActor")
        return list(unreal.GameplayStatics.get_all_actors_of_class(w, cls))

    def pick_group():
        """Точка, вокруг которой на alpen кучно стоят NPC: берём медианного по X/Y из видимых."""
        pts = []
        for a in npc_actors():
            # `hidden` — UPROPERTY актора (bHidden); питон-обёртки `is_hidden()` у него нет.
            if a.get_editor_property("hidden"):
                continue
            l = a.get_actor_location()
            pts.append((l.x, l.y, l.z))
        if not pts:
            return None
        pts.sort()
        return pts[len(pts) // 2]

    def look_at_npc(needle, dist, tag):
        """Навести камеру на КОНКРЕТНОГО NPC (подстрока имени) с дистанции `dist`.

        ★ Нужен отдельный кадр на одного NPC: в общем плане плашки соседей налезают друг на друга,
          и по такому кадру ничего не докажешь (классический ложный отрицательный результат).
        """
        w = world()
        pc = unreal.GameplayStatics.get_player_controller(w, 0)
        cam = probe_cam()
        target = None
        for a2 in npc_actors():
            # ★ Скрытых НЕ пропускаем: предыдущий кадр «по одному» их и спрятал.
            nm = str(a2.get_editor_property("NpcName"))
            if needle.lower() in nm.lower():
                target = a2
                break
        if not pc or not cam or not target:
            log("⚠ %s: NPC «%s» не найден или нет контроллера/камеры" % (tag, needle))
            return
        # ★ ОСТАВЛЯЕМ В КАДРЕ ТОЛЬКО ЕГО. Иначе плашки соседей (в том числе NPC с другого конца
        #   карты) висят рядом, и по кадру нельзя сказать, чьё имя мы видим — доказательства нет.
        for a3 in npc_actors():
            a3.set_editor_property("hidden", a3 != target)
        l = target.get_actor_location()
        centre = unreal.Vector(l.x, l.y, l.z + 130.0)     # чуть выше головы: имя и метка в кадре
        k = dist / 1.7320508
        eye = unreal.Vector(l.x - k, l.y - k, l.z + k * 0.35)
        rot = unreal.MathLibrary.find_look_at_rotation(eye, centre)
        cam.set_actor_location_and_rotation(eye, rot, False, False)
        pc.set_view_target_with_blend(cam, 0.0)
        log("%s: камера на %.0f юнитов от «%s» (%.0f,%.0f,%.0f)"
            % (tag, dist, str(target.get_editor_property("NpcName")), l.x, l.y, l.z))

    def look_from(dist, tag):
        """Поставить камеру-зонд на `dist` юнитов от группы NPC и сделать её видом игрока.

        ★ Дистанция — это и есть проверка №3: правило экранное, и зазор обязан выжить на любой.
        """
        w = world()
        pc = unreal.GameplayStatics.get_player_controller(w, 0)
        cam = probe_cam()
        c = pick_group()
        if not pc or not cam or not c:
            log("⚠ %s: нет контроллера/камеры/видимых NPC — кадр будет от начала координат" % tag)
            return
        cx, cy, cz = c
        centre = unreal.Vector(cx, cy, cz + 90.0)          # целимся в грудь модели
        k = dist / 1.7320508                                # равные доли по X/Y/Z
        eye = unreal.Vector(cx - k, cy - k, cz + k * 0.6)
        rot = unreal.MathLibrary.find_look_at_rotation(eye, centre)
        cam.set_actor_location_and_rotation(eye, rot, False, False)
        pc.set_view_target_with_blend(cam, 0.0)
        log("%s: камера на %.0f юнитов, глаз (%.0f,%.0f,%.0f) → NPC (%.0f,%.0f,%.0f)"
            % (tag, dist, eye.x, eye.y, eye.z, cx, cy, cz))

    # ── шаги ────────────────────────────────────────────────────────────────────────────────────
    def s_travel():
        # У карт мира нет своего GameMode: без явного режима не создаётся AAsdaPlayerController,
        #   а без него нет ни проекции, ни вида — экранное правило проверять было бы нечем.
        cmd("open /Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode")

    def s_show():
        cmd("Asda.Npc.ShowBaked 1")

    def s_mark():
        cmd("Asda.Quest.MarkAll 1")

    def s_show_again():
        cmd("Asda.Npc.ShowBaked 1")     # вернуть всех после кадров «по одному»

    def s_near():
        look_from(700.0, "БЛИЗКО")      # штатная длина штанги камеры игрока

    def s_near_dump():
        cmd("Asda.Quest.MarkHeightDump")

    def s_near_shot():
        take_shot("qmark_near")

    def s_near_get():
        harvest("qmark_near")

    def s_mid():
        look_from(170.0, "МАКС. ЗУМ")   # ближний предел зума игрока

    def s_mid_dump():
        cmd("Asda.Quest.MarkHeightDump")

    def s_mid_shot():
        take_shot("qmark_zoom_in")

    def s_mid_get():
        harvest("qmark_zoom_in")

    def s_far():
        look_from(1270.0, "МИН. ЗУМ")   # дальний предел зума игрока

    def s_far_dump():
        cmd("Asda.Quest.MarkHeightDump")

    def s_far_shot():
        take_shot("qmark_zoom_out")

    def s_far_get():
        harvest("qmark_zoom_out")

    # Эталонная формула для сравнения (верх габаритов + 60) — доказательство, что ветка жива.
    def s_etalon_on():
        cmd("Asda.Quest.MarkUseEtalonHeight 1")

    def s_etalon_dump():
        cmd("Asda.Quest.MarkHeightDump")

    def s_etalon_shot():
        take_shot("qmark_etalon")

    def s_etalon_get():
        harvest("qmark_etalon")

    def s_etalon_off():
        cmd("Asda.Quest.MarkUseEtalonHeight 0")

    def s_back():
        look_from(700.0, "ВОЗВРАТ")

    def s_final_dump():
        cmd("Asda.Quest.MarkHeightDump")

    def s_mark_off():
        cmd("Asda.Quest.MarkAll off")

    # ── ОТДЕЛЬНЫЕ КАДРЫ ПО ОДНОМУ NPC (рост: обычный / высокий / низкий) ──
    def s_one_john():
        look_at_npc("John", 450.0, "ОДИН NPC (обычный)")

    def s_one_john_dump():
        cmd("Asda.Quest.MarkHeightDump John")

    def s_one_john_shot():
        take_shot("qmark_one_john")

    def s_one_john_get():
        harvest("qmark_one_john")

    def s_one_statue():
        # «The Statue Of Sophia» — якорь плашки 620 при верхе габаритов 792: высокий объект.
        look_at_npc("Sophia", 1400.0, "ОДИН NPC (высокий)")

    def s_one_statue_dump():
        cmd("Asda.Quest.MarkHeightDump Sophia")

    def s_one_statue_shot():
        take_shot("qmark_one_statue")

    def s_one_statue_get():
        harvest("qmark_one_statue")

    def s_one_low():
        # «Soul Statue» — якорь плашки 70.3, самый низкий на карте.
        look_at_npc("Soul Statue", 400.0, "ОДИН NPC (низкий)")

    def s_one_low_dump():
        cmd("Asda.Quest.MarkHeightDump Soul Statue")

    def s_one_low_shot():
        take_shot("qmark_one_low")

    def s_one_low_get():
        harvest("qmark_one_low")

    PLAN = [s_travel, s_show, s_mark,
            s_one_john, s_one_john_dump, s_one_john_shot, s_one_john_get,
            s_one_statue, s_one_statue_dump, s_one_statue_shot, s_one_statue_get,
            s_one_low, s_one_low_dump, s_one_low_shot, s_one_low_get, s_show_again,
            s_near, s_near_dump, s_near_shot, s_near_get,
            s_mid, s_mid_dump, s_mid_shot, s_mid_get,
            s_far, s_far_dump, s_far_shot, s_far_get,
            s_etalon_on, s_back, s_etalon_dump, s_etalon_shot, s_etalon_get, s_etalon_off,
            s_back, s_final_dump, s_mark_off]

    def finish():
        if st["handle"] is not None:
            unreal.unregister_slate_post_tick_callback(st["handle"])
            st["handle"] = None
        try:
            les.editor_request_end_play()
        except Exception:
            pass
        try:
            eas.destroy_actor(probe)
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
                unreal.log_error("[метка-живая] предел тиков")
                finish()
                return
            if st["tick"] < st["next_at"]:
                return
            st["step"] += 1
            if st["step"] >= len(PLAN):
                finish()
                return
            PLAN[st["step"]]()
            st["next_at"] = st["tick"] + (TRAVEL_SETTLE if st["step"] == 0 else SETTLE)
        except Exception:
            import traceback
            unreal.log_error("[метка-живая] ошибка:\n" + traceback.format_exc())
            finish()
        finally:
            st["busy"] = False

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    log("tick-callback зарегистрирован")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
