# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# ЖИВАЯ (PIE) проверка ХОТКЕЯ ОКНА ЗВАНИЙ: клавиша «T» = cmd 38 «INTERFACE TITLE», JUI 18700.
#   Разбор раскладки — Reports/keybinds_all_actions.md; система ввода — ШТАТНЫЕ ActionMappings
#   движка (Config/DefaultInput.ini), НЕ Enhanced Input (в проекте его нет).
# ★ ЗАКОН diagnostic-must-use-real-path: обработчик НЕ зовём напрямую. Клавиша кладётся в
#   `UPlayerInput` (`asda.Keybinds.HoldSim`) — ровно туда, откуда её читает `PollKeys` через
#   `IsInputKeyDown`; дальше цепочка игроцкая: PollKeys → ExecuteCommand → OpenTitleWindow.
# ★ ЗАКОН false-negatives-method: удержание идёт ДВУМЯ способами — Slate (`asda.Keybinds.Hold`,
#   в headless давал ложное «нет») и UPlayerInput (`asda.Keybinds.HoldSim`).
# ★ Нажатие и отпускание — на РАЗНЫХ кадрах: PollKeys стреляет по фронту ОТПУСКАНИЯ.
LEVEL = "/Game/Map/alpen/alpen"
WARMUP = 300
TRAVEL_SETTLE = 400
MAX_TICKS = 40000


def log(m):
    import unreal
    unreal.log("[звания-клавиша] " + str(m))


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

    def ensure_pawn():
        """★★★ БЕЗ ПАВНА ОПРОС КЛАВИШ НЕ ИДЁТ ВОВСЕ.
        `AAsdaPlayerController::PlayerTick` зовёт `PollKeys()` ПОСЛЕ гейта `if (!Ch) return;`
        (AsdaPlayerController.cpp:2122), а `DefaultPawnClass = nullptr` — павна в порту спавнит
        `UAsdaWorldSubsystem` по серверному пакету 4003. В headless-PIE сервера нет ⇒ павна нет ⇒
        ни одна клавиша не сработала бы, ЧЕМ БЫ её ни слали.
        ⚠ Это и есть корень прошлого ЛОЖНОГО «клавиши в headless не воспроизводятся»
        (закон false-negatives-method): дело было не в способе доставки события.
        Здесь павн создаётся и берётся под управление вручную — ровно тот класс и та же
        `Possess`, что и в боевом пути подсистемы мира."""
        w = ues.get_game_world()
        pc = unreal.GameplayStatics.get_player_controller(w, 0)
        if pc is None:
            log("★ контроллера игрока нет"); return
        for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.AsdaPlayerCharacter):
            pc.possess(a)
            log("павн уже был: %s — взят под управление" % a.get_name())
            return
        # ★ Сначала объявить текущую карту: `OnSelfSpawn` спавнит только при `CurrentMapId >= 0`,
        #   иначе молча откладывает спавн до PostLoadMap, которого в этом прогоне уже не будет.
        cmd("Asda.Load.CurrentMap 3")
        # ★ ШТАТНЫЙ тестовый крюк порта: дёргает ТУ ЖЕ точку `OnSelfSpawn`, что и пакет 4003,
        #   то есть павн рождается боевым путём `TrySpawnLocalPlayer`, а не подделкой.
        cmd("Asda.World.FakeSelfSpawn 256 256 ТестЗвания")
        log("офлайн-спавн своего павна запрошен (эмуляция 4003)")

    def report_pawn():
        """Доказательство, что павн действительно есть: без него `PollKeys` не вызывается."""
        w = ues.get_game_world()
        pc = unreal.GameplayStatics.get_player_controller(w, 0)
        pawn = pc.get_controlled_pawn() if pc else None
        log("★ ПАВН ПОД УПРАВЛЕНИЕМ: %s" % (pawn.get_name() if pawn else "НЕТ — проверка недействительна"))

    # Один «удар по клавише»: зажать, подержать кадры, отпустить, дать кадры на реакцию.
    def tap(key):
        return [(lambda: cmd("asda.Keybinds.HoldSim %s 1" % key), 8),
                (lambda: cmd("asda.Keybinds.HoldSim %s 0" % key), 25)]

    PLAN = [
        (lambda: cmd("open /Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode"), TRAVEL_SETTLE),
        # Раскладка: подтвердить, что T сидит на cmd 38 и источник — штатные ActionMappings.
        (lambda: cmd("asda.Keybinds"), 15),
        # Павн — обязательное условие работы PollKeys (см. ensure_pawn).
        (ensure_pawn, 120),
        (report_pawn, 15),
        # Состояние окна ДО (должно быть закрыто).
        (lambda: cmd("asda.Title.State"), 10),
    ]
    PLAN += tap("T")                                     # 1-е нажатие → ОТКРЫТЬ
    PLAN += [(lambda: cmd("asda.Title.State"), 15)]
    PLAN += tap("T")                                     # 2-е нажатие → ЗАКРЫТЬ (тоггл)
    PLAN += [(lambda: cmd("asda.Title.State"), 15)]
    PLAN += tap("T")                                     # 3-е нажатие → снова ОТКРЫТЬ
    PLAN += [(lambda: cmd("asda.Title.State"), 15)]

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
            unreal.log_error("[звания-клавиша] ошибка:\n" + traceback.format_exc())
            finish()
        finally:
            st["busy"] = False

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
