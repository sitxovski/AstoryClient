# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# ЖИВАЯ (PIE) проверка РАСКЛАДКИ КЛАВИШ: источник — ШТАТНЫЕ ActionMappings движка.
#   Разбор — Reports/keybinds_all_actions.md.
# ★ Проверка ПУТЁМ ИГРОКА: клавиши шлём через APlayerController::InputKey (те же привязки
#   и тот же PollKeys, что и от настоящей клавиатуры), а не вызовом ExecuteCommand.
import os
LEVEL = "/Game/Map/alpen/alpen"
WARMUP = 300
TRAVEL_SETTLE = 400
MAX_TICKS = 40000


def log(m):
    import unreal
    unreal.log("[клавиши-живое] " + str(m))


def main():
    import unreal
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
    les.editor_request_begin_play()
    log("PIE запущен")
    st = {"tick": 0, "step": -1, "next_at": WARMUP, "handle": None, "busy": False}

    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    cmd = PH.make_console_cmd_ues(ues, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 6 стр.)

    PLAN = [
        (lambda: cmd("open /Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode"), TRAVEL_SETTLE),
        # Источник раскладки и полный дамп (включая неназначенные и наши расширения).
        (lambda: cmd("asda.Keybinds"), 10),
        # Настоящие нажатия: I — рюкзак (cmd 33), M — карта (37), Q — журнал (34), C — окно 900 (30).
        # ⚠ PollKeys стреляет по ФРОНТУ ОТПУСКАНИЯ и работает ОПРОСОМ состояния, поэтому нажатие
        #   и отпускание обязаны прийтись на РАЗНЫЕ кадры — иначе он их не увидит вовсе.
        (lambda: cmd("asda.Keybinds.Hold I 1"), 8), (lambda: cmd("asda.Keybinds.Hold I 0"), 20),
        (lambda: cmd("asda.Keybinds.Hold M 1"), 8), (lambda: cmd("asda.Keybinds.Hold M 0"), 20),
        (lambda: cmd("asda.Keybinds.Hold Q 1"), 8), (lambda: cmd("asda.Keybinds.Hold Q 0"), 20),
        (lambda: cmd("asda.Keybinds.Hold C 1"), 8), (lambda: cmd("asda.Keybinds.Hold C 0"), 20),
    ]

    finish = PH.make_pie_finish_endplay_first(st, les, log, "готово")   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 10 стр.)

    def on_tick(_dt):
        if st["busy"]:
            return
        st["busy"] = True
        try:
            st["tick"] += 1
            if st["tick"] > MAX_TICKS:
                finish(); return
            if st["tick"] < st["next_at"]:
                return
            st["step"] += 1
            if st["step"] >= len(PLAN):
                finish(); return
            fn, wait = PLAN[st["step"]]
            fn()
            st["next_at"] = st["tick"] + wait
        except Exception:
            import traceback
            unreal.log_error("[клавиши-живое] ошибка:\n" + traceback.format_exc())
            finish()
        finally:
            st["busy"] = False

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
