# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_title_live_check.py — ЖИВАЯ (PIE) проверка МЕНЮ ЗВАНИЙ (окно `Title_Win` 18700).
#   Разбор эталона, раскладки пакетов и таблица нотисов — `Reports/title_system.md`.
#
# ★★★ ЗАЧЕМ ЖИВАЯ, А НЕ ОФЛАЙН-ДАМП (закон false-negatives-method). Всё содержимое окна строится
#   в РАНТАЙМЕ: 12 строк наполняются из `DT_title_list` по маскам сервера, подписи колонок и
#   кнопок перетирают текст `.JUI` нотисами, полосы прокрутки в ассете НЕТ вовсе (её собирает
#   `FAsdaScrollBarNomal`), а клик по строке ловится по ЖИВОЙ геометрии контейнера ряда.
#
# ★★★ ЖМЁМ САМИ ЭЛЕМЕНТЫ (закон «диагностика обязана жать саму кнопку и читать состояние виджета»):
#   `asda.Title.Click …` дёргает ТЕ ЖЕ ветки, что и мышь, а `asda.Title.List` печатает текст,
#   ПРОЧИТАННЫЙ С ВИДЖЕТОВ, а не из внутренних переменных подсистемы.
#
# ЧТО ПРОВЕРЯЕТСЯ (каждый пункт оставляет строку-доказательство в логе):
#   1. таблицы прочитаны: 280 званий, 109 имён статов                — `asda.Title.State`
#   2. окно 18700 открывается публичным методом, контролы разобраны  — `asda.Title.Dump`
#   3. вкладка «Секретные» (2) показывает звания БЕЗ маски сервера   — `asda.Title.List`
#   4. маска «получено» (пакет 6066) наполняет вкладку «Обычный»     — `asda.Title.Sim owned N`
#   5. сортировки по всем семи колонкам меняют порядок строк         — `asda.Title.Click sort …`
#   6. выбор ряда наполняет инфо-панель, блоки статов из 2255 видны  — `asda.Title.Sim stats …`
#   7. «Применить» кладёт звание в слот приставки/окончания          — `asda.Title.Click apply`
#   8. «ОК» шлёт 6067 и ставит кулдаун 60 с                          — лог `→6067`
#   9. «Очистить» на надетом/несобственном даёт правильный отказ     — лог нотиса
#  10. 6068 «свои слоты» правит подписи 18708/18710                  — `asda.Title.Sim slots`
#  11. значок HUD 50000 зажигается на 6065 и 6085                    — `asda.Title.State`
#  12. повторный вызов публичного метода ЗАКРЫВАЕТ окно (тоггл)
#  13. кадры окна
#
# ЗАПУСК:  Tools\run_headless_title.bat
import os
import shutil

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/title_live"
SS_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
LEVEL = "/Game/Map/alpen/alpen"

WARMUP = 300
TRAVEL_SETTLE = 400
SETTLE = 60
MAX_TICKS = 40000


def log(m):
    import unreal
    unreal.log("[звания-живое] " + str(m))


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
    # ★ PIE-окно по умолчанию открывается во встроенном вьюпорте (в offscreen-лайоуте это ~700x200),
    #   и высокие окна уезжают за край — живая геометрия кнопок нижней части окна 900
    #   получается нулевой. Просим отдельное окно 1600x1000 (оно всё равно offscreen).
    try:
        ps = unreal.get_default_object(unreal.LevelEditorPlaySettings)
        ps.set_editor_property("new_window_width", 1600)
        ps.set_editor_property("new_window_height", 1000)
        ps.set_editor_property("center_new_window", True)
        log("PIE-окно: 1600x1000")
    except Exception as e:
        log("не удалось задать размер PIE-окна: %s" % e)
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
        # ★ Без `?game=/Script/AsdaReborn.AsdaWorldGameMode` подсистемы мира не поднимаются и
        #   прогон даёт ложное «ничего нет».
        (lambda: cmd("open /Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode"), TRAVEL_SETTLE),

        (lambda: mark("1. ТАБЛИЦЫ ПРОЧИТАНЫ"), 2),
        (lambda: cmd("asda.Title.State"), 20),

        (lambda: mark("2. ПУБЛИЧНЫЙ МЕТОД ОТКРЫТИЯ (тот же, что у пункта «Меню званий»)"), 2),
        (lambda: cmd("asda.Title.Open"), SETTLE),
        (lambda: cmd("asda.Title.Dump"), 30),
        (lambda: cmd("shot showui"), 60),

        (lambda: mark("3. ВКЛАДКА «Секретные» (2) — 9 званий видны БЕЗ маски сервера"), 2),
        (lambda: cmd("asda.Title.Click tab 2"), SETTLE),
        (lambda: cmd("asda.Title.List"), 30),
        (lambda: cmd("shot showui"), 60),

        (lambda: mark("4. ПАКЕТ 6066: маска «получено» на первые 40 званий"), 2),
        (lambda: cmd("asda.Title.Sim owned 40"), SETTLE),
        (lambda: cmd("asda.Title.Click tab 0"), SETTLE),
        (lambda: cmd("asda.Title.List"), 30),
        (lambda: cmd("shot showui"), 60),

        (lambda: mark("4а. КАДРЫ ВКЛАДОК: цвет подписи активной обязан отличаться от остальных"), 2),
        (lambda: cmd("asda.Title.Click tab 1"), SETTLE),
        (lambda: cmd("asda.Title.List"), 30),
        (lambda: cmd("asda.Title.Click tab 0"), SETTLE),
        (lambda: cmd("asda.Title.List"), 30),
        (lambda: cmd("shot showui"), 60),

        (lambda: mark("5. СОРТИРОВКА по семи колонкам — порядок обязан меняться"), 2),
        (lambda: cmd("asda.Title.Click sort 18729"), 20),
        (lambda: cmd("asda.Title.Click sort 18731"), 20),
        (lambda: cmd("asda.Title.Click sort 18733"), 20),
        (lambda: cmd("asda.Title.Click sort 18734"), 20),
        (lambda: cmd("asda.Title.Click sort 188350"), 20),

        (lambda: mark("6. ВЫБОР РЯДА + БЛОКИ СТАТОВ (пакет 2255)"), 2),
        (lambda: cmd("asda.Title.Sim stats 0 7"), 20),
        (lambda: cmd("asda.Title.Click row 0"), SETTLE),
        (lambda: cmd("asda.Title.List"), 30),
        (lambda: cmd("asda.Title.Dump"), 20),
        (lambda: cmd("shot showui"), 60),

        (lambda: mark("7. «ПРИМЕНИТЬ» — звание уходит в слот приставки/окончания"), 2),
        (lambda: cmd("asda.Title.Click apply"), SETTLE),
        (lambda: cmd("asda.Title.List"), 30),

        (lambda: mark("9. «ОК» — ждём отправку 6067 и кулдаун 60 000 мс"), 2),
        (lambda: cmd("asda.Title.Click ok"), SETTLE),
        (lambda: cmd("asda.Title.State"), 30),

        (lambda: mark("10. ПАКЕТ 6068: свои слоты и ранг"), 2),
        (lambda: cmd("asda.Title.Sim slots 1 2 15"), 30),
        (lambda: cmd("asda.Title.Open"), SETTLE),
        (lambda: cmd("asda.Title.List"), 30),
        (lambda: cmd("shot showui"), 60),

        (lambda: mark("11. ПАКЕТЫ 6065/6085/6070 — значок HUD, «новое», «потеряно», удаление"), 2),
        (lambda: cmd("asda.Title.Sim grant 5"), 30),
        (lambda: cmd("asda.Title.Sim secret 60"), 30),
        (lambda: cmd("asda.Title.Sim del 5"), 30),

        (lambda: mark("12. ТОГГЛ: повторный вызов публичного метода ЗАКРЫВАЕТ окно"), 2),
        (lambda: cmd("asda.Title.Open"), SETTLE),
        (lambda: cmd("asda.Title.State"), 30),

        (lambda: mark("12а. КНОПКА «МОЙ БЛОГ» (988) В ОКНЕ 900 — видна и жмётся НАСТОЯЩИМ кликом"), 2),
        (lambda: cmd("asda.CharInfo.Open"), SETTLE),
        (lambda: cmd("asda.CharInfo.BlogClick"), SETTLE),
        (lambda: cmd("shot showui"), 60),
        (lambda: cmd("asda.CharInfo.Close"), 30),

        # ★ ПОСЛЕДНИМ: модальный отказ поднимает `ONEBUT_MSGBOX` и забирает ввод (InputMode UIOnly),
        #   поэтому все шаги, которым нужны кадры, идут ДО него.
        (lambda: mark("13. «ОЧИСТИТЬ» на НАДЕТОМ звании — ждём модальный отказ (нотис 2430)"), 2),
        (lambda: cmd("asda.Title.Open"), SETTLE),
        (lambda: cmd("asda.Title.Click row 0"), 30),
        (lambda: cmd("asda.Title.Click delete"), 60),
        (lambda: cmd("shot showui"), 60),
    ]

    finish = PH.make_pie_finish_endplay_first(st, les, log, "ЗАВЕРШЕНО", frames="walk", ss_dir=SS_DIR, out_dir=OUT_DIR)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 20 стр.)

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
            unreal.log_error("[звания-живое] ошибка:\n" + traceback.format_exc())
            finish()
        finally:
            st["busy"] = False

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    log("tick-callback зарегистрирован")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
