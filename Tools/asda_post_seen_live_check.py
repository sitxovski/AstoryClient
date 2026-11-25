# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_post_seen_live_check.py — ЖИВАЯ (PIE) проверка НАШЕГО НОВОВВЕДЕНИЯ «ПРОСМОТРЕННЫЕ ЗАКАЗЫ»
#   в ЭЛИТНОМ ПОЧТОВОМ ЯЩИКЕ (окно 69000, иконка-уведомление типа 15 в слоте квик-бара).
#
# ⚠⚠ ПРОВЕРЯЕТСЯ НЕ ЭТАЛОННОЕ ПОВЕДЕНИЕ, А НАША ДОБАВКА. Эталон
#   (`netParseLocalSlotSync@0xEF0990`, п. 6) вешает иконку на КАЖДЫЙ непустой 11005 при закрытом
#   окне и памяти о просмотре не имеет вовсе. Требование владельца 2026-08-20: просмотренный ящик
#   больше не моргает — ни при входе в игру, ни после смены карты; иконка возвращается ТОЛЬКО на
#   заказ, которого игрок ещё не видел.
#
# ПОЧЕМУ ЖИВАЯ, А НЕ РАССУЖДЕНИЕМ. Признак «просмотрено» обязан пережить пересоздание панели
#   `QUICK_NEW_WIN` (её сносит `UAsdaHotbarSubsystem::ResetWidget` на каждой смене карты) и
#   «липкость» `TickWorldEntry`, которая иконку ПЕРЕВЫСТАВЛЯЕТ. Никакой офлайн-разбор этого не
#   покажет — нужен настоящий PIE и те же команды `asda.Post.*`, которыми пользуется владелец.
#
# СЦЕНАРИИ (каждый оставляет в логе строку-доказательство `asda.Post.SeenDump` +
#   `asda.Quick.NoticeDump` — реальное состояние виджета иконки, а не только флаг):
#    1. свежая сессия, пришли 2 заказа при закрытом окне            → иконка ЕСТЬ
#    2. игрок открыл ящик и закрыл (просмотрел)                     → иконка ГАСНЕТ
#    3. «перешёл на другую карту» ×3, список тот же                 → иконки НЕТ (главный пункт)
#    4. тот же список в ДРУГОМ ПОРЯДКЕ                              → иконки НЕТ
#    5. пришёл НОВЫЙ (третий) заказ                                 → иконка ЕСТЬ
#    6. тот же предмет, но ДРУГОЙ заказ (другой Order)              → иконка ЕСТЬ
#    7. один заказ забран (исчез из списка)                         → иконки нет, набор СЖАЛСЯ
#    8. ящик опустошён полностью                                    → иконки нет, набор ПУСТ
#    9. сервер ПЕРЕИСПОЛЬЗОВАЛ Order уже просмотренного заказа      → иконка ЕСТЬ
#   10. откат к эталону `asda.Post.SeenGate 0`                      → иконка ЕСТЬ на просмотренном
#
# ЗАПУСК: Tools\run_headless_rhi_tick.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_post_seen_live_check.py
#   (именно `_tick`: без тика редактора PIE не крутит кадры).
import glob
import os
import shutil

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/post_seen_live"
SS_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
LEVEL = "/Game/Map/Login"

WARMUP = 240        # прогрев PIE: шейдеры, стриминг, GameMode
SETTLE = 60         # тиков между шагами
MAX_TICKS = 40000

# ── Списки 11005 для эмуляции. Формат поля: ItemIndex:Count:BuyType:Order ─────────────────────
#   Коды предметов — РЕАЛЬНЫЕ строки `DT_item_list` (10000…10008), иначе иконок в строках не будет.
#   ★ Order задан ЯВНО: именно он вместе с остальными тремя полями образует ключ заказа, и без
#     явного Order сценарий «пришёл НОВЫЙ заказ» не отличить от «тот же самый».
A = "10000:2:1:501"          # заказ №501
B = "10001:1:1:502"          # заказ №502
C = "10002:1:3:503"          # заказ №503 — НОВЫЙ (сценарий 5)
D = "10000:2:1:504"          # ★ ТОТ ЖЕ предмет и Count, что у 501, но ДРУГОЙ заказ (сценарий 6)

LIST_AB = "asda.Post.Sim 1 %s %s" % (A, B)
LIST_BA = "asda.Post.Sim 1 %s %s" % (B, A)            # тот же набор, другой порядок строк
LIST_ABC = "asda.Post.Sim 1 %s %s %s" % (A, B, C)
LIST_ABCD = "asda.Post.Sim 1 %s %s %s %s" % (A, B, C, D)
LIST_BCD = "asda.Post.Sim 1 %s %s %s" % (B, C, D)     # заказ 501 забран
LIST_EMPTY = "asda.Post.Sim 1"                        # ящик пуст
LIST_A_AGAIN = "asda.Post.Sim 1 %s" % A               # сервер переиспользовал Order 501


def log(m):
    import unreal
    unreal.log("[ящик-просмотрено] " + str(m))


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

    st = {"tick": 0, "step": -1, "next_at": WARMUP, "handle": None, "busy": False, "shots": []}

    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    world = PH.make_game_world(ues)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 5 стр.)

    cmd = PH.make_console_cmd(world, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 6 стр.)

    take_shot = PH.make_take_shot(cmd, st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 3 стр.)

    harvest = PH.make_harvest(SS_DIR, OUT_DIR, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 10 стр.)

    # Единый «протокол доказательства» после каждого шага: и наш флаг, и РЕАЛЬНЫЙ виджет иконки.
    def evidence(tag):
        def _step():
            log("══════════ ДОКАЗАТЕЛЬСТВО: %s ══════════" % tag)
            cmd("asda.Post.SeenDump")
        return _step

    def notice(tag):
        def _step():
            log("── состояние иконки квик-бара: %s ──" % tag)
            cmd("asda.Quick.NoticeDump")
        return _step

    def run(c):
        def _step():
            cmd(c)
        return _step

    def shot(tag):
        def _step():
            take_shot(tag)
        return _step

    def get(tag):
        def _step():
            harvest(tag)
        return _step

    PLAN = [
        # ── подготовка: поднять панель квик-бара (её поднимает сам NoticeDump) и обнулить состояние
        run("asda.Quick.NoticeDump"),
        run("asda.Post.SeenGate 1"),
        run("asda.Post.SeenReset"),

        # ═══ СЦЕНАРИЙ 1. Свежая сессия: пришли два заказа при ЗАКРЫТОМ окне ⇒ иконка ОБЯЗАНА гореть.
        run(LIST_AB),
        evidence("1) пришли заказы 501+502 при закрытом окне — оба НОВЫЕ, иконка обязана гореть"),
        notice("1) иконка должна ВИСЕТЬ"),
        shot("s1_icon_on"), get("s1_icon_on"),

        # ═══ СЦЕНАРИЙ 2. Игрок ПОСМОТРЕЛ ящик: открыл, получил список, закрыл ⇒ иконка гаснет.
        run("asda.Post.Open 1"),
        run(LIST_AB),                       # ответ 11004 приходит уже при открытом окне
        run("asda.Post.Close"),
        evidence("2) игрок просмотрел ящик — оба заказа помечены просмотренными"),
        notice("2) иконки быть НЕ должно"),
        shot("s2_icon_off"), get("s2_icon_off"),

        # ═══ СЦЕНАРИЙ 3. ТРИ перехода между картами подряд. Каждый = вход в мир (4003) + 11005.
        #     Это главный пункт требования: «ни при входе в игру, ни при переходе на другую карту».
        run("asda.Post.SimWorldReady"), run(LIST_AB),
        notice("3.1) после 1-й смены карты — иконки быть НЕ должно"),
        run("asda.Post.SimWorldReady"), run(LIST_AB),
        notice("3.2) после 2-й смены карты — иконки быть НЕ должно"),
        run("asda.Post.SimWorldReady"), run(LIST_AB),
        evidence("3) три смены карты подряд — набор не потерян"),
        notice("3.3) после 3-й смены карты — иконки быть НЕ должно"),
        shot("s3_after_map_change"), get("s3_after_map_change"),

        # ═══ СЦЕНАРИЙ 3.5. ЗАСТРЯВШАЯ ИКОНКА. Панель `QUICK_NEW_WIN` пересоздаётся на каждой карте
        #     (`ResetWidget` стирает типы слотов), поэтому иконка может оказаться в любом состоянии.
        #     Вешаем её руками той же общей точкой, что и хотбар, и проверяем: приход просмотренного
        #     списка обязан её СНЯТЬ, а «липкость» `TickWorldEntry` — не вернуть обратно.
        run("asda.Quick.Notice 15"),
        notice("3.5а) иконка повешена вручную — сейчас она ВИСИТ"),
        run("asda.Post.SimWorldReady"), run(LIST_AB),
        evidence("3.5) просмотренный список после смены карты обязан СНЯТЬ застрявшую иконку"),
        notice("3.5б) иконки быть НЕ должно"),

        # ═══ СЦЕНАРИЙ 4. Тот же набор заказов, но строки переставлены ⇒ иконки нет (сравнение
        #     идёт по СОДЕРЖИМОМУ, а не по позиции строки).
        run(LIST_BA),
        evidence("4) тот же список в ДРУГОМ ПОРЯДКЕ — оба всё ещё просмотрены"),
        notice("4) иконки быть НЕ должно"),

        # ═══ СЦЕНАРИЙ 5. Пришёл НОВЫЙ, третий заказ ⇒ иконка обязана загореться.
        run(LIST_ABC),
        evidence("5) пришёл НОВЫЙ заказ 503 — он один непросмотренный"),
        notice("5) иконка должна ВИСЕТЬ"),
        shot("s5_new_order_icon_on"), get("s5_new_order_icon_on"),

        # игрок посмотрел ещё раз
        run("asda.Post.Open 1"), run(LIST_ABC), run("asda.Post.Close"),
        notice("5б) после просмотра — иконки быть НЕ должно"),

        # ═══ СЦЕНАРИЙ 6. ТОТ ЖЕ предмет и Count, что у просмотренного 501, но ДРУГОЙ заказ (504).
        run(LIST_ABCD),
        evidence("6) заказ 504 = тот же предмет, что у 501, но другой Order — обязан считаться НОВЫМ"),
        notice("6) иконка должна ВИСЕТЬ"),

        run("asda.Post.Open 1"), run(LIST_ABCD), run("asda.Post.Close"),
        notice("6б) после просмотра — иконки быть НЕ должно"),

        # ═══ СЦЕНАРИЙ 7. Заказ 501 забран ⇒ он выбывает из набора (память не растёт), иконки нет.
        run(LIST_BCD),
        evidence("7) заказ 501 забран — набор обязан сжаться до трёх записей"),
        notice("7) иконки быть НЕ должно"),

        # ═══ СЦЕНАРИЙ 8. Ящик опустошён полностью ⇒ набор пуст, иконки нет.
        run(LIST_EMPTY),
        evidence("8) ящик опустошён — набор обязан быть ПУСТ"),
        notice("8) иконки быть НЕ должно"),

        # ═══ СЦЕНАРИЙ 9. Сервер ПЕРЕИСПОЛЬЗОВАЛ Order 501 (та же запись целиком). Прежний заказ
        #     уже выбыл из набора ⇒ новый обязан считаться новым и зажечь иконку.
        run(LIST_A_AGAIN),
        evidence("9) переиспользованный Order 501 — обязан считаться НОВЫМ"),
        notice("9) иконка должна ВИСЕТЬ"),
        shot("s9_order_reuse_icon_on"), get("s9_order_reuse_icon_on"),

        # ═══ СЦЕНАРИЙ 10. Откат к ЭТАЛОНУ: просмотренный ящик снова зажигает иконку на каждый 11005.
        run("asda.Post.Open 1"), run(LIST_A_AGAIN), run("asda.Post.Close"),
        notice("10а) нововведение включено, ящик просмотрен — иконки НЕТ"),
        run("asda.Post.SeenGate 0"),
        run(LIST_A_AGAIN),
        evidence("10) откат к эталону — иконка обязана гореть на просмотренном ящике"),
        notice("10б) поведение ЭТАЛОНА: иконка должна ВИСЕТЬ"),
        run("asda.Post.SeenGate 1"),
    ]

    finish = PH.make_pie_finish(st, les, log, "=== готово, снимков запрошено %d ===", handle_is_not_none=True, count_shots=True)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 10 стр.)

    def on_tick(_dt):
        if st["busy"]:
            return
        st["busy"] = True
        try:
            st["tick"] += 1
            if st["tick"] > MAX_TICKS:
                unreal.log_error("[ящик-просмотрено] предел тиков")
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
            unreal.log_error("[ящик-просмотрено] ошибка:\n" + traceback.format_exc())
            finish()
        finally:
            st["busy"] = False

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    log("tick-callback зарегистрирован, шагов в плане %d" % len(PLAN))


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
