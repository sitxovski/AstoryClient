# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_quest_accept_win_live_check.py — ЖИВАЯ (PIE) проверка ОКНА ПРИЁМА ЗАДАНИЯ `QUEST_Confirm_Win`
#   (JUI 750), заменившего экранный нотис 20197 в ветке приёма 5044 `resultCode == 0`.
#
# ЧТО ПРОВЕРЯЕТСЯ (разбор — `Reports/quest_accept_window.md`):
#   · три высоты окна из `questRewardPanelResize@0xC676E0`: 214 (только первый ряд плиток),
#     250 (занят второй ряд, ячейки 2/3), 390 (есть награда «на выбор»);
#   · плитки по ФИКСИРОВАННЫМ местам: деньги → 1, опыт → 2, предметы → 3 и 4;
#     ★★★ С 2026-08-23 это правило ОТМЕНЕНО решением владельца «сдвигать всегда, когда там
#     пустота»: ЛЮБАЯ пустая ячейка выкидывается, соседи сдвигаются влево, порядок наград
#     сохраняется (`asda.Quest.CollapseRewardCells`, разбор — `Reports/quest_reward_collapse.md`).
#     Проверяются все пять значений ключа;
#   · строка 751 «Получено задание …» с подставленным названием эпизода (нотис 20197, слот [ss1]);
#   · заголовки 760/770 (нотисы 12003/12004) и подпись кнопки 780 (нотис 2288 «ОК»);
#   · кнопка 780 закрывает окно (`questPanel_HandleUICommand@0xC5C3B0`).
#
# ★ Показ идёт ТЕМ ЖЕ телом, что и приём 5044 (`ShowQuestAcceptWindow`) — отдельной тестовой ветки
#   НЕТ, команда `asda.Npc.AcceptShow` лишь подставляет тройку (эпизод, квест, шаг).
#
# ТРОЙКИ ВЗЯТЫ ИЗ ДАННЫХ `Data\NPC\RewardTable.BIN` + `EpisodeTable.json`, не придуманы:
#   (2005, 4,   0) money 100  exp 30                                   ⇒ высота 214
#   (2003, 2,   1) money 400  exp 150 + предметы 41782×1 и 73360×1     ⇒ высота 250
#   (2028, 27,  1) money 1600 exp 1000 + ДВЕ награды на выбор          ⇒ высота 390
#   (2158, 157, 1) записей «на выбор» ВОСЕМЬ при четырёх ячейках       ⇒ 390 + предупреждение
#   (2009, 8,   0) money 0    exp 30                 ⇒ денег нет, опыт СДВИНУТ на первое место (G1)
#   (2062, 61,  0) money 0    exp 100 + предмет 20573×20 ⇒ опыт G1, предмет G2, высота 214
#   (2341, 340, 0) money 0    exp 0   + предмет 23516×1  ⇒ предмет G1 (в режиме 4 — остался бы в
#                                                          нижнем ряду при ПУСТОМ верхнем)
#   (2078, 77,  2) money 3000 exp 20000 + ItemCode1 = −1, ItemCode2 = 7743 ⇒ дыры в G3 БОЛЬШЕ НЕТ:
#   (2080, 79,  2) то же, предмет 7744                     предмет поднимается в G3, высота 250
#   (2082, 81,  2) то же, предмет 7745                     (FIX-ORIG: у эталона тут было 214,
#                                                          и награда уезжала за нижний край окна)
#
# ★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА №1 (решение владельца 2026-08-23): при `Money == 0` плитка «0 Золото» НЕ
#   показывается, хотя оригинал её рисует (`questBuildRewardSet`, порог `>= 0`). Проверяется ОБА
#   состояния: по умолчанию плитки нет, а `asda.Quest.EtalonZeroMoney 1` возвращает эталонный вид —
#   так видно, что ключ отката живой, а не декоративный.
#
# ★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА №2 (решение владельца 2026-08-23, вечер): «СДВИГАТЬ ВСЕГДА, КОГДА ТАМ
#   ПУСТОТА». Проверяются значения `asda.Quest.CollapseRewardCells`: 3 (по умолчанию — любая
#   пустота, сдвиг сквозь границу рядов), 4 (сдвиг только внутри ряда), 2 (только нулевые
#   величины), 0 (эталонные фиксированные роли).
#
# ЗАПУСК: Tools\run_headless_quest_accept_win.bat
#   ★ Нужен ТИК редактора (`-ExecCmds="py …"`) и РЕНДЕР (без `-NullRHI`).
import glob
import os
import shutil

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/quest_accept_win"
SS_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
LEVEL = "/Game/Map/alpen/alpen"

WARMUP = 300
TRAVEL_SETTLE = 400
SETTLE = 40
MAX_TICKS = 40000


def log(m):
    import unreal
    unreal.log("[приём-задания-750] " + str(m))


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

    def case(tag, episode, quest, stage):
        """Один случай: показать окно, снять числа, снять кадр."""
        return [
            (lambda: cmd("asda.Npc.AcceptShow %d %d %d" % (episode, quest, stage)), SETTLE),
            (lambda: take_shot(tag), SETTLE),
            (lambda: harvest(tag), 5),
        ]

    PLAN = []
    PLAN += [
        (lambda: cmd("open /Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode"), TRAVEL_SETTLE),
        (lambda: cmd("asda.Msg.Verbose 1"), 5),
    ]
    PLAN += case("h214_money_exp", 2005, 4, 0)
    PLAN += case("h250_with_items", 2003, 2, 1)
    PLAN += case("h390_with_choice", 2028, 27, 1)
    PLAN += case("h390_choice_overflow", 2158, 157, 1)
    # ── НУЛЕВЫЕ ДЕНЬГИ: плитки быть не должно, а опыт обязан СДВИНУТЬСЯ на первое место (G1) ──
    PLAN += case("zero_money_exp_only", 2009, 8, 0)
    PLAN += case("zero_money_with_item", 2062, 61, 0)
    # ── и деньги, и опыт нули: первым обязан встать ПРЕДМЕТ ──
    PLAN += case("zero_money_and_exp", 2341, 340, 0)
    # ── НАСТОЯЩАЯ пустота (ItemCode1 = −1): дыры быть НЕ ДОЛЖНО, предмет поднимается в G3,
    #    окно 250. Все три пары данных, а не одна — охват не режем ──
    PLAN += case("true_gap_item2_only", 2078, 77, 2)
    PLAN += case("true_gap_quest79", 2080, 79, 2)
    PLAN += case("true_gap_quest81", 2082, 81, 2)
    # ── ключ схлопывания: 4 (только внутри ряда), 2 (лишь нулевые величины), 0 (эталон), назад 3 ──
    PLAN += [(lambda: cmd("asda.Quest.CollapseRewardCells 4"), 5)]
    PLAN += case("collapse_mode4_within_row", 2078, 77, 2)
    PLAN += case("collapse_mode4_zero_money", 2341, 340, 0)
    PLAN += [(lambda: cmd("asda.Quest.CollapseRewardCells 2"), 5)]
    PLAN += case("collapse_mode2_zero_only", 2078, 77, 2)
    PLAN += [(lambda: cmd("asda.Quest.CollapseRewardCells 0"), 5)]
    PLAN += case("collapse_off_etalon_roles", 2009, 8, 0)
    PLAN += [(lambda: cmd("asda.Quest.CollapseRewardCells 3"), 5)]
    PLAN += case("collapse_back_to_default", 2009, 8, 0)
    # ── ключ отката №1: эталонное поведение возвращает плитку «0 Золото» (схлопывать нечего) ──
    PLAN += [(lambda: cmd("asda.Quest.EtalonZeroMoney 1"), 5)]
    PLAN += case("zero_money_etalon_key", 2009, 8, 0)
    PLAN += [(lambda: cmd("asda.Quest.EtalonZeroMoney 0"), 5)]
    PLAN += case("zero_money_back_to_default", 2009, 8, 0)
    # Кнопка «ОК» 780 — окно должно погаснуть (эталон `SetVisible(confirmWindow, 0)`).
    PLAN += [
        (lambda: cmd("asda.Npc.AcceptOk"), SETTLE),
        (lambda: take_shot("after_ok"), SETTLE),
        (lambda: harvest("after_ok"), 5),
    ]

    finish = PH.make_pie_finish(st, les, log, "=== готово, снимков запрошено %d ===", count_shots=True)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 10 стр.)

    def on_tick(_dt):
        if st["busy"]:
            return
        st["busy"] = True
        try:
            st["tick"] += 1
            if st["tick"] > MAX_TICKS:
                unreal.log_error("[приём-задания-750] предел тиков")
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
            unreal.log_error("[приём-задания-750] ошибка:\n" + traceback.format_exc())
            finish()
        finally:
            st["busy"] = False

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    log("tick-callback зарегистрирован")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
