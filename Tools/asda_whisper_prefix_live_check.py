# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_whisper_prefix_live_check.py — ЖИВАЯ (PIE) проверка ВИДИМОЙ обратной связи при выборе шёпота.
#
# ЖАЛОБА ВЛАДЕЛЬЦА (30.08.2026): «нажимал [Шепот], НО в чате не было этого видно! Оригинал пишет
#   "шепот с" в чате». Именно поэтому он жал кнопку шесть раз — и получил залипание интерфейса.
#
# ЧТО ДЕЛАЕТ ОРИГИНАЛ (`CChatList_OnSelectWhisper@0xDBB7C0`, chat.c:6280):
#   sprintf(buf, "/w %s ", имя);  uiPanel_SetFocusChildren(chat, 0);  uiChatInputShowWithText(chat, buf)
#   → `uiChatInputShowWithText@0xBA3E80` (ui_core.c:12037) кладёт ЭТОТ ТЕКСТ В САМО ПОЛЕ ВВОДА,
#     отдаёт полю фокус и показывает окно чата. Строки в ленте чата при этом НЕ появляется.
#
# ЧТО ПРОВЕРЯЕТСЯ (каждый пункт оставляет строку-доказательство):
#   1. до выбора шёпота поле ввода пустое;
#   2. пункт «Шепот» 2054 меню игрока (жмётся ЕГО СОБСТВЕННОЙ кнопкой) кладёт в поле «/w имя »;
#   3. режим ввода становится 7, адресат запомнен;
#   4. КАДР экрана с подставленным текстом;
#   5. отправка: «/w имя привет» уходит как шёпот, префикс в текст не уезжает;
#   6. сверка флага ввода не сломана: клик мимо поля гасит флаг, мусора в поле не остаётся;
#   7. второй мышиный путь — пункт «Шепот» 80003 меню по нику в ленте чата.
#
# ЗАПУСК:  Tools\run_headless_whisper_prefix.bat
import os
import shutil

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/whisper_prefix_live"
SS_ROOT = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
LEVEL = "/Game/Map/alpen/alpen"

WARMUP = 300
TRAVEL_SETTLE = 400
SETTLE = 60
MAX_TICKS = 40000
PEER = "Rimy"


def log(m):
    import unreal
    unreal.log("[шёпот-видимость] " + str(m))


def main():
    import unreal

    if os.path.isdir(SS_ROOT):
        shutil.rmtree(SS_ROOT, ignore_errors=True)
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

    def mark(t):
        log("═══════════ %s ═══════════" % t)

    PLAN = [
        (lambda: cmd("open /Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode"), TRAVEL_SETTLE),
        # Без этих двух крюков нет ни павна, ни окон: тик подсистем UI гейтится теми же условиями.
        (lambda: cmd("Asda.Load.CurrentMap 3"), 10),
        (lambda: cmd("asda.World.FakeSelfSpawn 256 256 WhisperProbe"), 200),

        (lambda: mark("1. ДО ВЫБОРА ШЁПОТА: поле ввода обязано быть пустым"), 2),
        (lambda: cmd("asda.ChatWin.InputState"), 20),

        (lambda: mark("2. ПУНКТ «ШЕПОТ» 2054 МЕНЮ ИГРОКА — жмём ЕГО СОБСТВЕННОЙ кнопкой"), 2),
        (lambda: cmd("asda.PlayerMenu.FakePeer 7 %s" % PEER), 40),
        (lambda: cmd("asda.PlayerMenu.Open 7"), 40),
        (lambda: cmd("asda.PlayerMenu.Dump"), 20),
        (lambda: cmd("asda.PlayerMenu.Click 2054"), 40),
        # ★ ГЛАВНОЕ ДОКАЗАТЕЛЬСТВО: в поле 2609 стоит «/w Rimy », режим 7, фокус у поля.
        (lambda: cmd("asda.ChatWin.InputState"), 30),
        (lambda: cmd("asda.ChatWin.DumpCombo"), 30),

        (lambda: mark("3. КАДР: видно ли подставленный текст на экране"), 2),
        (lambda: cmd("shot showui"), 120),
        (lambda: cmd("HighResShot 1280x720"), 120),

        (lambda: mark("4. ОТПРАВКА: «/w имя привет» — префикс обязан быть ВЫРЕЗАН парсером"), 2),
        (lambda: cmd("asda.ChatWin.Say 4 /w %s привет" % PEER), 40),
        (lambda: cmd("asda.ChatWin.InputState"), 30),

        (lambda: mark("5. СВЕРКА ФЛАГА НЕ СЛОМАНА: клик мимо поля гасит флаг"), 2),
        (lambda: cmd("asda.ChatWin.InputOpen 7"), 4),
        (lambda: cmd("asda.ChatWin.InputState"), 20),
        (lambda: cmd("asda.UI.SimClick 40 40 8"), 40),
        (lambda: cmd("asda.ChatWin.InputState"), 30),

        (lambda: mark("6. ВТОРОЙ МЫШИНЫЙ ПУТЬ: пункт «Шепот» 80003 меню по нику в ленте"), 2),
        (lambda: cmd("asda.ChatWin.NickMenu %s" % PEER), 40),
        (lambda: cmd("asda.ChatWin.DumpMenu"), 40),
    ]

    def finish():
        try:
            for root, _dirs, files in os.walk(SS_ROOT):
                for f in files:
                    if f.lower().endswith(".png"):
                        shutil.copy2(os.path.join(root, f), os.path.join(OUT_DIR, f))
                        log("кадр сохранён: %s" % f)
        except Exception as e:
            log("кадры скопировать не удалось: %s" % e)
        try:
            les.editor_request_end_play()
        except Exception:
            pass
        if st["handle"]:
            unreal.unregister_slate_post_tick_callback(st["handle"])
            st["handle"] = None
        log("ЗАВЕРШЕНО")
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
            unreal.log_error("[шёпот-видимость] ошибка:\n" + traceback.format_exc())
            finish()
        finally:
            st["busy"] = False

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    log("tick-callback зарегистрирован")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
