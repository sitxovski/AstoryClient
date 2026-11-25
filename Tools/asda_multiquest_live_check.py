# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_multiquest_live_check.py — ЖИВАЯ (PIE) проверка ОБЛАЧКА ВЫБОРА ЗАДАНИЯ (JUI 2406
#   `MULTYQUEST_WIN`) после правки четырёх дефектов. Разбор — `Reports/multiquest_window_fix.md`.
#
# ★★★ ЗАЧЕМ ЖИВАЯ (закон `false-negatives-method`). Офлайн-дамп `.uasset` показывает ЗАПЕЧЁННУЮ
#   раскладку из `.JUI` и НЕ показывает того, что делает код на показе: центрирование, пересборку
#   рамки под контент, кнопки-строки списка и подмену заголовка строкой таблицы. Именно эти четыре
#   вещи и проверяются.
#
# ЧТО ПРОВЕРЯЕТСЯ (каждый пункт — строка-доказательство в логе или кадр):
#   1. окно 2406 собирается: 9 планов рамки, обе шапки, оба «уголка»  — «MULTYQUEST_WIN разобран: …»
#   2. ЦЕНТР ЭКРАНА: `asda.Npc.MultiQuestDump` печатает прямоугольник рамки, центр вьюпорта и
#      расхождение между ними — обязано быть около нуля НА ЛЮБОМ разрешении (скрипт запускается
#      дважды, из `.bat`, с разными -ResX/-ResY)
#   3. РАЗМЕР ПО КОНТЕНТУ: там же — «размер=WxH (JUI 258x58)» и ширина текста каждой строки;
#      ширина рамки обязана быть НЕ МЕНЬШЕ самой длинной строки
#   4. HOVER и КЛИК по строке: `asda.UI.SimHoverWidget` / `asda.UI.SimClickWidget` по кнопке строки
#      `MULTYQUEST_ROW_2408_0` + кадры; клик обязан дать в лог «MULTYQUEST: выбран …» и переход
#      на страницу диалога
#   5. ЗАГОЛОВОК: в дампе «текст=«Выбор задания»» (строка 6164 из таблицы, не зашита в код)
#
# ЗАПУСК: Tools/run_headless_multiquest.bat
import glob
import os
import shutil

OUT_DIR = os.environ.get("ASDA_MQ_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/multiquest_live")
SS_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
# NPC 108 «Grave Of Brave Worrior»: в SNQInfo у него есть и выдача (квест 150), и приём (149).
LEVEL = "/Game/Map/alpen/alpen"

WARMUP = 300
TRAVEL_SETTLE = 400
SETTLE = 60
MAX_TICKS = 30000


def log(m):
    import unreal
    unreal.log("[облачко-заданий] " + str(m))


def main():
    import unreal

    if os.path.isdir(SS_DIR):
        shutil.rmtree(SS_DIR, ignore_errors=True)
    if os.path.isdir(OUT_DIR):
        shutil.rmtree(OUT_DIR, ignore_errors=True)
    os.makedirs(OUT_DIR)

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

    # ★★★ «РАЗНЫЕ РАЗРЕШЕНИЯ» В PIE — ЧЕМ ИХ ЧЕСТНО ЭМУЛИРОВАТЬ.
    #   Что НЕ работает (проверено, чтобы не повторять):
    #     · ключи `-ResX/-ResY` — игровой вьюпорт в PIE редактора это ПАНЕЛЬ окна редактора, и её
    #       прямоугольник задаёт сохранённая раскладка: два прогона с разными ключами дали ОДИН И
    #       ТОТ ЖЕ прямоугольник (4,134)-(2042,916) — классический ложный «проверено»;
    #     · `Slate.ApplicationScale` — масштабирует оболочку редактора, до DPI игрового вьюпорта
    #       не доходит: прямоугольник корня не изменился;
    #     · `unreal.LevelEditorPlaySettings` — в Python НЕ экспонирован (AttributeError).
    #   Что работает: `UUserInterfaceSettings.ApplicationScale` — это множитель DPI ИМЕННО
    #   игрового UMG-слоя. Он меняет размер КОРНЕВОЙ КАНВЫ в единицах Slate, а это и есть
    #   «разрешение» с точки зрения раскладки окна. Значения ставятся шагами ниже.
    #   ★ Сам КЛАСС `UserInterfaceSettings` в модуль `unreal` не экспонирован, зато его CDO
    #     достаётся по пути объекта — этого хватает, свойства читаются и пишутся рефлексией.
    #   ★ Имя свойства пробуем ОБА: класс в модуль `unreal` не экспонирован, поэтому «змеиного»
    #     псевдонима (`application_scale`) у него нет — работает только точное `ApplicationScale`.
    ui_settings = None
    ui_scale_prop = None
    try:
        ui_settings = unreal.load_object(None, "/Script/Engine.Default__UserInterfaceSettings")
        for name in ("ApplicationScale", "application_scale"):
            try:
                base = ui_settings.get_editor_property(name)
                ui_scale_prop = name
                log("UserInterfaceSettings.%s = %s (база)" % (name, base))
                break
            except Exception:
                continue
        if not ui_scale_prop:
            log("⚠ свойство масштаба не найдено — проверка масштаба будет пропущена")
    except Exception as e:
        log("⚠ UserInterfaceSettings недоступны (%s) — проверка масштаба будет пропущена" % e)

    unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
    log("уровень %s загружен" % LEVEL)
    les.editor_request_begin_play()
    log("PIE запущен")

    st = {"tick": 0, "step": -1, "next_at": WARMUP, "handle": None, "shots": [], "busy": False}

    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    world = PH.make_game_world(ues)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 5 стр.)

    cmd = PH.make_console_cmd(world, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 6 стр.)

    take_shot = PH.make_take_shot(cmd, st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 4 стр.)

    harvest = PH.make_harvest(SS_DIR, OUT_DIR, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 10 стр.)

    # ── шаги ────────────────────────────────────────────────────────────────────────────────────
    def s_travel():
        cmd("open /Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode")

    # ══ РЕЖИМ 0 — ТОЛЬКО ВЫДАЧА. NPC 108, квест 150 (ключ SNQICT 386). ══════════════════════════
    def s_open_108():
        cmd("asda.Npc.Dialog 108")

    def s_fake_self():
        # Без сервера уровень нулевой, а условия записей SNQICT у этого NPC требуют 140…150.
        cmd("asda.Quest.FakeSelf 145")

    def s_fake_done149():
        # Условие ключа 386 («выдать 150») требует ЗАВЕРШЁННОГО квеста 149 — ставим его в битовую
        #   карту завершённых, взятого квеста нет ⇒ список «принимает» пуст ⇒ режим 0.
        cmd("asda.Quest.FakeState -1 0 149")

    def s_mq0_show():
        cmd("asda.Npc.MultiQuest")

    def s_mq0_dump():
        cmd("asda.Npc.MultiQuestDump")

    def s_mq0_shot():
        take_shot("multiquest_mode0")

    def s_mq0_get():
        harvest("multiquest_mode0")

    # ══ НАВЕДЕНИЕ и НАЖАТИЕ по строке списка «выдаёт» (2408) ═════════════════════════════════════
    def s_hover():
        # ★ Наведение обязательно ПЕРЕД кликом: `SButton::OnMouseButtonUp` выполняет `ExecuteOnClick`
        #   только при `IsHovered()`, а платформенный курсор в headless стоит в (0,0).
        cmd("asda.UI.SimHoverWidget MULTYQUEST_WIN MULTYQUEST_ROW_2408_0")

    def s_hover_shot():
        take_shot("multiquest_hover")

    def s_hover_get():
        harvest("multiquest_hover")

    def s_press():
        # Удержание 150 кадров: следующий шаг (снимок) приходит через SETTLE=60 кадров, то есть
        #   гарантированно ВО ВРЕМЯ нажатия — кадр застаёт состояние Pressed.
        cmd("asda.UI.SimClickWidget MULTYQUEST_WIN MULTYQUEST_ROW_2408_0 150")

    def s_press_shot():
        take_shot("multiquest_press")

    def s_press_get():
        harvest("multiquest_press")

    def s_after_click_dump():
        # Отпускание уже прошло ⇒ в логе обязаны быть «MULTYQUEST: выбран …» и новая страница.
        cmd("asda.Npc.DialogDump")

    def s_unhover():
        cmd("asda.UI.SimHoverWidget")

    # ══ РЕЖИМ 1 — ТОЛЬКО СДАЧА (квест 149 взят и готов) ═════════════════════════════════════════
    def s_fake_take149():
        cmd("asda.Quest.FakeState 149 0")

    def s_mq1_show():
        cmd("asda.Npc.MultiQuest")

    def s_mq1_dump():
        cmd("asda.Npc.MultiQuestDump")

    def s_mq1_shot():
        take_shot("multiquest_mode1")

    def s_mq1_get():
        harvest("multiquest_mode1")

    # ══ РЕЖИМ 2 — ОБА БЛОКА + ДЛИННЫЕ РУССКИЕ НАЗВАНИЯ (проверка ширины). ═══════════════════════
    #   NPC 132 «Golden Scissors»: выдаёт 246/247/248/262, принимает 246/247/248/261. У квеста 248
    #   самое длинное название во всей `EpisodeTable` среди выдаваемых —
    #   «"Золотые ножницы 1 - Исследование развалин"» (43 знака), то есть заведомо шире
    #   эталонных 210 px списка. Именно на нём и видно, работает ли подгонка ширины.
    #   Состояние подобрано ПО ДАННЫМ `SnqIct.json`, а не наугад:
    #     ключ 586 (выдать 248): уровень 19…26, квест 247 ЗАВЕРШЁН, 248 свободен;
    #     ключ 616 (принять 261): уровень 20…150, квест 261 В РАБОТЕ.
    #   ⇒ уровень 25, завершены 246 и 247, взят 261 — сходятся обе стороны, режим 2.
    def s_close_108():
        cmd("asda.Npc.DialogClose")

    def s_open_132():
        cmd("asda.Npc.Dialog 132")

    def s_fake_self_hi():
        cmd("asda.Quest.FakeSelf 25")

    def s_fake_mode2():
        cmd("asda.Quest.FakeState 261 0 246,247")

    def s_mq2_show():
        cmd("asda.Npc.MultiQuest")

    def s_mq2_dump():
        cmd("asda.Npc.MultiQuestDump")

    def s_mq2_shot():
        take_shot("multiquest_mode2")

    def s_mq2_get():
        harvest("multiquest_mode2")

    # ══ РАЗНЫЕ РАЗРЕШЕНИЯ ═══════════════════════════════════════════════════════════════════════
    #   ★ Ключи `-ResX/-ResY` в PIE редактора НЕ работают: вьюпорт — часть окна редактора, и его
    #     размер задаёт сохранённая раскладка (оба прогона .bat дали один и тот же прямоугольник).
    #     Настоящее изменение «разрешения» в терминах UMG — это размер КОРНЕВОЙ КАНВЫ в единицах
    #     Slate, и меняет его `Slate.ApplicationScale`. Окно центрируется якорем 0.5, поэтому при
    #     любом масштабе расхождение центров обязано остаться нулевым.
    def set_ui_scale(v):
        if not ui_settings or not ui_scale_prop:
            log("масштаб %s пропущен — настроек нет" % v)
            return
        ui_settings.set_editor_property(ui_scale_prop, v)
        log("ApplicationScale = %s" % v)

    def s_scale_small():
        set_ui_scale(0.6)

    def s_scale_small_dump():
        cmd("asda.Npc.MultiQuestDump")

    def s_scale_small_shot():
        take_shot("multiquest_scale06")

    def s_scale_small_get():
        harvest("multiquest_scale06")

    def s_scale_big():
        set_ui_scale(1.4)

    def s_scale_big_dump():
        cmd("asda.Npc.MultiQuestDump")

    def s_scale_big_shot():
        take_shot("multiquest_scale14")

    def s_scale_big_get():
        harvest("multiquest_scale14")

    def s_scale_reset():
        set_ui_scale(1.0)

    def s_close():
        cmd("asda.Npc.DialogClose")

    STEPS = [
        (s_travel, TRAVEL_SETTLE),
        (s_open_108, SETTLE),
        (s_fake_self, SETTLE),
        (s_fake_done149, SETTLE),
        (s_mq0_show, SETTLE),
        (s_mq0_dump, SETTLE),
        (s_mq0_shot, SETTLE),
        (s_mq0_get, SETTLE),
        (s_hover, SETTLE),
        (s_hover_shot, SETTLE),
        (s_hover_get, SETTLE),
        (s_press, 30),
        (s_press_shot, SETTLE),
        (s_press_get, SETTLE),
        (s_after_click_dump, SETTLE),
        (s_unhover, SETTLE),
        (s_fake_take149, SETTLE),
        (s_mq1_show, SETTLE),
        (s_mq1_dump, SETTLE),
        (s_mq1_shot, SETTLE),
        (s_mq1_get, SETTLE),
        (s_close_108, SETTLE),
        (s_open_132, SETTLE),
        (s_fake_self_hi, SETTLE),
        (s_fake_mode2, SETTLE),
        (s_mq2_show, SETTLE),
        (s_mq2_dump, SETTLE),
        (s_mq2_shot, SETTLE),
        (s_mq2_get, SETTLE),
        (s_scale_small, SETTLE),
        (s_scale_small_dump, SETTLE),
        (s_scale_small_shot, SETTLE),
        (s_scale_small_get, SETTLE),
        (s_scale_big, SETTLE),
        (s_scale_big_dump, SETTLE),
        (s_scale_big_shot, SETTLE),
        (s_scale_big_get, SETTLE),
        (s_scale_reset, SETTLE),
        (s_close, SETTLE),
    ]

    def on_tick(_dt):
        # ★ Реентерабельность: `shot showui` и travel гонят свои тики, и без флага шаг успел бы
        #   выполниться дважды (тот же приём, что в `asda_npc_dialog_live_check.py`).
        if st["busy"]:
            return
        st["busy"] = True
        try:
            st["tick"] += 1
            if st["tick"] > MAX_TICKS:
                unreal.log_error("[облачко-заданий] предел тиков")
                stop()
                return
            if st["tick"] < st["next_at"]:
                return
            st["step"] += 1
            if st["step"] >= len(STEPS):
                log("ВСЕ ШАГИ ПРОЙДЕНЫ, снимков запрошено %d" % len(st["shots"]))
                stop()
                return
            fn, wait = STEPS[st["step"]]
            fn()
            st["next_at"] = st["tick"] + wait
        except Exception:
            import traceback
            unreal.log_error("[облачко-заданий] ошибка:\n" + traceback.format_exc())
            stop()
        finally:
            st["busy"] = False

    def stop():
        if st["handle"] is not None:
            unreal.unregister_slate_post_tick_callback(st["handle"])
            st["handle"] = None
        try:
            les.editor_request_end_play()
        except Exception:
            pass
        log("PIE остановлен, выходим")
        unreal.SystemLibrary.quit_editor()

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    log("машина шагов запущена: %d шагов" % len(STEPS))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
