# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_player_menu_live_check.py — ЖИВАЯ (PIE) проверка КОНТЕКСТНОГО МЕНЮ ПО ЧУЖОМУ ИГРОКУ
#   (JUI-окно `TRANTOOL_WIN` 2000). Разбор эталона — `Reports/player_context_menu.md`.
#
# ★★★ ЗАЧЕМ ЖИВАЯ, А НЕ ОФЛАЙН-ДАМП (закон false-negatives-method). Окно строится подсистемой
#   `UAsdaPlayerMenuSubsystem` в РАНТАЙМЕ: подписи берутся из `DT_GameText`, видимость кнопок
#   ставится кодом (в ассете часть узлов Collapsed), позиция считается от курсора. Офлайн-дамп
#   ассета показывает ДАННЫЕ, а не то, что увидит игрок.
#
# ★★★ ЖМЁМ САМИ КНОПКИ (закон «диагностика обязана жать саму кнопку»): команда
#   `asda.PlayerMenu.Click <idnum>` находит узел `UButton` в ЖИВОМ виджете и вызывает его
#   `OnClicked.Broadcast()` — тот же путь, что у мыши игрока. Обработчики напрямую НЕ зовём.
#
# ЧТО ПРОВЕРЯЕТСЯ (каждый пункт оставляет строку-доказательство в логе):
#   1. меню 2000 открывается (`asda.PlayerMenu.Open <userNum>`) и пишет свои координаты;
#   2. в окне ровно 12 видимых пунктов + заголовок; мёртвые 2053/2060 скрыты ВМЕСТЕ С ПОДПИСЯМИ;
#   3. подписи взяты из таблицы текста (совпадают с loc 6800/6802/12819/6801/6803/6804/5061/
#      6921/6958/6959/6963/5035) — печатаются построчно;
#   4. КАЖДАЯ живая кнопка нажимается своим `OnClicked` и оставляет свою строку в логе;
#   5. меню закрывается пунктом «Закрыть» (2052);
#   6. кадр `player_menu.png`.
#
# ЗАПУСК:  Tools\run_headless_player_menu.bat
import os
import shutil

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/player_menu_live"
SS_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
LEVEL = "/Game/Map/alpen/alpen"

WARMUP = 300
TRAVEL_SETTLE = 400
SETTLE = 60
MAX_TICKS = 40000

# Пункты меню: (idnum, имя узла кнопки, имя узла подписи, ожидаемый loc-id)
ITEMS = [
    (2001, "TRANTOOL_BUT01", "TRANTOOL_BUT01_Text", 6800),
    (2003, "TRANTOOL_BUT03", "TRANTOOL_BUT03_Text", 6802),
    (2055, "TRANTOOL_BUT09", "TRANTOOL_BUT09_Text", 12819),
    (2002, "TRANTOOL_BUT02", "TRANTOOL_BUT02_Text", 6801),
    (2004, "TRANTOOL_BUT04", "TRANTOOL_BUT04_Text", 6803),
    (2005, "TRANTOOL_BUT05", "TRANTOOL_BUT05_Text", 6804),
    (2006, "TRANTOOL_BUT06", "TRANTOOL_BUT06_Text", 5061),
    (2054, "TRANTOOL_BUT08", "TRANTOOL_BUT08_Text", 6921),
    (2062, "TRANTOOL_BUT11", "TRANTOOL_BUT11_Text", 6958),
    (2063, "TRANTOOL_BUT12", "TRANTOOL_BUT12_Text", 6959),
    (2070, "TRANTOOL_BUT13", "TRANTOOL_BUT13_Text", 6963),
    (2052, "TRANTOOL_BUT07", "TRANTOOL_BUT07_Text", 5035),
]
DEAD = [("TRANTOOL_BUTDISABLE", "TRANTOOL_BUTDISABLE_Text", 2053),
        ("TRANTOOL_BUT10", "TRANTOOL_BUT10_Text", 2060)]


def log(m):
    import unreal
    unreal.log("[меню-живое] " + str(m))


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

    st = {"tick": 0, "step": -1, "next_at": WARMUP, "handle": None, "busy": False}

    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    world = PH.make_game_world(ues)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 5 стр.)

    cmd = PH.make_console_cmd(world, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 6 стр.)

    # ★ Состояние ЖИВОГО окна печатает САМА подсистема (asda.PlayerMenu.Dump): Python-обёртки
    #   `WidgetBlueprintLibrary` в этой сборке нет (проверено прогоном — AttributeError), а дамп
    #   на стороне C++ читает те же поля, что видит игрок.
    def dump_menu(tag):
        log("=== дамп: %s ===" % tag)
        cmd("asda.PlayerMenu.Dump")

    def take_shot(tag):
        cmd("HighResShot 1280x720")
        log("кадр запрошен: %s" % tag)

    # ── шаги ────────────────────────────────────────────────────────────────────────────────────
    def s_travel():
        cmd("open /Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode")

    def s_open():
        cmd("asda.PlayerMenu.Open 7")

    def s_dump_open():
        dump_menu("после открытия")

    def s_shot():
        take_shot("player_menu")

    # ★★★ ПРОВЕРКА ПУТЁМ ИГРОКА: ставим фейкового соседа и делаем НАСТОЯЩИЙ правый клик по нему
    #   (нажатие+отпускание идут через привязки ввода контроллера, а не через вызов обработчика).
    def s_peer():
        cmd("asda.PlayerMenu.FakePeer 7 Сосед")

    def s_realclick():
        cmd("asda.PlayerMenu.RightClick 7")

    steps = [(s_travel, TRAVEL_SETTLE), (s_peer, 60), (s_realclick, 30),
             (lambda: dump_menu("после НАСТОЯЩЕГО ПКМ"), 5), (s_shot, 60),
             (s_open, SETTLE), (s_dump_open, 5)]
    # Каждую живую кнопку жмём ЕЁ СОБСТВЕННЫМ OnClicked, кроме «Закрыть» — он идёт последним.
    for (idnum, node, cap, loc) in ITEMS:
        if idnum == 2052:
            continue
        steps.append(((lambda i: (lambda: cmd("asda.PlayerMenu.Click %d" % i)))(idnum), 10))
        steps.append(((lambda i: (lambda: cmd("asda.PlayerMenu.Open 7")))(idnum), 10))
    steps.append((lambda: cmd("asda.PlayerMenu.Click 2052"), 10))
    steps.append((lambda: dump_menu("после Закрыть"), 5))

    PLAN = steps

    def finish():
        try:
            for f in os.listdir(SS_DIR):
                if f.lower().endswith(".png"):
                    shutil.copy2(os.path.join(SS_DIR, f), os.path.join(OUT_DIR, f))
                    log("кадр сохранён: %s" % f)
        except Exception:
            pass
        try:
            les.editor_request_end_play()
        except Exception:
            pass
        if st["handle"]:
            unreal.unregister_slate_post_tick_callback(st["handle"])
            st["handle"] = None
        log("готово")
        unreal.SystemLibrary.quit_editor()

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
            unreal.log_error("[меню-живое] ошибка:\n" + traceback.format_exc())
            finish()
        finally:
            st["busy"] = False

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    log("tick-callback зарегистрирован")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
