# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_inven_use_fix_live_check.py — ЖИВАЯ (PIE) проверка двух дефектов инвентаря.
#
#   Д1: предмет «расширение хранилища» (special_itemlist.SubType == 34, коды 30/467/541)
#       применяется, но МОДАЛЬНОГО ОКНА нет. Причина: пакет 5456 разбирался, а делегат
#       `OnBagExpand` не имел ни одного подписчика.
#   Д2: выделение ячейки не сбрасывается, когда ячейка опустела.
#
# ★★★ ЗАЧЕМ ИМЕННО PIE. Оба дефекта — про КОД В РАНТАЙМЕ (подписка на делегат и показ окна),
#   офлайн-дамп ассетов их не видит в принципе. Проверяем теми же консольными командами,
#   которыми пользуется владелец.
#
# ЗАПУСК: Tools\run_headless_rhi_tick.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_inven_use_fix_live_check.py
import os

LEVEL = "/Game/Map/Login"
WARMUP = 240
SETTLE = 45
MAX_TICKS = 20000

# Реальные строки DT_item_list — иначе иконки не найдутся и запись будет неполной.
ITEM_A = 20577   # расходник (из живого лога хотбара владельца)


def log(m):
    import unreal
    unreal.log("[инв-фикс] " + str(m))


def main():
    import unreal

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

    unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
    les.editor_request_begin_play()
    log("PIE запущен, уровень %s" % LEVEL)

    st = {"tick": 0, "step": -1, "next_at": WARMUP, "handle": None, "busy": False}

    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    world = PH.make_game_world(ues)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 5 стр.)

    cmd = PH.make_console_cmd(world, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 6 стр.)

    # ── Д2: выделение сбрасывается при опустошении ячейки ──────────────────────────────────────
    def s_open():
        cmd("asda.Inven.Open 1")

    def s_fill():
        cmd("asda.Inven.Put 2 0 %d 5" % ITEM_A)   # ячейка расходников 2:0
        cmd("asda.Inven.Put 1 3 %d 1" % ITEM_A)   # ячейка снаряжения 1:3

    def s_sel_spend():
        cmd("asda.Inven.Tab 0")
        cmd("asda.Inven.Select 2 0")
        cmd("asda.Inven.SelDump")                  # ОЖИДАЕМ: «2:0 код=…», курсор виден

    def s_empty_spend():
        cmd("asda.Inven.Put 2 0 0 0")              # ячейка опустела (тот же путь, что применение)
        cmd("asda.Inven.SelDump")                  # ОЖИДАЕМ: «нет», курсор скрыт

    def s_sel_shell():
        cmd("asda.Inven.Tab 1")
        cmd("asda.Inven.Select 1 3")
        cmd("asda.Inven.SelDump")                  # ОЖИДАЕМ: «1:3 код=…»

    def s_empty_shell():
        cmd("asda.Inven.Put 1 3 0 0")
        cmd("asda.Inven.SelDump")                  # ОЖИДАЕМ: «нет»

    def s_keep_when_not_empty():
        # КОНТРОЛЬ: содержимое сменилось, но ячейка НЕ пуста — эталон выделение ДЕРЖИТ.
        cmd("asda.Inven.Put 1 3 %d 1" % ITEM_A)
        cmd("asda.Inven.Select 1 3")
        cmd("asda.Inven.Put 1 3 %d 7" % ITEM_A)    # то же место, другое количество
        cmd("asda.Inven.SelDump")                  # ОЖИДАЕМ: выделение ОСТАЛОСЬ «1:3»

    # ── Д1: 5456 → чат + однокнопочная модалка ────────────────────────────────────────────────
    def s_bag_accept():
        # Ёмкость склада ещё не приходила (-1) ⇒ первая же посылка её задаёт.
        cmd("asda.Inven.Recv5456 60")              # 60/30 = 2 вкладки ⇒ ПРИНЯТО, нотис 12833
        cmd("asda.Inven.Pump")

    def s_bag_reject_lower():
        cmd("asda.Inven.Recv5456 30")              # меньше текущего ⇒ 12834 (чат + модалка)
        cmd("asda.Inven.Pump")

    def s_bag_reject_rollback():
        # Сброс ёмкости на «одна вкладка» невозможен снизу, поэтому ветку отката проверяем
        #   с нулевого состояния отдельной сессией — здесь фиксируем только достижимые ветки.
        cmd("asda.Inven.Recv5456 90")              # больше текущего ⇒ ПРИНЯТО, 12833
        cmd("asda.Inven.Pump")

    def s_bag_cap():
        cmd("asda.Inven.Recv5456 270")             # доводим до потолка ⇒ принято
        cmd("asda.Inven.Pump")
        cmd("asda.Inven.Recv5456 300")             # уже 270 ⇒ 32010
        cmd("asda.Inven.Pump")

    def s_windows():
        cmd("asda.UI.DumpZones")                   # какие окна живы/видимы (модалка 1500 в их числе)

    PLAN = [s_open, s_fill, s_sel_spend, s_empty_spend, s_sel_shell, s_empty_shell,
            s_keep_when_not_empty, s_bag_accept, s_bag_reject_lower, s_bag_reject_rollback,
            s_bag_cap, s_windows]

    def finish():
        if st["handle"] is not None:
            try:
                unreal.unregister_slate_post_tick_callback(st["handle"])
            except Exception:
                pass
            st["handle"] = None
        try:
            les.editor_request_end_play()
        except Exception:
            pass
        log("ГОТОВО: шагов выполнено %d из %d" % (st["step"] + 1, len(PLAN)))

    def on_tick(_dt):
        if st["busy"]:
            return
        st["tick"] += 1
        if st["tick"] > MAX_TICKS:
            log("⚠ превышен лимит тиков")
            finish()
            return
        if st["tick"] < st["next_at"]:
            return
        st["step"] += 1
        if st["step"] >= len(PLAN):
            finish()
            return
        st["busy"] = True
        try:
            PLAN[st["step"]]()
        except Exception as e:
            log("⚠ шаг %d упал: %s" % (st["step"], e))
        finally:
            st["busy"] = False
        st["next_at"] = st["tick"] + SETTLE

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)


# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
