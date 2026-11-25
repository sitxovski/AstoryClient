# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_caption_states_live_check.py — ЖИВАЯ (PIE) проверка ПОЛНОГО ПЕРЕИМПОРТА JUI.
#
# ★★★ ЗАЧЕМ ИМЕННО ЖИВАЯ (закон `false-negatives-method`). Офлайн-сверка снимков `.uasset`
#   доказывает, что данные состояний легли в стиль кнопки, и НЕ доказывает, что состояние
#   доезжает до подписи: между стилем и текстом стоит рантайм (`AsdaUi::BindButtonCaptionStates`),
#   который вешает реле на живые события Slate. Именно этот последний шаг и проверяем.
#
# ЧТО ПРОВЕРЯЕТСЯ по КАЖДОМУ окну из таблицы `WINDOWS` (окна разных классов .JUI и разных скинов):
#   1. окно открывается из переимпортированного ассета   — `asda.UI.OpenByPath`;
#   2. состояния подписи ЕСТЬ в стиле кнопок             — `asda.UI.DumpCaptionStates`;
#   3. НАВЕДЕНИЕ меняет цвет подписи                     — `asda.UI.SimHoverWidget` + дамп;
#   4. НАЖАТИЕ меняет цвет и сдвигает подпись            — `asda.UI.SimClickWidget <кадров>` + дамп;
#   5. уход курсора возвращает цвет покоя                — `asda.UI.SimHoverWidget` без аргументов;
#   6. ручки перетаскивания на месте                     — `asda.UI.DumpDrag`;
#   7. окно реально рисуется, тексты на месте            — снимок `shot showui`.
#
# ЗАПУСК: Tools\run_headless_ui_caption.bat  (нужен ТИК редактора и большой вьюпорт — см. .bat).
import glob
import os
import shutil

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/ui_caption_live"
SS_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
LEVEL = "/Game/Map/alpen/alpen"

WARMUP = 300
TRAVEL_SETTLE = 400
SETTLE = 50
MAX_TICKS = 60000

# (ИдОкна, имя ассета WBP, имя кнопки, ожидаемые цвета скина N/H/P, класс .JUI, чем окно интересно)
WINDOWS = [
    ("INVEN_WIN",        "WBP_INVEN_WIN",        "Inven_Smelting_Btn",
     ("FF323232", "FF808080", "FF000000"), "gamemain", "инвентарь, скин WkBtn_W140, 3 ручки"),
    ("QUEST_WIN",        "WBP_QUEST_WIN",        "QUEST_btn_b1",
     ("FFCDCDCD", "FFFFFFFF", "FF808080"), "gamemain", "квесты, скин Long_btn"),
    ("SYSTEM_WIN",       "WBP_SYSTEM_WIN",       "SYSTEM_CONFIRMBUT",
     ("FF323232", "FF808080", "FF000000"), "gamemain", "системное окно, скин WkBtn_W72"),
    ("COMMUNITY_WIN",    "WBP_COMMUNITY_WIN",    "CMTY_CLAN_SEC_BTN",
     ("FFE9D6BC", "FFFFFFFF", "FFCDCDCD"), "gamemain", "сообщество, 53 кнопки, скин RdBtn_W100"),
    ("SHELLITEM_STORE_WIN", "WBP_SHELLITEM_STORE_WIN", "SHELLITEM_POP00_BUT0_b",
     ("FFCDCDCD", "FFFFFFFF", "FF808080"), "gamemain", "магазин за шеллы, 155 кнопок, BKBtn_W122"),
    ("BOOSTER_SHOP_WIN", "WBP_BOOSTER_SHOP_WIN", "BOOSTER_ITEM_PUR00",
     ("FFCDCDCD", "FFFFFFFF", "FF808080"), "gamemain", "магазин бустеров, 46 кнопок"),
    ("Title_Win",        "WBP_Title_Win",        "Title_BtnOk",
     ("FF323232", "FF808080", "FF000000"), "gamemain", "титулы, есть и Npc_Shop-кнопка"),
    ("NUMPAD_WIN",       "WBP_NUMPAD_WIN",       "NUMPAD_NO00",
     ("FF555555", "FF808080", "FF323232"), "gamemain", "цифровая клавиатура, скин SmWtBtn_W18"),
    ("EXCHANGE_WIN",     "WBP_EXCHANGE_WIN",     "EXCH_OK",
     ("FF323232", "FF808080", "FF000000"), "gamemain", "обмен; тут же дефект оригинала со сдвигом X"),
    ("Premium_PickBAR_Win", "WBP_Premium_PickBAR_Win", "Premium_PickBAR_coupon1",
     ("FF562E0A", "FF562E0A", "FFFBE8D7"), "gamemain", "премиум-гача, скин Mac_Btn"),
    ("COMMONWIN_MSGBOX_CommonWin", "WBP_COMMONWIN_MSGBOX_CommonWin", "COMMONWIN_BUT",
     ("FF323232", "FF808080", "FF000000"), "commonwin", "★ класс commonwin"),
    ("Job_Guide_Win_Temp", "WBP_Job_Guide_Win_Temp", "Job_Guide_Move2NPCBtn",
     ("FF562E0A", "FFB56115", "FFFBE8D7"), "temp", "★ класс temp"),
    ("LOGIN_WIN",        "WBP_LOGIN_WIN",        "LOGIN_BUT00",
     ("FFFFD71E", "FFFFF2B5", "FF6F5B00"), "login", "★ класс login, скин Log_NomYellow_BarBtn"),
]


def log(m):
    import unreal
    unreal.log("[переимпорт-живой] " + str(m))


def main():
    import unreal

    if os.path.isdir(SS_DIR):
        shutil.rmtree(SS_DIR, ignore_errors=True)
    if os.path.isdir(OUT_DIR):
        shutil.rmtree(OUT_DIR, ignore_errors=True)
    os.makedirs(OUT_DIR)

    # Путь ассета ищем по ИМЕНИ во всём дереве UI: раскладка окон по тематическим папкам
    #   (`asda_player_ui_layout.py`) не зашита в этот скрипт и может меняться.
    byname = {}
    for a in unreal.EditorAssetLibrary.list_assets("/Game/AsdaAssets/UI", recursive=True,
                                                   include_folder=False):
        p = a.split(".")[0]
        byname[p.rsplit("/", 1)[-1]] = p

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

    unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
    les.editor_request_begin_play()
    log("PIE запущен, ассетов UI найдено %d" % len(byname))

    st = {"tick": 0, "step": -1, "next_at": WARMUP, "handle": None, "busy": False, "shots": 0}

    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    world = PH.make_game_world(ues)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 5 стр.)

    cmd = PH.make_console_cmd(world, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 6 стр.)

    def take_shot(tag):
        # ★ ЕДИНСТВЕННЫЙ способ снять кадр ВМЕСТЕ С ИНТЕРФЕЙСОМ — `shot showui`.
        cmd("shot showui")
        st["shots"] += 1

    harvest = PH.make_harvest(SS_DIR, OUT_DIR, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 10 стр.)

    PLAN = []

    def add(fn):
        PLAN.append(fn)

    def s_travel():
        cmd("open /Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode")

    add(s_travel)

    for win_id, asset, btn, colors, cls, why in WINDOWS:
        path = byname.get(asset)

        def mk(win_id=win_id, asset=asset, btn=btn, colors=colors, cls=cls, why=why, path=path):
            def open_win():
                log("═" * 90)
                log("ОКНО %s (класс %s) — %s; кнопка %s; ожидаем подпись %s→%s→%s"
                    % (win_id, cls, why, btn, colors[0], colors[1], colors[2]))
                if not path:
                    log("⚠ АССЕТА %s НЕТ — окно пропущено" % asset)
                    return
                cmd("asda.UI.OpenByPath %s %s" % (win_id, path))

            def dump_rest():
                cmd("asda.UI.DumpCaptionStates %s" % win_id)

            def drag():
                cmd("asda.UI.DumpDrag")

            def shot_rest():
                take_shot("%s_rest" % win_id)

            def get_rest():
                harvest("%s_rest" % win_id)

            def hover():
                cmd("asda.UI.SimHoverWidget %s %s" % (win_id, btn))

            def dump_hover():
                cmd("asda.UI.DumpCaptionStates %s" % win_id)

            def shot_hover():
                take_shot("%s_hover" % win_id)

            def get_hover():
                harvest("%s_hover" % win_id)

            def press():
                # Удержание 150 кадров: дамп приходит через SETTLE=50 кадров, то есть
                #   ГАРАНТИРОВАННО во время нажатия (кадр 2).
                cmd("asda.UI.SimClickWidget %s %s 150" % (win_id, btn))

            def dump_press():
                cmd("asda.UI.DumpCaptionStates %s" % win_id)

            def shot_press():
                take_shot("%s_press" % win_id)

            def get_press():
                harvest("%s_press" % win_id)

            def unhover():
                cmd("asda.UI.SimHoverWidget")

            def dump_after():
                cmd("asda.UI.DumpCaptionStates %s" % win_id)

            def close():
                cmd("asda.UI.Front %s" % win_id)   # без закрытия: окна не мешают, z-порядок виден

            return [open_win, dump_rest, drag, shot_rest, get_rest,
                    hover, dump_hover, shot_hover, get_hover,
                    press, dump_press, shot_press, get_press,
                    unhover, dump_after]

        for f in mk():
            add(f)

    def finish():
        if st["handle"] is not None:
            unreal.unregister_slate_post_tick_callback(st["handle"])
            st["handle"] = None
        try:
            les.editor_request_end_play()
        except Exception:
            pass
        log("=== готово, снимков запрошено %d ===" % st["shots"])
        unreal.SystemLibrary.quit_editor()

    def on_tick(_dt):
        if st["busy"]:
            return
        st["busy"] = True
        try:
            st["tick"] += 1
            if st["tick"] > MAX_TICKS:
                unreal.log_error("[переимпорт-живой] предел тиков")
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
            unreal.log_error("[переимпорт-живой] ошибка:\n" + traceback.format_exc())
            finish()
        finally:
            st["busy"] = False

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    log("tick-callback зарегистрирован, шагов %d" % len(PLAN))


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
