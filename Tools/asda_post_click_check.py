# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_post_click_check.py — ЖИВАЯ (PIE) проверка ЖАЛОБЫ «приходится нажимать по два раза»
#   на окне ЭЛИТНОГО ПОЧТОВОГО ЯЩИКА (69000).
#
# ★★★ ЗАЧЕМ ОТДЕЛЬНЫЙ ПРОГОН. `asda_post_live_check.py` дёргает КОЛБЭКИ (`asda.Post.Take`,
#   `asda.Post.ClickIcon`) — они проходят мимо Slate и потому НЕ ловят дефект: ломается ровно
#   доставка события между нажатием и отпусканием. Здесь кликают НАСТОЯЩИЕ события Slate
#   (`asda.UI.SimClick` — нажатие, удержание N кадров, отпускание).
#
# ЧТО ДОКАЗЫВАЕТСЯ, A/B в ОДНОМ прогоне:
#   A. `asda.UI.LegacyRaiseOnPress 1` — старое поведение (подъём окна на НАЖАТИИ). Ожидание:
#      в логе `[z-порядок] … ★ ЛКМ ЗАЖАТА` между `[симклик] НАЖАТИЕ` и `[симклик] ОТПУСКАНИЕ`,
#      и НИ ОДНОЙ строки `[элитный ящик] 11006 отправлен` / `выбрана строка` — клик съеден.
#   B. `asda.UI.LegacyRaiseOnPress 0` — исправленное поведение. Ожидание: `[z-порядок]` идёт
#      ПОСЛЕ отпускания, а действие кнопки/галочки выполняется с ПЕРВОГО клика.
#
# Координаты кликов не выдумываются: их печатает `asda.Post.HitDump` (центр экранного
#   прямоугольника контрола). Скрипт читает их из лога редактора между шагами.
#
# ЗАПУСК: Tools\run_headless_rhi_tick.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_post_click_check.py

LEVEL = "/Game/Map/Login"
WARMUP = 240
SETTLE = 45
MAX_TICKS = 30000

# Тело эмуляции 11005: 3 непустых заказа реальными кодами `DT_item_list`.
SIM = "asda.Post.Sim 1 10000:1:1 10001:1:1 10002:1:3"


def log(m):
    import unreal
    unreal.log("[ящик-клики] " + str(m))


def main():
    import unreal

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

    unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
    les.editor_request_begin_play()
    log("PIE запущен")

    st = {"tick": 0, "step": -1, "next_at": WARMUP, "handle": None, "busy": False, "pts": {}}

    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    world = PH.make_game_world(ues)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 5 стр.)

    cmd = PH.make_console_cmd(world, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 6 стр.)

    # ── Кликаем ПО ИМЕНИ ВИДЖЕТА (`asda.UI.SimClickWidget`): центр считает сам движок, переносить
    #    координаты из лога не надо. Имена — из `asda.Post.HitDump` того же прогона.
    WIN = "Primium_Item_post_WIN"
    RADIO0 = "Smelting_V7Btn_2_152_0"   # 69081, элемент строки 0 («галочка»)
    OKBTN = "Primium_Item_post_OK"      # 69082 «Положить в рюкзак»
    ICON0 = "CELL_HIT00"                # прозрачный оверлей клика по иконке строки 0

    def click(widget, hold=10):
        cmd("asda.UI.SimClickWidget %s %s %d" % (WIN, widget, hold))

    # ── шаги ───────────────────────────────────────────────────────────────────────────────
    def s_open():
        cmd("asda.Post.Open 1")

    def s_sim():
        cmd(SIM)

    def s_hit():
        cmd("asda.Post.HitDump")

    def s_click_icon():
        # Контрольный клик по прозрачному оверлею иконки — он же проверка исправления дефекта A.
        click(ICON0)

    # ══ A: старое поведение (подъём на нажатии) ══
    def s_legacy_on():
        cmd("asda.UI.LegacyRaiseOnPress 1")

    def s_legacy_lower():
        # Опустить ящик под инвентарь: инвентарь поднимаем наверх «программно», как это делает
        #   любой клик по нему. Тогда следующий клик по ящику обязан вызвать подъём ящика.
        cmd("asda.UI.Front INVEN_WIN")

    def s_legacy_click_radio():
        click(RADIO0)

    def s_legacy_model():
        cmd("asda.Post.Dump model")

    def s_legacy_lower2():
        cmd("asda.UI.Front INVEN_WIN")

    def s_legacy_click_ok():
        click(OKBTN)

    # ══ B: исправленное поведение ══
    def s_fix_on():
        cmd("asda.UI.LegacyRaiseOnPress 0")

    def s_fix_resim():
        cmd(SIM)

    def s_fix_lower():
        cmd("asda.UI.Front INVEN_WIN")

    def s_fix_click_radio():
        click(RADIO0)

    def s_fix_model():
        cmd("asda.Post.Dump model")

    def s_fix_lower2():
        cmd("asda.UI.Front INVEN_WIN")

    def s_fix_click_ok():
        click(OKBTN)

    def s_final_model():
        cmd("asda.Post.Dump model")

    def s_probe_lower():
        cmd("asda.UI.Front INVEN_WIN")

    def s_probe():
        # Проба: подъём окна и сравнение геометрии «галочки»/кнопки ДО и ПОСЛЕ — в одном вызове.
        cmd("asda.Post.ProbeRaise")


    # ══ ДЕФЕКТ 2: приз с выбором «профессия × уровень» не должен появляться в сумке до выбора ══
    #   Проигрывается РОВНО случай владельца: 74532 `High Vampire Set` (коробка, LinkWordCode 296,
    #   в группе 9 записей = 3 профессии × 3 уровня); выбор «Лучник» даёт код 74533.
    def s_d2_on():
        cmd("asda.Post.HideUntilChoice 1")

    def s_d2_apply():
        cmd("asda.Post.SimApply 1 74532 3")

    def s_d2_inven():
        cmd("asda.Inven.Dump")

    def s_d2_pick():
        cmd("asda.Gacha.VarCombo job 0 1")

    def s_d2_ok():
        cmd("asda.Gacha.VarOk")

    def s_d2_srv():
        cmd("asda.Gacha.SimPrizeApply 74533 1 0 1")

    def s_d2_inven2():
        cmd("asda.Inven.Dump")

    # Отмена: окно закрыли крестиком — отложенная запись обязана лечь в сумку вариантом по умолчанию.
    def s_d2_apply2():
        cmd("asda.Post.SimApply 1 74532 3")

    def s_d2_close():
        cmd("asda.Gacha.VarClose")

    def s_d2_inven3():
        cmd("asda.Inven.Dump")

    PLAN = [s_open, s_sim, s_hit,
            s_legacy_on, s_legacy_lower, s_legacy_click_radio, s_legacy_model,
            s_legacy_lower2, s_legacy_click_ok, s_legacy_model,
            s_fix_on, s_fix_resim, s_fix_lower, s_fix_click_radio, s_fix_model,
            s_fix_lower2, s_fix_click_ok, s_final_model,
            s_fix_lower, s_click_icon, s_final_model,
            s_probe_lower, s_probe,
            s_d2_on, s_d2_apply, s_d2_inven, s_d2_pick, s_d2_ok, s_d2_srv, s_d2_inven2,
            s_d2_apply2, s_d2_close, s_d2_inven3]

    finish = PH.make_pie_finish(st, les, log, "=== готово ===", handle_is_not_none=True)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 10 стр.)

    def on_tick(_dt):
        if st["busy"]:
            return
        st["busy"] = True
        try:
            st["tick"] += 1
            if st["tick"] > MAX_TICKS:
                unreal.log_error("[ящик-клики] предел тиков")
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
            unreal.log_error("[ящик-клики] ошибка:\n" + traceback.format_exc())
            finish()
        finally:
            st["busy"] = False

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    log("tick-callback зарегистрирован")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
