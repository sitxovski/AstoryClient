# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_stuck_gate_live_check.py — ЖИВАЯ (PIE) проверка ДЕФЕКТА «залип весь интерфейс».
#
# ЖАЛОБА ВЛАДЕЛЬЦА (30.08.2026): «открыл неимплементированное окно — и больше не открывается
#   никакое другое окошко». В логе клиента при этом повторялось:
#       [меню] чат-ввод в фокусе — открытие по кнопке 73405 подавлено (эталон)
#       [меню] кнопка 73405 (NEW_SYSTEMMAIN_COMUNITYBUT): просят ЗАКРЫТЬ окно COMMUNITY_WIN (6400)
#
# ДВЕ ПРИЧИНЫ, КОТОРЫЕ ПРОВЕРЯЕТ ЭТОТ ПРОГОН:
#   1) ЛАТЧ «идёт набор в чате» (`bInputActive`). Ставился в `OpenInput` (кнопка «Шёпот» окна
#      поиска отряда, пункт меню ника), снимался ТОЛЬКО по Esc. Игрок закрыл окно мышью — латч
#      остался взведённым НАВСЕГДА, а он стоит гейтом в системном меню, хотбаре и диспетчере
#      клавиш ⇒ мертвы ВСЕ окна и ВСЁ движение. Чиним сверкой с ФАКТИЧЕСКИМ фокусом Slate.
#   2) ЛАТЧ «окно открыто» у кнопки трея без окна (система не портирована): подсветку возвращать
#      было некому, поэтому следующее нажатие читалось как «ЗАКРЫТЬ».
#
# ЖМЁМ САМИ КНОПКИ И РЕАЛЬНЫЕ ПУТИ (`diagnostic-must-use-real-path`):
#   `asda.ChatWin.InputOpen` = ровно `OpenInput`, который зовёт кнопка «Шёпот»;
#   `asda.UI.SimClick`       = НАСТОЯЩЕЕ событие мыши Slate (так игрок и уводит фокус);
#   `asda.SysMenu.Click`     = `UAsdaSystemMenuSubsystem::ClickButton`, путь мышиного клика.
#
# ЗАПУСК:  Tools\run_headless_ui_stuck_gate.bat
import os
import shutil

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/ui_stuck_gate_live"
SS_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
LEVEL = "/Game/Map/alpen/alpen"

WARMUP = 300
TRAVEL_SETTLE = 400
SETTLE = 60
MAX_TICKS = 40000


def log(m):
    import unreal
    unreal.log("[залипание-UI] " + str(m))


def main():
    import unreal

    if os.path.isdir(SS_DIR):
        shutil.rmtree(SS_DIR, ignore_errors=True)
    if os.path.isdir(OUT_DIR):
        shutil.rmtree(OUT_DIR, ignore_errors=True)
    os.makedirs(OUT_DIR)

    # Окно PIE побольше: панель системного меню и окна прибиты к краям базовых 768.
    try:
        cls = unreal.find_object(None, "/Script/UnrealEd.LevelEditorPlaySettings")
        ps = unreal.get_default_object(cls)
        ps.set_editor_property("new_window_width", 1600)
        ps.set_editor_property("new_window_height", 1000)
        ps.set_editor_property("center_new_window", True)
        ps.set_editor_property("play_in_editor_start_mode",
                               unreal.PlayInEditorStartMode.NEW_EDITOR_WINDOW)
        log("размер окна PIE выставлен 1600x1000")
    except Exception as e:
        log("не удалось выставить размер окна PIE: %s" % e)

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
    log("уровень %s загружен" % LEVEL)
    les.editor_request_begin_play()
    log("PIE запущен")

    st = {"tick": 0, "step": -1, "next_at": WARMUP, "handle": None, "busy": False}

    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    world = PH.make_game_world(ues)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 5 стр.)

    cmd = PH.make_console_cmd(world, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 6 стр.)

    def mark(t):
        log("═══════════ %s ═══════════" % t)

    PLAN = [
        # Без game mode подсистемы мира не поднимаются и прогон даёт ложное «ничего нет».
        (lambda: cmd("open /Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode"), TRAVEL_SETTLE),

        # ★ БЕЗ ЭТИХ ДВУХ СТРОК НЕТ НИ ПАВНА, НИ ОКОН: спавн своего персонажа разрешён только при
        #   `CurrentMapId >= 0`, а офлайн менеджер загрузки это поле не заполняет (сервера нет).
        #   3 = alpen (`DT_MapTable`). Тик чата/меню тоже гейтится этими же двумя условиями.
        (lambda: cmd("Asda.Load.CurrentMap 3"), 10),
        (lambda: cmd("asda.World.FakeSelfSpawn 256 256 StuckGateProbe"), 200),

        (lambda: mark("0. БАЗА: флаг набора снят, кнопки трея на месте"), 2),
        (lambda: cmd("asda.ChatWin.InputState"), 20),
        (lambda: cmd("asda.SysMenu.Dump"), 30),

        (lambda: mark("1. ВОСПРОИЗВОДИМ ДЕФЕКТ: строку ввода открыли ТЕМ ЖЕ путём, что кнопка «Шёпот»"), 2),
        (lambda: cmd("asda.ChatWin.InputOpen 7"), 4),
        (lambda: cmd("asda.ChatWin.InputState"), 20),

        (lambda: mark("2. ИГРОК УВОДИТ ФОКУС НАСТОЯЩИМ КЛИКОМ МИМО ПОЛЯ (Esc не жмём — как владелец)"), 2),
        (lambda: cmd("asda.UI.SimClick 40 40 8"), 40),
        # Через отсрочку в 8 кадров флаг ОБЯЗАН сняться сам. До правки он оставался взведённым.
        (lambda: cmd("asda.ChatWin.InputState"), 30),

        (lambda: mark("3. НЕПОРТИРОВАННОЕ ОКНО: кнопка 73405 (COMMUNITY_WIN 6400) — первый клик"), 2),
        (lambda: cmd("asda.SysMenu.Click 73405"), 30),
        (lambda: cmd("asda.SysMenu.Dump"), 30),

        (lambda: mark("4. ТОТ ЖЕ ПУНКТ ЕЩЁ РАЗ: обязано снова быть «просят ОТКРЫТЬ», а не «ЗАКРЫТЬ»"), 2),
        (lambda: cmd("asda.SysMenu.Click 73405"), 30),
        (lambda: cmd("asda.SysMenu.Dump"), 30),

        (lambda: mark("5. ПОСЛЕ ОТКАЗА ОТКРЫВАЕМ ДРУГОЕ ОКНО — ИНВЕНТАРЬ (кнопка 73402)"), 2),
        (lambda: cmd("asda.SysMenu.Click 73402"), SETTLE),
        (lambda: cmd("asda.Inven.Dump"), 30),
        (lambda: cmd("shot showui"), 60),

        (lambda: mark("6. И ОКНО ЗВАНИЙ (клавиша T) — через доп-меню тем же путём"), 2),
        (lambda: cmd("asda.Title.Dump"), 30),

        (lambda: mark("7. КОНТРОЛЬ ОБРАТНОГО: пока фокус ДЕРЖИТСЯ, флаг набора обязан стоять"), 2),
        (lambda: cmd("asda.ChatWin.InputOpen 4"), 4),
        (lambda: cmd("asda.ChatWin.InputState"), 4),
        (lambda: cmd("asda.ChatWin.InputState"), 30),
    ]

    finish = PH.make_pie_finish_endplay_first(st, les, log, "ЗАВЕРШЕНО", frames="flat", ss_dir=SS_DIR, out_dir=OUT_DIR)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 17 стр.)

    def on_tick(_dt):
        if st["busy"]:
            return
        st["busy"] = True
        try:
            st["tick"] += 1
            if st["tick"] > MAX_TICKS:
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
            unreal.log_error("[залипание-UI] ошибка:\n" + traceback.format_exc())
            finish()
        finally:
            st["busy"] = False

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    log("tick-callback зарегистрирован")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
