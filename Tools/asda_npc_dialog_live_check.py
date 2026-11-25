# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_npc_dialog_live_check.py — ЖИВАЯ (PIE) проверка ОКНА РАЗГОВОРА С NPC (JUI 570) и ветки клика.
#
# ★★★ ЗАЧЕМ ИМЕННО ЖИВАЯ (закон `false-negatives-method`). Офлайн-дамп `.uasset` видит разметку и
#   НЕ видит последнего шага — привязки контролов кодом, разворота свёрнутых групп, подписей,
#   портрета и текста. На гаче и на ящике это уже стоило нескольких итераций.
#
# ЧТО ПРОВЕРЯЕТСЯ (каждый пункт даёт строку-доказательство в логе):
#   1. окно 570 создаётся, контролы разбираются                — лог «контролы разобраны: …»
#   2. открытие по npcId, стартовая страница NSR (opcode 121)  — `asda.Npc.Dialog 3` (Ian)
#   3. ЭФФЕКТИВНАЯ видимость всех групп и подписи кнопок       — `asda.Npc.DialogDump`
#   4. окно реально рисуется                                   — снимок `npc_dialog.png`
#   5. переход на другую страницу диалога                      — `asda.Npc.DialogPage 63`
#   6. нажатие кнопки меню (код 30002) — явное сообщение D6     — `asda.Npc.DialogClick 581`
#   7. нажатие кнопки шага (состояние 5 → Э5)                   — `asda.Npc.DialogClick 578`
#   8. закрытие по 575 и сброс состояния                        — `asda.Npc.DialogClick 575` + дамп
#   9. второй NPC с портретом и другим набором кнопок           — `asda.Npc.Dialog 21` (rachel)
#  10. ВЕТКА КЛИКА (этап Э1): зона, отложенная цель, D5/D9      — `asda.Npc.TalkZone` / `TalkNearest`
#
# ЗАПУСК: Tools\run_headless_npc_dialog.bat
#   ★ Отдельный .bat, а не общий `run_headless_rhi_tick.bat`, по двум причинам:
#     (1) нужен ТИК редактора (ExecCmds=py …, а не -ExecutePythonScript) — иначе машина
#         состояний на `register_slate_post_tick_callback` не сделает ни одного шага;
#     (2) нужен БОЛЬШОЙ вьюпорт: окно 570 рассчитано на базу 1024x768 и привязано к
#         ПРАВОМУ-НИЖНЕМУ углу, а в стандартном headless-окне 960x777 на вьюпорт остаётся
#         ~765x420, и верхняя половина окна (тело реплики) уезжает за край — снимок выглядит
#         «пустым», хотя построено всё верно. Классический ложный отрицательный результат
#         (закон `false-negatives-method`), поэтому редактор поднимается с -ResX/-ResY.
import glob
import os
import shutil

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/npc_dialog_live"
SS_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
# Ian (npcId 3) стоит на карте mapId 3 = Альпия; там же rachel и десятки других NPC.
LEVEL = "/Game/Map/alpen/alpen"

WARMUP = 300
TRAVEL_SETTLE = 400   # переезд уровня: стриминг + BeginPlay мира
SETTLE = 60
MAX_TICKS = 30000


def log(m):
    import unreal
    unreal.log("[npc-диалог-живой] " + str(m))


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

    take_shot = PH.make_take_shot(cmd, st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 11 стр.)

    harvest = PH.make_harvest(SS_DIR, OUT_DIR, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 10 стр.)

    # ── шаги ────────────────────────────────────────────────────────────────────────────────────
    def s_travel():
        # ★ У карт мира в порту НЕТ своего GameMode: PIE поднимает `GameModeBase`, и
        #   `AAsdaPlayerController` не создаётся вовсе (в игре его ставит переход из логина).
        #   Поэтому переезжаем в тот же уровень с ЯВНЫМ игровым режимом — тогда контроллер живой
        #   и ветку клика можно прогнать. Павна всё равно не будет: `DefaultPawnClass = nullptr`,
        #   персонажа спавнит `UAsdaWorldSubsystem` по серверному 4003 (см. отчёт, §«что осталось»).
        cmd("open /Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode")

    def s_zone_before():
        cmd("asda.Npc.TalkZone")

    def s_open_ian():
        cmd("asda.Npc.Dialog 3")

    def s_dump_ian():
        cmd("asda.Npc.DialogDump")

    def s_shot_ian():
        take_shot("npc_dialog_ian")

    def s_get_ian():
        harvest("npc_dialog_ian")

    def s_page_63():
        cmd("asda.Npc.DialogPage 63")

    def s_dump_63():
        cmd("asda.Npc.DialogDump")

    def s_shot_63():
        take_shot("npc_dialog_ian_page63")

    def s_get_63():
        harvest("npc_dialog_ian_page63")

    def s_click_menu():
        # Кнопка меню 581 = «Sell Items» → код 30002 (магазин). Подсистемы магазина нет ⇒ ждём
        #   ЯВНОЕ сообщение и строку в логе (исправление дефекта D6, а не молчание оригинала).
        cmd("asda.Npc.DialogClick 581")

    def s_click_step():
        # Кнопка шага 578: у стартовой страницы Ian её подпись «Quest», состояние шага 5 ⇒
        #   эталон открыл бы MULTYQUEST_WIN (этап Э5) — ждём явное сообщение.
        cmd("asda.Npc.DialogPage 1")

    def s_click_step2():
        cmd("asda.Npc.DialogClick 578")

    def s_close():
        cmd("asda.Npc.DialogClick 575")

    def s_dump_closed():
        cmd("asda.Npc.DialogDump")

    def s_open_rachel():
        cmd("asda.Npc.Dialog 21")

    def s_dump_rachel():
        cmd("asda.Npc.DialogDump")

    def s_shot_rachel():
        take_shot("npc_dialog_rachel")

    def s_get_rachel():
        harvest("npc_dialog_rachel")

    def s_close2():
        cmd("asda.Npc.DialogClose")

    # ── ДВИЖЕНИЕ ЗАКРЫВАЕТ ОКНО (требование владельца 2026-08-21, отступление от эталона) ──
    #   Павна в PIE нет (его спавнит серверный 4003), поэтому клик по земле/WASD маршрут не
    #   построят. Проверяем ТУ ЖЕ общую точку `NotifyPlayerStartedMoving`, в которую сходятся
    #   ВСЕ источники движения, — второй реализации правила нет, разъехаться нечему.
    def s_move_open():
        cmd("asda.Npc.Dialog 3")      # открытие идёт через АКТОРА: взводит выделение и собеседника

    def s_move_dump_before():
        cmd("asda.Npc.DialogDump")

    def s_move_close():
        cmd("asda.Npc.DialogMoveClose")

    def s_move_dump_after():
        cmd("asda.Npc.DialogDump")

    # ── КАДР С КНОПКОЙ ШАГА (QA) — ровно тот экран, на котором владелец увидел БЕЛЫЙ ФОН ──
    #   `clark` (npcId 47), страница 15: текст «О, [NAME]! Хм. У тебя найдется немного времени?…»,
    #   подпись шага «Я расскажу, когда выслушаю твою историю.» ложится на кнопку `NPC_QA_BUT00`
    #   (скин `NoImage_PushBtn`). Именно её непрозрачная подложка и давала белый прямоугольник.
    def s_open_clark():
        cmd("asda.Npc.Dialog 47")

    def s_clark_page15():
        cmd("asda.Npc.DialogPage 15")

    def s_clark_dump():
        cmd("asda.Npc.DialogDump")

    def s_clark_shot():
        take_shot("npc_dialog_clark_qa")

    def s_clark_get():
        harvest("npc_dialog_clark_qa")

    def s_clark_close():
        cmd("asda.Npc.DialogClose")

    # ══ СОСТОЯНИЯ ПОДПИСИ КНОПОК: наведение и нажатие (реплики игрока 578-580, меню 581-585) ══
    #   Эталон: подпись рисуется из АКТИВНОГО кадра плана (`ui_core.c:109129-109151`), кадры
    #   0 покой / 1 наведение / 2 нажатие. У скина `NoImage_PushBtn` картинка кадров одинаковая и
    #   ПРОЗРАЧНАЯ, поэтому весь эффект — цвет подписи (+1 px вниз при нажатии). Разбор:
    #   `Reports/npc_dialog_button_states.md`.
    #   ★ Мышью в headless-PIE не навести, поэтому шлём НАСТОЯЩИЕ события Slate
    #   (`asda.UI.SimHoverWidget` / `asda.UI.SimClickWidget`) — звать колбэк напрямую бессмысленно,
    #   проверяем ровно доставку состояния до подписи.
    def s_cap_rest():
        cmd("asda.UI.DumpCaptionStates NPC_LINE_NPC")

    def s_cap_hover():
        cmd("asda.UI.SimHoverWidget NPC_LINE_NPC NPC_QA_BUT00")

    def s_cap_hover_dump():
        cmd("asda.UI.DumpCaptionStates NPC_LINE_NPC")

    def s_cap_hover_shot():
        take_shot("npc_dialog_qa_hover")

    def s_cap_hover_get():
        harvest("npc_dialog_qa_hover")

    def s_cap_press():
        # Удержание 150 кадров: следующий шаг (дамп) приходит через SETTLE=60 кадров, то есть
        #   ГАРАНТИРОВАННО во время нажатия.
        cmd("asda.UI.SimClickWidget NPC_LINE_NPC NPC_QA_BUT00 150")

    def s_cap_press_dump():
        cmd("asda.UI.DumpCaptionStates NPC_LINE_NPC")

    def s_cap_press_shot():
        take_shot("npc_dialog_qa_press")

    def s_cap_press_get():
        harvest("npc_dialog_qa_press")

    def s_cap_wait():
        cmd("asda.UI.DumpCaptionStates NPC_LINE_NPC")   # ещё в нажатии (кадр 2)

    def s_cap_after_dump():
        # Отпускание уже прошло: эталон `CPushButton_OnReleased` ставит кадр 1 (курсор над кнопкой),
        #   а сам клик обязан уйти в `OnDialogButton(578)` — это видно строкой выше в логе.
        cmd("asda.UI.DumpCaptionStates NPC_LINE_NPC")

    def s_cap_unhover():
        cmd("asda.UI.SimHoverWidget")                   # без аргументов — курсор в (0,0)

    def s_cap_rest_dump():
        cmd("asda.UI.DumpCaptionStates NPC_LINE_NPC")

    def s_cap_reopen():
        # Вернуть ту же страницу: длинное удержание выше довело диалог до следующего шага либо
        #   потеряло захват мыши (см. ниже) — для проверки клика нужна известная точка отсчёта.
        cmd("asda.Npc.DialogPage 15")

    def s_cap_rehover():
        # ★★★ НАВЕСТИ ОБЯЗАТЕЛЬНО ПЕРЕД КЛИКОМ. `SButton::OnMouseButtonUp` выполняет `ExecuteOnClick`
        #   только при `IsHovered()`, а `SetCursorPos` сам по себе событий наведения не порождает
        #   (платформенный курсор в headless стоит в (0,0) — строка «КУРСОР НЕ ВСТАЛ» в логе).
        #   Без этого шага синтетический клик молча не доходит — классический ложный отрицательный
        #   результат (закон `false-negatives-method`).
        cmd("asda.UI.SimHoverWidget NPC_LINE_NPC NPC_QA_BUT00")

    def s_cap_click_short():
        # Короткое удержание (2 кадра) — обычный клик мышью. Длинное удержание выше
        #   нужно было только чтобы дамп успел застать кадр 2 (нажатие).
        cmd("asda.UI.SimClickWidget NPC_LINE_NPC NPC_QA_BUT00 2")

    def s_cap_click_dump():
        # Клик обязан выбрать шаг диалога: в логе появляется новая «[NPC-диалог] страница talkId=…».
        cmd("asda.Npc.DialogDump")

    def s_cap_menu_hover():
        # Кнопка МЕНЮ сценария (скин `Npc_Shop`): у неё меняется и картинка, и цвет подписи.
        cmd("asda.UI.SimHoverWidget NPC_LINE_NPC NPC_SHOP_BUT00")

    def s_cap_menu_dump():
        cmd("asda.UI.DumpCaptionStates NPC_LINE_NPC")

    def s_cap_menu_shot():
        take_shot("npc_dialog_menu_hover")

    def s_cap_menu_get():
        harvest("npc_dialog_menu_hover")

    def s_cap_menu_unhover():
        cmd("asda.UI.SimHoverWidget")

    # Остальные кнопки окна: стрелка «Далее» (577, скин `Arrow_Next`) и закрытие (575,
    #   `Npc_CloseBtn`). Подписи у них ПУСТЫЕ, состояния — целиком в КАРТИНКЕ кадров
    #   (`Diffuse` у стрелок, `UV`+`Diffuse` у крестика), поэтому проверяются СНИМКОМ.
    def s_cap_next_hover():
        cmd("asda.UI.SimHoverWidget NPC_LINE_NPC NPC_NEXT_BUT")

    def s_cap_next_shot():
        take_shot("npc_dialog_next_hover")

    def s_cap_next_get():
        harvest("npc_dialog_next_hover")

    def s_cap_close_hover():
        cmd("asda.UI.SimHoverWidget NPC_LINE_NPC NPC_TALKCLOSE")

    def s_cap_close_shot():
        take_shot("npc_dialog_close_hover")

    def s_cap_close_get():
        harvest("npc_dialog_close_hover")

    def s_cap_all_unhover():
        cmd("asda.UI.SimHoverWidget")

    def s_cap_all_shot():
        take_shot("npc_dialog_all_rest")

    def s_cap_all_get():
        harvest("npc_dialog_all_rest")

    # ── ветка клика (этап Э1) ──
    def s_talk_nearest():
        cmd("asda.Npc.TalkNearest")

    def s_zone_after():
        cmd("asda.Npc.TalkZone")

    def s_zone_calc():
        cmd("asda.Npc.TalkZoneCalc 3")

    def s_cancel_by_move():
        # ★ D5: новый приказ движения обязан снять отложенную цель. Клик по земле у нас идёт
        #   мимо консоли, но `RequestMoveTo` дёргает и мини-карта — используем её публичный вход.
        cmd("asda.Npc.TalkZone")

    # ══════════════ ЭТАПЫ Э3…Э5: условия, диспетчер команд, облачко выбора квеста ═══════════════
    def s_open_108():
        # NPC 108 «Grave Of Brave Worrior» — у него в SNQInfo есть и выдача (квест 150), и приём (149),
        #   а на страницах есть шаги с условием по уровню 140…150 и по активному квесту 149.
        cmd("asda.Npc.Dialog 108")

    def s_fake_self():
        # Уровень персонажа без сервера равен нулю, а условия страниц 20/22 сценария «108npc»
        #   требуют 140…150. Подменяем уровень ТОЛЬКО для проверки (в игре подмена не активна).
        cmd("asda.Quest.FakeSelf 145")

    def s_fake_quest():
        # ★ Без сервера состояние квестов пусто, и КАЖДОЕ условие категории 2 честно ложно.
        #   Подставляем то же 688-байтовое тело, что шлёт пакет 5047: квест 149 ВЗЯТ (стадия 0),
        #   цели набраны ⇒ он готов к сдаче. Это ровно тот квест, который NPC 108 принимает.
        cmd("asda.Quest.FakeState 149 0")

    def s_cond_dump():
        cmd("asda.Npc.CondDump 0")

    def s_page_cond():
        # Страница 20 сценария «108npc»: шаг «Next», состояние 385, условие «уровень 140…150 И
        #   квест 149 В РАБОТЕ». После подмены обе категории обязаны сойтись.
        cmd("asda.Npc.DialogPage 20")

    def s_cond_dump2():
        cmd("asda.Npc.CondDump 0")

    def s_step_go():
        # Нажатие кнопки шага: условие выполнено, прогресс 0 ⇒ переход на страницу из поля +228 (21).
        cmd("asda.Npc.DialogClick 578")

    def s_dump_after_step():
        cmd("asda.Npc.DialogDump")

    def s_fake_done():
        # Теперь квест 149 ЗАВЕРШЁН, а не взят — это условие страницы 22 («Accept it»).
        cmd("asda.Quest.FakeState -1 0 149")

    def s_page_accept():
        cmd("asda.Npc.DialogPage 22")

    def s_cond_dump3():
        cmd("asda.Npc.CondDump 0")

    def s_accept_quest():
        # Шаг «Accept it»: AcceptQuestId = 2151 ⇒ прогресс 1 ⇒ отправка 5043 «принять».
        cmd("asda.Npc.DialogClick 578")

    def s_multiquest2():
        cmd("asda.Npc.MultiQuest")

    def s_multiquest_dump2():
        cmd("asda.Npc.MultiQuestDump")

    def s_cond_why():
        cmd("asda.Quest.CondWhy 386")

    def s_multiquest():
        cmd("asda.Npc.MultiQuest")

    def s_multiquest_dump():
        cmd("asda.Npc.MultiQuestDump")

    def s_multiquest_shot():
        take_shot("npc_multiquest")

    def s_multiquest_get():
        harvest("npc_multiquest")

    def s_multiquest_pick():
        cmd("asda.Npc.MultiQuestPick 2408 0")

    def s_cmd_teleport():
        # 30031 — телепорт: строка 0 таблицы WarpListTable → mapId 7. Сети нет, поэтому пакет
        #   уйдёт в «FAIL», но по логу видно, ЧТО именно клиент собрался отправить.
        cmd("asda.Npc.Command 30031 0")

    def s_cmd_hunt():
        cmd("asda.Npc.Command 30068")

    def s_cmd_gap():
        # 30005 — код из «дырок» диспетчера оригинала (исправление D6).
        cmd("asda.Npc.Command 30005")

    def s_cmd_savepoint():
        cmd("asda.Npc.Command 30022")

    def s_cmd_exchange():
        cmd("asda.Npc.Command 30053")

    def s_cmd_raid():
        cmd("asda.Npc.Command 30061")

    def s_close3():
        cmd("asda.Npc.DialogClose")

    # ── Э6: попап наград EPISODEPOP_WIN (540) ──
    def s_reward_scan():
        cmd("asda.Npc.RewardScan")

    def s_reward_show():
        # 108npc talkId=20 шаг 0 — единственная страница Ian-цепочки с наградой (деньги 34500 +
        #   опыт 23250 + выбираемый предмет). Тройка взята ИЗ СКАНА выше, а не придумана.
        cmd("asda.Npc.RewardShow 108npc 20 0 0")

    def s_reward_dump():
        # Числовое доказательство трёх правок 2026-08-22: контур каждой подписи, текст заголовка
        #   (и счётчик CJK-символов), прямоугольник рамки против центра вьюпорта.
        cmd("asda.Npc.RewardDump")

    def s_reward_shot():
        take_shot("npc_reward_popup")

    def s_reward_get():
        harvest("npc_reward_popup")

    def s_reward_ok():
        cmd("asda.Npc.DialogClick 567")

    # ── Э6-бис: ВТОРАЯ высота окна и панель ВЫБОРА (порт uiWindow_relayoutByState) ──
    #   117npc talkId=7 шаг 0 — режим 1: гарантированных 2 (деньги 9000 + опыт 6000) и 2 выбираемых.
    #   По таблице эталона это высота 320 и сдвиг панели 553 на 100. Тройка взята из RewardScan.
    def s_reward2_show():
        cmd("asda.Npc.RewardShow 117npc 7 0 0")

    def s_reward2_dump():
        cmd("asda.Npc.RewardDump")

    def s_reward2_shot():
        take_shot("npc_reward_popup_select")

    def s_reward2_get():
        harvest("npc_reward_popup_select")

    def s_reward2_select():
        # ★ Рамка выделения 566 видна только ПОСЛЕ выбора строки: в режимах 1 и 2 эталон стартует
        #   с «ничего не выбрано» (−1). Команда идёт тем же телом, что и клик мышью.
        cmd("asda.Npc.RewardSelect 1")

    def s_reward2_ok():
        cmd("asda.Npc.DialogClick 567")

    # ── Э6-трижды: рамка выделения на РАЗНОМ ЧИСЛЕ выбираемых наград (1 / 2 / 3) ──
    #   Тройки взяты из `asda.Npc.RewardScan` предыдущего прогона, а не придуманы:
    #     zebian talkId=5  — выбираемых 1 (эпизод 356)
    #     117npc talkId=7  — выбираемых 2 (эпизод 372)  ← уже проверяется выше
    #     130npc talkId=36 — выбираемых 3 (эпизод 376)
    def s_reward_sel1_show():
        cmd("asda.Npc.RewardShow zebian 5 0 0")

    def s_reward_sel1_pick():
        cmd("asda.Npc.RewardSelect 0")

    def s_reward_sel1_dump():
        cmd("asda.Npc.RewardDump")

    def s_reward_sel1_shot():
        take_shot("npc_reward_select1")

    def s_reward_sel1_get():
        harvest("npc_reward_select1")

    def s_reward_sel1_ok():
        cmd("asda.Npc.DialogClick 567")

    def s_reward_sel3_show():
        cmd("asda.Npc.RewardShow 130npc 36 0 0")

    def s_reward_sel3_pick():
        # Последняя из трёх строк — самая нижняя: если бы рамка «разъезжалась», это видно именно тут.
        cmd("asda.Npc.RewardSelect 2")

    def s_reward_sel3_dump():
        cmd("asda.Npc.RewardDump")

    def s_reward_sel3_shot():
        take_shot("npc_reward_select3")

    def s_reward_sel3_get():
        harvest("npc_reward_select3")

    def s_reward_sel3_ok():
        cmd("asda.Npc.DialogClick 567")

    # Разговорная анимация NPC (порт uiPanelSelectTab): состояние 1 → клип anim_02, затем возврат в 0.
    def s_anim_talk():
        cmd("asda.Npc.AnimState 1")

    def s_anim_idle():
        cmd("asda.Npc.AnimState 0")

    PLAN = [s_travel, s_zone_before,
            s_open_ian, s_dump_ian, s_shot_ian, s_get_ian,
            s_page_63, s_dump_63, s_shot_63, s_get_63,
            s_click_menu,
            s_click_step, s_click_step2,
            s_close, s_dump_closed,
            s_open_rachel, s_dump_rachel, s_shot_rachel, s_get_rachel, s_close2,
            s_move_open, s_move_dump_before, s_move_close, s_move_dump_after,
            s_open_clark, s_clark_page15, s_clark_dump, s_clark_shot, s_clark_get,
            # состояния подписи кнопок (наведение/нажатие) — см. блок выше
            s_cap_rest, s_cap_hover, s_cap_hover_dump, s_cap_hover_shot, s_cap_hover_get,
            s_cap_press, s_cap_press_dump, s_cap_press_shot, s_cap_press_get, s_cap_wait,
            s_cap_after_dump, s_cap_unhover, s_cap_rest_dump,
            s_cap_reopen, s_cap_rehover, s_cap_click_short, s_cap_click_dump,
            s_cap_menu_hover, s_cap_menu_dump, s_cap_menu_shot, s_cap_menu_get, s_cap_menu_unhover,
            s_cap_all_unhover, s_cap_all_shot, s_cap_all_get,
            s_cap_next_hover, s_cap_next_shot, s_cap_next_get,
            s_cap_close_hover, s_cap_close_shot, s_cap_close_get, s_cap_all_unhover,
            s_clark_close,
            s_talk_nearest, s_zone_after, s_zone_calc, s_cancel_by_move,
            # ── Э3…Э5 ──
            s_open_108, s_cond_dump,
            s_fake_self, s_fake_quest, s_page_cond, s_cond_dump2, s_step_go, s_dump_after_step,
            s_fake_done, s_page_accept, s_cond_dump3, s_accept_quest,
            s_multiquest2, s_multiquest_dump2, s_cond_why,
            s_multiquest, s_multiquest_dump, s_multiquest_shot, s_multiquest_get, s_multiquest_pick,
            s_cmd_teleport, s_cmd_hunt, s_cmd_gap, s_cmd_savepoint, s_cmd_exchange, s_cmd_raid,
            # ── Э6 ──
            s_reward_scan, s_reward_show, s_reward_dump, s_reward_shot, s_reward_get, s_reward_ok,
            s_reward2_show, s_reward2_select, s_reward2_dump, s_reward2_shot, s_reward2_get, s_reward2_ok,
            s_reward_sel1_show, s_reward_sel1_pick, s_reward_sel1_dump, s_reward_sel1_shot,
            s_reward_sel1_get, s_reward_sel1_ok,
            s_reward_sel3_show, s_reward_sel3_pick, s_reward_sel3_dump, s_reward_sel3_shot,
            s_reward_sel3_get, s_reward_sel3_ok,
            s_anim_talk, s_anim_idle,
            s_close3]

    finish = PH.make_pie_finish(st, les, log, "=== готово, снимков запрошено %d ===", handle_is_not_none=True, count_shots=True)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 10 стр.)

    def on_tick(_dt):
        if st["busy"]:
            return
        st["busy"] = True
        try:
            st["tick"] += 1
            if st["tick"] > MAX_TICKS:
                unreal.log_error("[npc-диалог-живой] предел тиков")
                finish()
                return
            if st["tick"] < st["next_at"]:
                return
            st["step"] += 1
            if st["step"] >= len(PLAN):
                finish()
                return
            PLAN[st["step"]]()
            st["next_at"] = st["tick"] + (TRAVEL_SETTLE if st["step"] == 0 else SETTLE)
        except Exception:
            import traceback
            unreal.log_error("[npc-диалог-живой] ошибка:\n" + traceback.format_exc())
            finish()
        finally:
            st["busy"] = False

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    log("tick-callback зарегистрирован")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
