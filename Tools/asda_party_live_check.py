# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_party_live_check.py — ЖИВАЯ (PIE) проверка ОКОН ОТРЯДА `PARTYTAKE_WIN` 2250 и
#   `PARTYBOX_WIN` 2245. Разбор эталона — `Reports/party_invite_2250.md`.
#
# ★★★ ЗАЧЕМ ЖИВАЯ, А НЕ ОФЛАЙН-ДАМП (закон false-negatives-method). Оба окна собираются
#   подсистемой `UAsdaPartySubsystem` в РАНТАЙМЕ: подписи берутся из `DT_GameText`, список 2254
#   строится общей точкой `UAsdaJuiComboBox`, видимость 2255 и группы списка ставится КОДОМ
#   (в ассете обе видимы), шкала 2247 двигается тиком. Дамп ассета показывает данные, а не то,
#   что увидит игрок.
#
# ★★★ ЖМЁМ САМИ КНОПКИ (закон «диагностика обязана жать саму кнопку»):
#   `asda.Party.Click <имя узла>` находит `UButton` в ЖИВОМ виджете и зовёт его
#   `OnClicked.Broadcast()` — тот же путь, что у мыши игрока. Обработчики напрямую НЕ зовём.
#
# ★★★ ВХОДЯЩАЯ ЗАЯВКА без второго игрока: `asda.Party.FakeRequest` собирает тело 30 б РОВНО по
#   раскладке сервера и кладёт его в ТУ ЖЕ очередь, которую наполняет `UAsdaNetSubsystem`.
#   Разбирает его НАСТОЯЩИЙ приёмник — копии парсера в скрипте нет.
#
# ЧТО ПРОВЕРЯЕТСЯ (каждый шаг оставляет строку-доказательство в логе):
#   1. окно 2250 открывается по пункту меню (гейты пройдены), контролы разобраны;
#   2. список 2254 живой, оба пункта — из таблицы текста (12153/12154);
#   3. переключение правила дележа доходит до состояния;
#   4. кнопка 2252 «ОК» нажимается СВОИМ OnClicked, уходит 5090 (без сервера — «НЕ отправлен»);
#   5. гейт «моя заявка в полёте» (нотис 1056) срабатывает на повторном открытии;
#   6. приходит заявка (5091) → иконка уведомления + звук 215 + нотис 5057;
#   7. окно 2245 показывается кликом по иконке (эквивалент `asda.Party.ShowJoinBox`), текст —
#      loc 20021/20022 с подставленным именем;
#   8. шкала 2247 тикает (два дампа с разницей во времени);
#   9. кнопка 2248 «ОК» нажимается СВОИМ OnClicked → уходит 5092 с кодом 1;
#  10. приходит 5093 код 1 → состояние «в отряде», гейт 1047 на повторное приглашение того же;
#  11. кадры обоих окон.
#
# ЗАПУСК:  Tools\run_headless_party.bat
import os
import shutil

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/party_live"
SS_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
LEVEL = "/Game/Map/alpen/alpen"

WARMUP = 300
TRAVEL_SETTLE = 400
SETTLE = 60
MAX_TICKS = 40000

# Цель-заглушка: тот же userNum, которым пользуется живая проверка меню игрока.
TARGET_USERNUM = 7
TARGET_NAME = "TestTarget"
# Приглашающий во входящей заявке.
INV_USERNUM = 5
INV_CHARID = 1000
INV_NAME = "TestLeader"
REQ_ID = 42


def log(m):
    import unreal
    unreal.log("[отряд-живое] " + str(m))


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

    def dump(tag):
        log("=== дамп: %s ===" % tag)
        cmd("asda.Party.Dump")

    steps = []

    def add(fn, wait):
        steps.append((fn, wait))

    add(lambda: cmd("open /Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode"), TRAVEL_SETTLE)

    # ── 1. Окно 2250 по пункту меню ───────────────────────────────────────────────────────────
    add(lambda: cmd("asda.Party.Invite %d %s 1" % (TARGET_USERNUM, TARGET_NAME)), SETTLE)
    add(lambda: dump("2250 открыто, ветка «создание отряда»"), 5)
    add(lambda: cmd("HighResShot 1280x720"), 60)

    # ── 2-3. Список 2254: переключить правило и убедиться, что состояние поменялось ────────────
    add(lambda: cmd("asda.Party.Rule 1"), 5)
    add(lambda: dump("правило = автораспределение (12154)"), 5)
    add(lambda: cmd("asda.Party.Rule 0"), 5)
    add(lambda: dump("правило = кто успел (12153)"), 5)

    # ── 4. Кнопка 2252 своим OnClicked ────────────────────────────────────────────────────────
    add(lambda: cmd("asda.Party.Click PARTYTAKE_YES"), 20)
    add(lambda: dump("после 2252 «ОК» (уходит 5090)"), 5)

    # ── 5. Гейт «моя заявка в полёте» → нотис 1056 ────────────────────────────────────────────
    add(lambda: cmd("asda.Party.Invite %d %s 1" % (TARGET_USERNUM, TARGET_NAME)), 10)
    add(lambda: dump("повторное открытие ДОЛЖНО быть отбито нотисом 1056"), 5)

    # ── 5б. Приглашённый ОТКАЗАЛСЯ (5093 код 0) → нотис 1048 и защёлка снята ─────────────────
    #    Это честная эталонная ветка снятия флага: `Recv_GM_NEWPARTY_RESPONSEUSER_RES` в любом
    #    коде делает `sub_923EAB(mgr, 0)`. Без неё следующая входящая заявка была бы (правильно)
    #    отбита кодом 6 — что тоже проверено выше.
    add(lambda: cmd("asda.Party.FakeResult 5093 0 %d %d %d 2000 %d" % (INV_USERNUM, INV_CHARID, TARGET_USERNUM, REQ_ID)), 20)
    add(lambda: dump("5093 код 0: отказ, защёлка «моя заявка» снята"), 5)

    # ── 6. Входящая заявка 5091 ───────────────────────────────────────────────────────────────
    add(lambda: cmd("asda.Party.FakeRequest 5091 0 1 %d %d %d %s" % (REQ_ID, INV_USERNUM, INV_CHARID, INV_NAME)), 20)
    add(lambda: dump("заявка 5091 принята: иконка + звук 215 + нотис 5057"), 5)

    # ── 7. Окно 2245 (эквивалент клика по иконке уведомления) ─────────────────────────────────
    add(lambda: cmd("asda.Party.ShowJoinBox"), 30)
    add(lambda: dump("2245 показано, текст loc 20022 (правило = 1)"), 5)
    add(lambda: cmd("HighResShot 1280x720"), 60)

    # ── 8. Шкала 2247 тикает: второй дамп через ~2 с ──────────────────────────────────────────
    add(lambda: dump("шкала 2247: контрольная точка 1"), 120)
    add(lambda: dump("шкала 2247: контрольная точка 2 (время должно вырасти)"), 5)

    # ── 9. Кнопка 2248 своим OnClicked ────────────────────────────────────────────────────────
    add(lambda: cmd("asda.Party.Click PARTYBOX_YES"), 20)
    add(lambda: dump("после 2248 «ОК» (уходит 5092 код 1)"), 5)

    # ── 10. Ответ 5093 код 1 → «в отряде», и гейт 1047 на того же участника ───────────────────
    add(lambda: cmd("asda.Party.FakeResult 5093 1 %d %d %d 2000 %d" % (INV_USERNUM, INV_CHARID, TARGET_USERNUM, REQ_ID)), 20)
    add(lambda: dump("5093 код 1: отряд создан"), 5)
    add(lambda: cmd("asda.Party.Invite %d %s 1" % (TARGET_USERNUM, TARGET_NAME)), 10)
    add(lambda: dump("приглашение УЖЕ участника ДОЛЖНО быть отбито нотисом 1047"), 5)

    # ── 10б. Ветка ЛИДЕРА: сделать себя главой (5102) и открыть 2250 по НЕ-участнику ──────────
    add(lambda: cmd("asda.Party.FakeState 1 1 42 1"), 15)
    add(lambda: cmd("asda.Party.Invite 9 Another 1"), 20)
    add(lambda: dump("2250 в ветке ЛИДЕРА: список 2254 скрыт, 2255 показан (исправление D3)"), 5)
    add(lambda: cmd("asda.Party.Click PARTYTAKE_NO"), 15)
    add(lambda: dump("после 2253 «Отмена»: окно закрыто, флаг заявки снят"), 5)

    # ── 12. ПАНЕЛЬ ОТРЯДА 2200 (окно списка участников в HUD) ─────────────────────────────────
    #   Состав подаём телом 5107 РОВНО по раскладке сервера (пустые слоты = −1), параметры — 5109,
    #   состояния — 5108. Разбирают их настоящие приёмники, копии парсера в скрипте нет.
    add(lambda: cmd("asda.Party.FakeRoster 1000 2000 3000"), 25)
    # ★★★ ВОСПРОИЗВЕДЕНИЕ ЖИВОГО СЦЕНАРИЯ: здоровье приходит ОДИН раз и больше не меняется.
    #   Именно так в игре у соотрядца с полным здоровьем — и именно этого стенд раньше не делал
    #   (я подавал 100 -> 50 -> 25, доля менялась, и заливку кто-то писал).
    add(lambda: cmd("asda.Party.FakeParam 1000 42 100 100 100 100 1"), 90)
    add(lambda: cmd("asda.Party.DumpRoster"), 15)
    add(lambda: dump("5107: состав принят, панель 2200 должна показаться"), 5)
    add(lambda: cmd("asda.Party.DumpRoster"), 10)
    add(lambda: cmd("asda.Party.FakeParam 1000 42 70 100 30 60 1"), 15)
    add(lambda: cmd("asda.Party.FakeParam 2000 17 25 200 10 40 12"), 15)
    add(lambda: cmd("asda.Party.FakeParam 3000 55 300 300 80 80 23"), 30)
    add(lambda: cmd("asda.Party.DumpRoster"), 60)
    add(lambda: cmd("asda.Party.DumpRoster"), 10)
    add(lambda: cmd("HighResShot 1280x720"), 60)
    # Кнопки панели — жмём САМИ кнопки
    add(lambda: cmd("asda.Party.Click PARTY_EXITBUT"), 15)
    add(lambda: cmd("asda.Party.Click PARTY_TOOLBUT00"), 15)
    add(lambda: cmd("asda.Party.Click PARTY_MINIBUT_H"), 15)
    add(lambda: dump("после кнопок панели (Выход / Выгнать / свернуть)"), 5)

    # ── 13. ДОЛИ ПОЛОС при 100 / 50 / 25 % (проверка бага «100 % рисуется как 50 %») ──────────
    add(lambda: cmd("asda.Party.FakeParam 1000 42 100 100 100 100 1"), 20)
    add(lambda: cmd("asda.Party.FakeParam 2000 17 50 100 50 100 12"), 20)
    add(lambda: cmd("asda.Party.FakeParam 3000 55 25 100 25 100 23"), 90)
    add(lambda: cmd("asda.Party.DumpRoster"), 15)

    # ── 14. Клик по строке и меню ПКМ 8100 ───────────────────────────────────────────────────
    add(lambda: cmd("asda.Party.ClickRow 1"), 20)
    add(lambda: cmd("asda.Party.DumpRoster"), 10)
    add(lambda: cmd("asda.Party.RowMenu 1"), 25)
    add(lambda: cmd("asda.Party.Click Target_Share"), 20)
    add(lambda: cmd("asda.Party.DumpRoster"), 10)
    add(lambda: cmd("asda.Party.RowMenu 2"), 20)
    add(lambda: cmd("asda.Party.Click Follow_CloseBtn"), 15)
    add(lambda: cmd("asda.Party.DumpRoster"), 15)
    # ★ Обводка: пометка «общая цель» должна ОСТАТЬСЯ, а зелёная «моя цель» — гаснуть сама
    add(lambda: cmd("asda.Party.ClickRow 2"), 20)
    add(lambda: cmd("asda.Party.DumpRoster"), 15)
    # ── 15. СЛЕДОВАНИЕ: оба входа. Соседей в офлайн-прогоне нет, поэтому проверяем, что
    #    вход 8101 ДОХОДИТ до общей точки StartFollow и что отказ не молчаливый.
    add(lambda: cmd("asda.Party.RowMenu 1"), 20)
    add(lambda: cmd("asda.Party.Click Follow_Another"), 25)
    add(lambda: cmd("asda.PlayerMenu.Open 7"), 20)
    add(lambda: cmd("asda.PlayerMenu.Click 2062"), 25)
    add(lambda: dump("после меню 8100"), 5)

    # ── 11. 1:1-режим: пункт меню обязан замолчать при выключенном ключе D1 ───────────────────
    add(lambda: cmd("asda.Party.FixContextMenuInvite 0"), 5)
    add(lambda: cmd("asda.Party.Invite 9 Another 1"), 10)
    add(lambda: dump("ключ D1 = 0: окно 2250 открываться НЕ должно (как в оригинале)"), 5)
    add(lambda: cmd("asda.Party.FixContextMenuInvite 1"), 5)

    def collect():
        n = 0
        if os.path.isdir(SS_DIR):
            for f in sorted(os.listdir(SS_DIR)):
                if f.lower().endswith(".png"):
                    shutil.copy2(os.path.join(SS_DIR, f), os.path.join(OUT_DIR, f))
                    n += 1
        log("кадров собрано: %d → %s" % (n, OUT_DIR))

    def on_tick(dt):
        st["tick"] += 1
        if st["busy"]:
            return
        if st["tick"] < st["next_at"]:
            if st["tick"] > MAX_TICKS:
                log("ПРЕВЫШЕН ЛИМИТ ТИКОВ")
                finish()
            return
        st["step"] += 1
        if st["step"] >= len(steps):
            finish()
            return
        fn, wait = steps[st["step"]]
        st["busy"] = True
        try:
            fn()
        except Exception as e:
            log("ОШИБКА на шаге %d: %s" % (st["step"], e))
        st["busy"] = False
        st["next_at"] = st["tick"] + wait

    def finish():
        if st["handle"] is not None:
            unreal.unregister_slate_post_tick_callback(st["handle"])
            st["handle"] = None
        collect()
        log("=== ПРОВЕРКА ЗАВЕРШЕНА ===")
        les.editor_request_end_play()
        unreal.SystemLibrary.quit_editor()

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    log("тик-драйвер зарегистрирован, шагов: %d" % len(steps))


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
