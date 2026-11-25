# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_post_live_check.py — ЖИВАЯ (PIE) проверка ЭЛИТНОГО ПОЧТОВОГО ЯЩИКА (окно 69000).
#
# ★★★ ЗАЧЕМ ИМЕННО ЖИВАЯ. Офлайн-зонд по .uasset видит только разметку и НЕ видит последнего шага
#   (привязку контролов кодом, показ окна, заливку иконок, выбор строки). На премиум-гаче это уже
#   стоило четырёх итераций: офлайн всё сходилось, а в PIE окно было пустым. Поэтому здесь
#   поднимается настоящий PIE и дёргаются РОВНО ТЕ ЖЕ консольные команды, которыми пользуется
#   владелец (`asda.Post.*` из `AsdaPremiumPostSubsystem.cpp`).
#
# ЧТО ПРОВЕРЯЕТСЯ (каждый пункт даёт строку-доказательство в логе):
#   1. окно 69000 открывается и его контролы разобраны       — `asda.Post.Dump controls`
#   2. приход 11005 разбирается, иконки заливаются           — `asda.Post.Sim` + `asda.Post.Dump model`
#   3. ДЕФЕКТ C: список из 9 записей обрезается до 7         — лог «★ 11005 принёс 9 записей…»
#   4. ДЕФЕКТ B: Count = 9 клампится до 7                    — лог «★ строка N: сервер прислал Count=9…»
#   5. ДЕФЕКТ A: клик по иконке МЕНЯЕТ выбранную строку      — `asda.Post.ClickIcon` (выбор X -> Y)
#   6. окно реально рисуется на экране                       — снимок `post_window.png`
#
# ЗАПУСК: Tools\run_headless_rhi_tick.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_post_live_check.py
#   (именно `_tick`: без тика редактора PIE не крутит кадры и снимков не будет).
import glob
import os
import shutil

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/post_live"
SS_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
LEVEL = "/Game/Map/Login"

WARMUP = 240        # прогрев PIE: шейдеры, стриминг, GameMode
SETTLE = 60         # между шагами
MAX_TICKS = 20000

# Тело эмуляции 11005. 9 записей при потолке 7 (дефект C) и Count = 9 у первой (дефект B).
#   Коды предметов — РЕАЛЬНЫЕ строки `DT_item_list` (10000…10008), иначе иконок не будет вовсе.
SIM = "asda.Post.Sim 1 10000:9:1 10001:3:1 10002:2:3 10003:1:1 10004:1:1 10005:1:1 10006:1:1 10007:1:1 10008:1:1"


def log(m):
    import unreal
    unreal.log("[ящик-живой] " + str(m))


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

    take_shot = PH.make_take_shot(cmd, st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 4 стр.)

    harvest = PH.make_harvest(SS_DIR, OUT_DIR, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 10 стр.)

    # ── шаги ────────────────────────────────────────────────────────────────────────────────
    def s_open():
        # Открываем ВМЕСТЕ с инвентарём — то самое отступление по требованию владельца.
        cmd("asda.Post.Open 1")

    def s_dump_controls():
        cmd("asda.Post.Dump controls")

    def s_sim():
        # Пункты 2/3/4: разбор 11005 + оба клампа.
        cmd(SIM)

    def s_dump_model_1():
        cmd("asda.Post.Dump model")

    def s_shot_window():
        take_shot("post_window")

    def s_get_window():
        harvest("post_window")

    def s_click_icon():
        # Пункт 5, дефект A. Ячейка 21 = строка 3, колонка 0. Эталонный авто-выбор ставит НИЖНЮЮ
        #   непустую строку (обход k = 6..0 с выходом на первой) — при 7 строках это строка 6,
        #   значит после клика по иконке строки 3 выбор ОБЯЗАН стать 3.
        cmd("asda.Post.ClickIcon 21")

    def s_dump_model_2():
        cmd("asda.Post.Dump model")

    def s_shot_selected():
        take_shot("post_selected_row3")

    def s_get_selected():
        harvest("post_selected_row3")

    def s_click_last():
        # Строка 1 (ячейка 7 = строка 1, колонка 0) — второй клик подряд: проверяем, что выбор
        #   действительно переносится кликом, а не «вернулся к авто-выбору».
        #   Строки в `.JUI` стоят на 0/41/82/122/162/202/242 — шаг НЕ ровно 40, поэтому Y рамки
        #   берётся из живой позиции группы строки, а не из формулы «40·i».
        cmd("asda.Post.ClickIcon 7")

    def s_shot_last():
        take_shot("post_selected_row1")

    def s_get_last():
        harvest("post_selected_row1")

    def s_notice():
        # Иконка-уведомление слота 15: сначала закрываем окно, потом шлём непустой список —
        #   эталонное условие «пришёл непустой 11005 при закрытом окне ящика».
        cmd("asda.Post.Close")

    def s_notice_sim():
        cmd(SIM)

    def s_shot_notice():
        take_shot("post_notice_icon")

    def s_get_notice():
        harvest("post_notice_icon")

    # ══════ ЗАМЕЧАНИЯ ВЛАДЕЛЬЦА 2026-08-18 (пункты 1..6) ══════
    #   Проверяем ЖИВЫМ приходом 11007 (`asda.Post.SimApply`), потому что именно этот пакет решает,
    #   открывать ли окно 68500. Три предмета выбраны по ДАННЫМ `item_list` (разбор — в отчёте
    #   `Reports/elite_mailbox_fix.md` §0):
    #     68431 `Crafting Package(2)` — subType 200, LinkWordCode 199, группа = ТОЛЬКО он сам
    #                                   ⇒ выбора нет ⇒ окно открываться НЕ должно (случай владельца);
    #     51925 `Package - Christmas Set` — LinkWordCode 134, группа 42 записи,
    #                                   уровни 5/10/20/30/40/50/60, профессии Воин/Лучник/Маг
    #                                   ⇒ окно ОБЯЗАНО открыться с этими списками;
    #     64584 `Повязка героя` — экипировка (itemType 0) с SetCode 251, группа 6 записей,
    #                                   уровни 1/40/60 ⇒ второй маршрут отбора (по SetCode).
    def s_apply_nochoice():
        cmd("asda.Post.SimApply 1 68431 3")

    def s_var_dump_nochoice():
        cmd("asda.Gacha.VarDump")

    def s_apply_choice():
        cmd("asda.Post.SimApply 1 51925 3")

    def s_var_dump_choice():
        cmd("asda.Gacha.VarDump")

    def s_shot_var():
        take_shot("post_variant_window")

    def s_get_var():
        harvest("post_variant_window")

    def s_combo_open():
        # Пункты 4/5: раскрыть список профессий и выбрать второй пункт ТЕМ ЖЕ кодом, что и клик.
        cmd("asda.Gacha.VarCombo job 1")

    def s_combo_pick():
        cmd("asda.Gacha.VarCombo job 0 1")

    def s_combo_level():
        cmd("asda.Gacha.VarCombo level 1 3")

    def s_apply_equip():
        # Второй маршрут отбора кандидатов — по `SetCode` (не коробка). 68493 `Butler, Maid Hair`:
        #   itemType 0 (шлем), SetCode 2115, LinkWordCode 0 (⇒ `canShowGachaPopup` истинно),
        #   в наборе уровни 1/40/60 и три профессии.
        #   ⚠ Первый выбранный пример 64584 «Повязка героя» ОТСЕИВАЕТ САМ ЭТАЛОН: у него
        #     LinkWordCode = 67 ≤ 0x63, а условие `canShowGachaPopup` требует 0 либо > 0x63.
        cmd("asda.Post.SimApply 1 68493 3")

    def s_var_dump_equip():
        cmd("asda.Gacha.VarDump")

    def s_shot_equip():
        take_shot("post_variant_equip")

    def s_get_equip():
        harvest("post_variant_equip")

    def s_shot_combo():
        take_shot("post_variant_combo_open")

    def s_get_combo():
        harvest("post_variant_combo_open")

    def s_var_dump_pick():
        cmd("asda.Gacha.VarDump")

    def s_world_ready():
        # Пункт 6: тот же путь, что делегат OnWorldReady (4055/4056) — вход в игру и смена карты.
        cmd("asda.Post.SimWorldReady")

    def s_notice_state():
        cmd("asda.Post.NoticeState")

    # ══════ ЗАМЕЧАНИЯ ВЛАДЕЛЬЦА 2026-08-19 (два бага живого запуска) ══════
    #   БАГ 1. «Окно не ждёт выбора, предмет уже в инвентаре, выбор ни на что не влияет».
    #   Разбор — `Reports/elite_mailbox_fix2.md` §1. Причина: `UAsdaPremiumGachaSubsystem::Tick`
    #   выходил по гейту `bSceneOpen` РАНЬШЕ применения ответа 6582, а на маршруте «из элитного
    #   ящика» сцена гачи закрыта ⇒ обмен варианта сервером проводился, а модель инвентаря его не
    #   видела. Проигрываем ровно случай владельца из живого лога:
    #     74532 `High Vampire Set` — коробка (subType 200, LinkWordCode 296), в группе 9 записей
    #           = 3 профессии × 3 уровня; 74533 — та же коробка для ЛУЧНИКА.
    def s_b1_apply():
        cmd("asda.Post.SimApply 1 74532 3")

    def s_b1_var_dump():
        cmd("asda.Gacha.VarDump")

    def s_b1_pick_job():
        # Выбрать вторую профессию (Лучник) — тем же кодом, что клик по пункту списка.
        cmd("asda.Gacha.VarCombo job 0 1")

    def s_b1_cell_before():
        cmd("asda.Inven.Dump")

    def s_b1_ok():
        # Кнопка ОК (68511): резолв варианта + отправка 6581.
        cmd("asda.Gacha.VarOk")

    def s_b1_srv():
        # Ответ сервера 6582 при ЗАКРЫТОЙ сцене гачи — ровно то, что раньше терялось.
        cmd("asda.Gacha.SimPrizeApply 74533 1 0 1")

    def s_b1_cell_after():
        cmd("asda.Inven.Dump")

    #   БАГ 2. «Иконки после входа в игру нет / её не видно». Вторая, независимая причина —
    #   родительская группа `P_ICON0X_N_grp` остаётся COLLAPSED (в `.JUI` у 73350…73356
    #   `Visible = "0"`, а дети есть ⇒ импортёр сворачивает ГРУППУ, а код включал только иконку).
    def s_b2_notice_add():
        cmd("asda.Quick.Notice 15")

    def s_b2_notice_dump():
        cmd("asda.Quick.NoticeDump")

    def s_b2_shot():
        take_shot("post_notice_icon_visible")

    def s_b2_get():
        harvest("post_notice_icon_visible")

    def s_drag_dump():
        # Пункт 3: ручки перетаскивания обоих окон в реестре (данные `DA_JuiWindowMeta`).
        cmd("asda.UI.DumpDrag")

    PLAN = [s_open, s_dump_controls, s_sim, s_dump_model_1, s_shot_window, s_get_window,
            s_click_icon, s_dump_model_2, s_shot_selected, s_get_selected,
            s_click_last, s_shot_last, s_get_last,
            s_apply_nochoice, s_var_dump_nochoice,
            s_apply_choice, s_var_dump_choice, s_shot_var, s_get_var,
            s_combo_open, s_shot_combo, s_get_combo, s_combo_pick, s_combo_level, s_var_dump_pick,
            s_apply_equip, s_var_dump_equip, s_shot_equip, s_get_equip,
            s_drag_dump, s_world_ready, s_notice_state,
            s_notice, s_notice_sim, s_shot_notice, s_get_notice,
            # ── баги живого запуска 2026-08-19 ──
            s_b1_apply, s_b1_var_dump, s_b1_pick_job, s_b1_cell_before,
            s_b1_ok, s_b1_srv, s_b1_cell_after,
            s_b2_notice_add, s_b2_notice_dump, s_b2_shot, s_b2_get]

    finish = PH.make_pie_finish(st, les, log, "=== готово, снимков запрошено %d ===", handle_is_not_none=True, count_shots=True)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 10 стр.)

    def on_tick(_dt):
        if st["busy"]:
            return
        st["busy"] = True
        try:
            st["tick"] += 1
            if st["tick"] > MAX_TICKS:
                unreal.log_error("[ящик-живой] предел тиков")
                finish()
                return
            if st["tick"] < st["next_at"]:
                return
            st["step"] += 1
            if st["step"] >= len(PLAN):
                finish()
                return
            PLAN[st["step"]]()
            st["next_at"] = st["tick"] + SETTLE
        except Exception:
            import traceback
            unreal.log_error("[ящик-живой] ошибка:\n" + traceback.format_exc())
            finish()
        finally:
            st["busy"] = False

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    log("tick-callback зарегистрирован")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
