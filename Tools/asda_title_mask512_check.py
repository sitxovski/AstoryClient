# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_title_mask512_check.py — ЖИВАЯ (PIE) проверка РАСШИРЕНИЯ МАСКИ СЛАВЫ ДО 512 БИТ.
#   Разбор системы званий — `Reports/title_system.md`; расширение — `Reports/title_mask_512.md`.
#
# ★★★ ЗАЧЕМ ЖИВАЯ, А НЕ ОФЛАЙН. Проверяется не «переменная в подсистеме», а путь игрока целиком:
#   тело пакета собирается ПО-НАСТОЯЩЕМУ и идёт через тот же приёмник, что и сетевой (штатный
#   `HandleMaskOwned`/`HandleMaskFound`), а результат читается С ВИДЖЕТОВ (`asda.Title.List`
#   печатает текст, снятый с узлов ряда), а не из внутренних полей.
#
# ЧТО ДОКАЗЫВАЕТСЯ (каждый пункт оставляет строку в логе):
#   1. таблица прочитана, действующая маска 512 бит, недостижимых званий НЕТ (было 24);
#   2. НОВЫЙ формат 6066 (тело 74 б) — 280 бит взведено, звание id 279 «Zombie» ПОЛУЧЕНО;
#   3. СТАРЫЙ формат 6066 (тело 42 б, необновлённый сервер) — ровно 256 бит, id 279 НЕ получено
#      (совместимость «вниз»: новый клиент понимает старый пакет);
#   4. то же для 6086 «найдено» (тела 70 б и 38 б);
#   5. откат `asda.Title.Mask512 0` — хвост расширенного пакета игнорируется, снова 256 бит;
#   6. «Всего собрано %» (дефект D7): знаменатель = максимальный id, поэтому при полной
#      коллекции процент выходит больше 100; ключ `asda.Title.TotalRateByCount 1` это чинит.
#
# ЗАПУСК:  Tools\run_headless_title_mask512.bat
import os
import shutil

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/title_mask512_live"
SS_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
LEVEL = "/Game/Map/alpen/alpen"

WARMUP = 300
TRAVEL_SETTLE = 400
SETTLE = 60
MAX_TICKS = 40000


def log(m):
    import unreal
    unreal.log("[маска512] " + str(m))


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
    try:
        ps = unreal.get_default_object(unreal.LevelEditorPlaySettings)
        ps.set_editor_property("new_window_width", 1600)
        ps.set_editor_property("new_window_height", 1000)
        ps.set_editor_property("center_new_window", True)
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
        # ★ Без `?game=/Script/AsdaReborn.AsdaWorldGameMode` подсистемы мира не поднимаются.
        (lambda: cmd("open /Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode"), TRAVEL_SETTLE),

        (lambda: mark("1. ТАБЛИЦА + ШИРИНА МАСКИ (ждём «маска славы 512 бит», недостижимых нет)"), 2),
        (lambda: cmd("asda.Title.State"), 20),

        (lambda: mark("2. НОВЫЙ ФОРМАТ 6066 — тело 74 б, биты 0…279 (ждём «получено 280 бит»)"), 2),
        (lambda: cmd("asda.Title.Sim owned 280 512"), SETTLE),
        (lambda: cmd("asda.Title.State"), 20),
        (lambda: cmd("asda.Title.Open"), SETTLE),
        (lambda: cmd("asda.Title.Click tab 0"), SETTLE),
        # Колонка 18729 «Нет.» = SortNum; у id 256…279 он максимальный (325…348),
        # поэтому убывающая сортировка выводит их в 12 видимых строк.
        (lambda: cmd("asda.Title.Click sort 18729"), SETTLE),
        (lambda: cmd("asda.Title.List"), 40),
        (lambda: cmd("shot showui"), 60),

        (lambda: mark("3. СТАРЫЙ ФОРМАТ 6066 — тело 42 б (необновлённый сервер): ждём ровно 256 бит"), 2),
        (lambda: cmd("asda.Title.Sim owned 280 256"), SETTLE),
        (lambda: cmd("asda.Title.State"), 20),
        (lambda: cmd("asda.Title.List"), 40),
        (lambda: cmd("shot showui"), 60),

        (lambda: mark("4. МАСКА «НАЙДЕНО» 6086: новый формат 70 б, затем старый 38 б"), 2),
        (lambda: cmd("asda.Title.Sim found 280 512"), SETTLE),
        (lambda: cmd("asda.Title.State"), 20),
        (lambda: cmd("asda.Title.List"), 40),
        (lambda: cmd("asda.Title.Sim found 280 256"), SETTLE),
        (lambda: cmd("asda.Title.State"), 20),

        (lambda: mark("5. 6065 «звание получено» с id >= 256 — одиночная выдача звания 270"), 2),
        (lambda: cmd("asda.Title.Sim grant 270"), SETTLE),
        (lambda: cmd("asda.Title.State"), 20),
        (lambda: cmd("asda.Title.List"), 40),

        (lambda: mark("6. ДЕФЕКТ D7: знаменатель «Всего собрано %» = максимальный id"), 2),
        (lambda: cmd("asda.Title.Sim owned 280 512"), SETTLE),
        (lambda: cmd("asda.Title.State"), 20),
        (lambda: cmd("asda.Title.TotalRateByCount 1"), 10),
        (lambda: cmd("asda.Title.Sim owned 280 512"), SETTLE),
        (lambda: cmd("asda.Title.State"), 20),
        (lambda: cmd("asda.Title.TotalRateByCount 0"), 10),

        (lambda: mark("7. ОТКАТ `asda.Title.Mask512 0` — хвост 74-байтного тела игнорируется"), 2),
        (lambda: cmd("asda.Title.Mask512 0"), 20),
        (lambda: cmd("asda.Title.Sim owned 280 512"), SETTLE),
        (lambda: cmd("asda.Title.State"), 20),
        (lambda: cmd("asda.Title.List"), 40),
        (lambda: cmd("shot showui"), 60),
        (lambda: cmd("asda.Title.Mask512 1"), 20),
        (lambda: cmd("asda.Title.Sim owned 280 512"), SETTLE),
        (lambda: cmd("asda.Title.State"), 20),
    ]

    finish = PH.make_pie_finish_endplay_first(st, les, log, "ЗАВЕРШЕНО", frames="walk", ss_dir=SS_DIR, out_dir=OUT_DIR)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 18 стр.)

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
            unreal.log_error("[маска512] ошибка:\n" + traceback.format_exc())
            finish()
        finally:
            st["busy"] = False

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    log("tick-callback зарегистрирован")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
