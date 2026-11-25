# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_post_order_check.py — ЖИВАЯ проверка ПОРЯДКА СТРОК элитного почтового ящика (окно 69000).
#
# ЗАЧЕМ. Жалоба владельца 2026-08-28: «предметы добавляются странным порядком, не по новизне».
#   Разбор — `Reports/elite_mailbox_order.md`. Здесь проверяется ровно то, что делает КЛИЕНТ:
#     1. по умолчанию порядок строк = порядок записей 11005 (эталон, сортировки нет);
#     2. ключ `asda.EliteMail.SortNewestFirst 1` переставляет строки по убыванию `Order`,
#        стабильно (равные `Order` сохраняют серверный порядок) и не роняет выбор строки;
#     3. ключ `0` возвращает эталонное поведение.
#
# ДАННЫЕ ВЗЯТЫ ИЗ ЖИВОГО ЛОГА, а не придуманы: набор `Order` = 132, 150, 150, 1000, 2640,
#   10000, 10000 — ровно тот, что сервер прислал 28.08.2026 в 21:59:40
#   (`\\@@ASTORY_SERVER_HOSTNAME@@\…\v2\GameServer\Log\Log_MGamePremium\List_20260828.txt`). Коды предметов взяты
#   существующие в `DT_item_list` (10000…10006), иначе иконок не будет вовсе.
#
# ЗАПУСК: Tools\run_headless_rhi_tick.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_post_order_check.py
LEVEL = "/Game/Map/Login"

WARMUP = 240
SETTLE = 45
MAX_TICKS = 8000

# ItemIndex:Count:BuyType:Order — порядок аргументов = порядок записей в 11005 (по возрастанию Order).
SIM = ("asda.Post.Sim 1 10000:1:1:132 10001:1:1:150 10002:1:1:150 10003:1:1:1000 "
       "10004:1:1:2640 10005:1:1:10000 10006:1:1:10000")


def log(m):
    import unreal
    unreal.log("[ящик-порядок] " + str(m))


def main():
    import unreal

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

    unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
    les.editor_request_begin_play()
    log("PIE запущен")

    st = {"tick": 0, "step": -1, "next_at": WARMUP, "handle": None, "busy": False}

    def cmd(c):
        w = ues.get_game_world()
        if not w:
            raise RuntimeError("нет игрового мира — PIE не стартовал")
        unreal.SystemLibrary.execute_console_command(w, c)
        log("→ %s" % c)

    PLAN = [
        lambda: cmd("asda.Post.Open 1"),
        # ── А. ЭТАЛОН: сортировки нет, строки идут как прислал сервер ───────────────────────
        lambda: cmd("asda.EliteMail.SortNewestFirst 0"),
        lambda: cmd(SIM),
        lambda: cmd("asda.Post.Dump model"),
        # ── Б. РАСШИРЕНИЕ: строки переставлены по убыванию Order ────────────────────────────
        lambda: cmd("asda.EliteMail.SortNewestFirst 1"),
        lambda: cmd(SIM),
        lambda: cmd("asda.Post.Dump model"),
        # клик по иконке строки 3 — выбор обязан встать на 3 и при переставленном списке
        lambda: cmd("asda.Post.ClickIcon 21"),
        lambda: cmd("asda.Post.Dump model"),
        # ── В. ОТКАТ: ключ 0 возвращает эталонный порядок ───────────────────────────────────
        lambda: cmd("asda.EliteMail.SortNewestFirst 0"),
        lambda: cmd(SIM),
        lambda: cmd("asda.Post.Dump model"),
    ]

    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    finish = PH.make_pie_finish(st, les, log, "=== готово ===", handle_is_not_none=True)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 10 стр.)

    def on_tick(_dt):
        if st["busy"]:
            return
        st["busy"] = True
        try:
            st["tick"] += 1
            if st["tick"] > MAX_TICKS:
                unreal.log_error("[ящик-порядок] предел тиков")
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
            unreal.log_error("[ящик-порядок] ошибка:\n" + traceback.format_exc())
            finish()
        finally:
            st["busy"] = False

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    log("tick-callback зарегистрирован")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
