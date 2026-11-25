# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_questlog_live_check.py — ЖИВАЯ (PIE) проверка ЖУРНАЛА ЗАДАНИЙ (окно 700 QUEST_WIN).
#
# ★★★ ЗАЧЕМ ИМЕННО ЖИВАЯ (закон `false-negatives-method`). Офлайн-дамп `.uasset` видит разметку и
#   НЕ видит последнего шага — привязки контролов кодом, подписей из loc-таблицы, иконок ячеек,
#   раскрытия страницы-свитка и того, открывается ли окно вообще.
#
# ЧТО ПРОВЕРЯЕТСЯ (каждый пункт даёт строку-доказательство в логе):
#   1. окно 700 создаётся и контролы разбираются        — «окно 700 разобрано: …»
#   2. журнал наполняется (эмуляция пакета 5047)        — `Asda.QuestLog.FakeJournal 5`
#   3. ОТКРЫТИЕ ТУМБЛЕРОМ (тот же путь, что клавиша Q)  — `Asda.QuestLog.Open` + дамп
#   4. содержимое: названия, стадии, цели, маршруты     — `Asda.QuestLog.Dump`
#   5. окно реально рисуется                            — снимок `questlog_open.png`
#   6. выбор другой ячейки журнала                      — `Asda.QuestLog.Select 2` + дамп + снимок
#   7. правая страница-свиток (кнопка 739)              — `Asda.QuestLog.Page 1` + снимок
#   8. отслеживание (чекбокс 743) и лимит 6             — `Asda.QuestLog.Track 1`
#   9. ПРОГРЕСС ЦЕЛЕЙ: приём 5052 двигает счётчик         — `Asda.QuestLog.FakeProgress`
#  10. отказ от задания (модалка → C2S 5006)            — `Asda.QuestLog.Abandon`
#  10. закрытие ПОВТОРНЫМ нажатием (тумблер)            — `Asda.QuestLog.Open` + дамп
#  11. условие «задание выполнено» по таблице QCT       — `Asda.QuestLog.WhyDone <квест>`
#  12. СХЛОПЫВАНИЕ пустых строк награды и его ключ отката
#      — `asda.Quest.CollapseRewardCells 0/3` + строки «награда N: …» в дампе
#
# ЗАПУСК: Tools\run_headless_questlog.bat
import glob
import os
import shutil

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/questlog_live"
SS_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
LEVEL = "/Game/Map/alpen/alpen"

WARMUP = 300
TRAVEL_SETTLE = 400
SETTLE = 60
MAX_TICKS = 30000


def log(m):
    import unreal
    unreal.log("[журнал-живой] " + str(m))


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
    les.editor_request_begin_play()
    log("PIE запущен")

    st = {"tick": 0, "step": -1, "next_at": WARMUP, "handle": None, "busy": False, "shots": []}

    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    world = PH.make_game_world(ues)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 5 стр.)

    cmd = PH.make_console_cmd(world, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 6 стр.)

    take_shot = PH.make_take_shot(cmd, st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 3 стр.)

    harvest = PH.make_harvest(SS_DIR, OUT_DIR, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 10 стр.)

    PLAN = [
        # ★ У карт мира в порту нет своего GameMode: PIE поднимает GameModeBase, и
        #   AAsdaPlayerController не создаётся. Переезжаем в тот же уровень с ЯВНЫМ режимом.
        lambda: cmd("open /Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode"),
        lambda: cmd("Asda.QuestLog.Dump"),                 # окно ещё закрыто — «выбрана ячейка -1»
        lambda: cmd("Asda.QuestLog.FakeJournal 5"),        # эмуляция 5047: пять заданий в журнале
        lambda: cmd("Asda.QuestLog.Open"),                 # ТУМБЛЕР — тот же путь, что клавиша Q
        lambda: cmd("Asda.QuestLog.Dump"),
        lambda: take_shot("questlog_open"),
        lambda: harvest("questlog_open"),
        # ★★★ СХЛОПЫВАНИЕ НУЛЕВЫХ НАГРАД (решение владельца 2026-08-23, отчёт
        #   `Reports/quest_reward_collapse.md`). Ячейка 0 журнала — квест 0 шаг 0:
        #   Money = 0, Exp = 10 ⇒ строка «10 Опыт» обязана стоять ПЕРВОЙ, а не второй.
        #   Ключ отката проверяется тут же: при `0` возвращаются эталонные фиксированные роли.
        #   Строки награды печатает `Asda.QuestLog.Dump` («награда 0: …»).
        lambda: cmd("Asda.QuestLog.Select 0"),
        lambda: cmd("Asda.QuestLog.Dump"),
        lambda: take_shot("questlog_reward_collapsed"),
        lambda: harvest("questlog_reward_collapsed"),
        # ★ `SelectSlot` на ТЕКУЩЕЙ ячейке выходит сразу (`ActiveSlot == SlotIdx`), поэтому после
        #   смены ключа панель перерисовывают ЧЕРЕЗ соседнюю ячейку — иначе дамп покажет старое
        #   и это будет ложное «ключ не работает».
        lambda: cmd("asda.Quest.CollapseRewardCells 0"),
        lambda: cmd("Asda.QuestLog.Select 1"),
        lambda: cmd("Asda.QuestLog.Select 0"),
        lambda: cmd("Asda.QuestLog.Dump"),
        lambda: take_shot("questlog_reward_etalon_roles"),
        lambda: harvest("questlog_reward_etalon_roles"),
        lambda: cmd("asda.Quest.CollapseRewardCells 3"),
        lambda: cmd("Asda.QuestLog.Select 1"),
        lambda: cmd("Asda.QuestLog.Select 0"),
        lambda: cmd("Asda.QuestLog.Dump"),
        lambda: cmd("Asda.QuestLog.Select 2"),
        lambda: cmd("Asda.QuestLog.Dump"),
        lambda: take_shot("questlog_slot2"),
        lambda: harvest("questlog_slot2"),
        lambda: cmd("Asda.QuestLog.Page 1"),               # развернуть страницу-свиток (кнопка 739)
        lambda: take_shot("questlog_page"),
        lambda: harvest("questlog_page"),
        lambda: cmd("Asda.QuestLog.Track 1"),
        # ★ прогресс целей: эмуляция ПАКЕТА 5052 тем же путём, что и сеть
        lambda: cmd("Asda.QuestLog.FakeProgress 2 21385 3"),   # цель + счётчик у русского задания
        lambda: cmd("Asda.QuestLog.Dump"),
        lambda: take_shot("questlog_progress"),
        lambda: harvest("questlog_progress"),
        lambda: cmd("Asda.QuestLog.Dump"),
        lambda: cmd("Asda.QuestLog.WhyDone"),              # разбор условия завершения по QCT
        lambda: cmd("Asda.QuestLog.Abandon"),              # модалка подтверждения
        lambda: take_shot("questlog_abandon"),
        lambda: harvest("questlog_abandon"),
        lambda: cmd("Asda.QuestLog.Open"),                 # повторное нажатие — закрытие
        lambda: cmd("Asda.QuestLog.Dump"),
    ]

    finish = PH.make_pie_finish(st, les, log, "=== готово, снимков запрошено %d ===", count_shots=True)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 10 стр.)

    def on_tick(_dt):
        if st["busy"]:
            return
        st["busy"] = True
        try:
            st["tick"] += 1
            if st["tick"] > MAX_TICKS:
                unreal.log_error("[журнал-живой] предел тиков")
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
            unreal.log_error("[журнал-живой] ошибка:\n" + traceback.format_exc())
            finish()
        finally:
            st["busy"] = False

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    log("tick-callback зарегистрирован")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
