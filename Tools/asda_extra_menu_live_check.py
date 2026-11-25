# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_extra_menu_live_check.py — ЖИВАЯ (PIE) проверка ДОП-МЕНЮ «Прочее» (окно `ETC_NEW_WIN` 74600).
#   Разбор эталона и таблица нотисов — `Reports/extra_menu.md`.
#
# ★★★ ЗАЧЕМ ЖИВАЯ, А НЕ ОФЛАЙН-ДАМП (закон false-negatives-method). Всё содержимое окна строится
#   в РАНТАЙМЕ из `DT_ExtraMenu`: подписи перетирают текст `.JUI`, иконки режутся из атласа
#   `extra_menu`, лишние строки гасятся, клик-зоны считаются по ЖИВОЙ геометрии (строки лежат
#   на третьем уровне вложенности). Офлайн-дамп ассета всего этого не видит.
#
# ★★★ ЖМЁМ САМИ КНОПКИ (закон «диагностика обязана жать саму кнопку»):
#   `asda.ExtraMenu.MenuButton` → `UAsdaSystemMenuSubsystem::ClickButton(73409)` — то есть НАСТОЯЩАЯ
#   кнопка трея, а не наш `ToggleWindow`; `asda.ExtraMenu.Click N` → `UAsdaIconCellGrid::SimulateClick`,
#   то есть тот же колбэк, что у прозрачной кнопки-оверлея строки. Обработчики напрямую НЕ зовём,
#   а состояние читаем дампами, которые смотрят на ВИДЖЕТЫ (видимость, живая геометрия, текст).
#
# ЧТО ПРОВЕРЯЕТСЯ (каждый пункт оставляет строку-доказательство в логе):
#   1. кнопка трея 73409 существует и до неё доходит клик       — `asda.SysMenu.Dump`
#   2. нажатие кнопки ОТКРЫВАЕТ окно 74600                      — `asda.ExtraMenu.Dump` (видимо=1)
#   3. живых строк ровно 11, остальные 5 погашены               — там же, построчно
#   4. подписи взяты ИЗ ТАБЛИЦЫ, а не из `.JUI`                 — там же, «подпись на экране»
#   5. клик-зоны стоят поверх строк (живая геометрия, не слот)  — там же, блок «ЖИВЫЕ клик-зоны»
#   6. клик по строке 9 (элитный ящик) ОТКРЫВАЕТ окно 69000     — `asda.Post.Dump controls`
#   7. клик по строке 0 (звания) даёт ЧЕСТНЫЙ отказ             — строка «НЕ ПОРТИРОВАНА» + нотис
#   8. слот хотбара типа 5 несёт иконку из атласа доп-меню
#      и запускает пункт                                        — `asda.Quick.Set/Dump/Use`
#   9. повторное нажатие кнопки трея ЗАКРЫВАЕТ окно             — `asda.ExtraMenu.Dump` (видимо=0)
#  10. кадры окна
#
# ЗАПУСК:  Tools\run_headless_extra_menu.bat
import os
import shutil

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/extra_menu_live"
SS_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
LEVEL = "/Game/Map/alpen/alpen"

WARMUP = 300
TRAVEL_SETTLE = 400
SETTLE = 60
MAX_TICKS = 40000


def log(m):
    import unreal
    unreal.log("[доп-меню-живое] " + str(m))


def main():
    import unreal

    if os.path.isdir(SS_DIR):
        shutil.rmtree(SS_DIR, ignore_errors=True)
    if os.path.isdir(OUT_DIR):
        shutil.rmtree(OUT_DIR, ignore_errors=True)
    os.makedirs(OUT_DIR)

    # ★★★ ОКНО PIE ПОБОЛЬШЕ. Окно доп-меню в базе 768 имеет высоту 368, а вьюпорт PIE по умолчанию
    #   мал: нижняя строка левой колонки физически выходит за его границу, Slate её отсекает, и
    #   прогон даёт ЛОЖНОЕ «строка не рисуется» (закон false-negatives-method).
    try:
        # ★ Класс не экспортирован в python как `unreal.LevelEditorPlaySettings` — берём по имени.
        cls = unreal.find_object(None, "/Script/UnrealEd.LevelEditorPlaySettings")
        ps = unreal.get_default_object(cls)
        ps.set_editor_property("new_window_width", 1600)
        ps.set_editor_property("new_window_height", 1000)
        ps.set_editor_property("center_new_window", True)
        ps.set_editor_property("play_in_editor_start_mode",
                               unreal.PlayInEditorStartMode.NEW_EDITOR_WINDOW)
        log("размер окна PIE выставлен 1600x1000, режим — отдельное окно")
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

    # ── план ────────────────────────────────────────────────────────────────────────────────────
    PLAN = [
        # ★ Без `?game=/Script/AsdaReborn.AsdaWorldGameMode` подсистемы мира не поднимаются и
        #   прогон даёт ложное «ничего нет».
        (lambda: cmd("open /Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode"), TRAVEL_SETTLE),

        (lambda: mark("1. КНОПКА ТРЕЯ 73409: доходит ли до неё клик"), 2),
        (lambda: cmd("asda.SysMenu.Dump"), 20),

        (lambda: mark("2. состояние ДО открытия"), 2),
        (lambda: cmd("asda.ExtraMenu.Dump"), 20),

        (lambda: mark("3. ЖМЁМ САМУ КНОПКУ ТРЕЯ 73409"), 2),
        (lambda: cmd("asda.ExtraMenu.MenuButton"), SETTLE),
        (lambda: cmd("asda.ExtraMenu.Dump"), 30),
        (lambda: cmd("shot showui"), 60),

        (lambda: mark("4. КЛИК ПО СТРОКЕ 9 «Элитный почтовый ящик» (единственная портированная)"), 2),
        (lambda: cmd("asda.ExtraMenu.Click 9"), SETTLE),
        (lambda: cmd("asda.Post.Dump controls"), 30),
        (lambda: cmd("shot showui"), 60),

        (lambda: mark("5. КЛИК ПО СТРОКЕ 0 «Меню званий» — ждём ЧЕСТНЫЙ ОТКАЗ, а не тишину"), 2),
        (lambda: cmd("asda.ExtraMenu.Click 0"), 30),
        (lambda: mark("6. КЛИК ПО СТРОКЕ 10 «Добыча» — тоже отказ"), 2),
        (lambda: cmd("asda.ExtraMenu.Click 10"), 30),

        (lambda: mark("7. СЛОТ ХОТБАРА ТИПА 5 (пункт доп-меню) — иконка и запуск"), 2),
        # тип 5 = ExtraMenu, «ячейка» = индекс записи (9 — элитный ящик)
        (lambda: cmd("asda.Quick.Set 0 5 0 9 0 0"), 30),
        (lambda: cmd("asda.Quick.Dump"), 30),
        (lambda: cmd("asda.Quick.Use 0"), 30),

        (lambda: mark("7а. НАСТОЯЩИЙ клик Slate по координате: строка 9 (контроль) и строка 7"), 2),
        # Центры строк по ЖИВОЙ геометрии дампа: строка 9 (355,118)-(558,157) и строка 6
        #   (151,323)-(354,362). Клик идёт через Slate, а не через колбэк кнопки.
        # ⚠ Строку 7 (8-я левой колонки) настоящим кликом в ЭТОМ прогоне не проверить: вьюпорт
        #   headless-PIE равен 700x201 юнитов (печатает дамп), окно доп-меню — 432x368, и нижняя
        #   строка физически за краем экрана. Это ограничение ПРОГОНА, а не окна: та же строка,
        #   сдвинутая `asda.ExtraMenu.MoveRow 7 206 166` внутрь экрана, рисуется и нажимается.
        (lambda: cmd("asda.UI.SimClick 456 137"), 40),
        (lambda: cmd("asda.UI.SimClick 252 342"), 40),
        # ★ ПОДСКАЗКА: курсор после симклика остаётся на строке, поэтому дамп СРАЗУ после него
        #   обязан показать «подсказка показана=1» — это событие 8 эталона (наведение).
        (lambda: cmd("asda.UI.SimClick 456 137"), 4),
        (lambda: cmd("asda.ExtraMenu.Dump"), 30),

        (lambda: mark("7б. ПОВТОРНЫЙ дамп через ~400 кадров: не артефакт ли «нет геометрии»"), 2),
        (lambda: cmd("asda.ExtraMenu.Dump"), 400),
        (lambda: cmd("asda.ExtraMenu.Dump"), 30),

        (lambda: mark("8. ПОВТОРНОЕ нажатие кнопки трея — окно обязано закрыться"), 2),
        (lambda: cmd("asda.ExtraMenu.MenuButton"), SETTLE),
        (lambda: cmd("asda.ExtraMenu.Dump"), 30),
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
            unreal.log_error("[доп-меню-живое] ошибка:\n" + traceback.format_exc())
            finish()
        finally:
            st["busy"] = False

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    log("tick-callback зарегистрирован")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
