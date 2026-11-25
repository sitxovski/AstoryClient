# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _shot_gacha_bars5.py — ЖИВОЙ кадр PIE: пять полос премиум-гачи с вывеской, цифрой и двумя кнопками.
#
# ★★★ ЗАЧЕМ ИМЕННО PIE. Оффлайн-дамп WBP показывает СТАТИЧЕСКОЕ состояние ассета (там полосы 3…5
#   свёрнуты импортёром), а нас интересует состояние ПОСЛЕ работы кода сцены — выравнивания
#   видимости по эталонной полосе 1 в `UAsdaPremiumGachaSubsystem::SetupControls`. Увидеть это
#   можно только на живом кадре (закон: проверять на ЖИВОМ PIE, а не по оффлайн-дампам).
#
# ЗАПУСК: Tools\run_headless_rhi_tick.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\_shot_gacha_bars5.py
#   (именно `_tick`: без тика редактора PIE не крутит кадры и скриншотов не будет).
import glob
import os
import shutil

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/gacha_slots5_ui"
SS_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
LEVEL = "/Game/Map/Login"

WARMUP = 240        # прогрев PIE: шейдеры, стриминг, GameMode
AFTER_OPEN = 240    # окно гачи открылось — дать сцене собраться, полосам сесть на проекцию
SETTLE = 60
MAX_TICKS = 20000


def log(m):
    import unreal
    unreal.log("[пять-полос] " + str(m))


def main():
    import unreal

    if os.path.isdir(SS_DIR):
        shutil.rmtree(SS_DIR, ignore_errors=True)
    os.makedirs(OUT_DIR, exist_ok=True)

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

    # ★★★ ЗАМЕТКА О РАЗМЕРЕ КАДРА. Полосы садятся на ПРОЕКЦИЮ 3D-якорей, поэтому их разлёт по X
    #   пропорционален ВЫСОТЕ вьюпорта: screenX = x·h / (2·tan(fovY/2)·z) — аспект в формуле
    #   сокращается. В маленьком окне редактора (замерено 1082×344) пять полос шириной 267 px
    #   наезжают друг на друга; в нормальном (2038×782) стоят раздельно. Это геометрия эталонной
    #   формулы, а не дефект раскладки, и лечится размером окна редактора, а не кодом.
    #   Зафиксировать размер окна PIE из Python НЕЧЕМ: класса `LevelEditorPlaySettings` в обёртке
    #   `unreal` нет, а у CDO по пути `/Script/UnrealEd.Default__LevelEditorPlaySettings` свойства
    #   `new_window_width` не находится (проверено запуском). Поэтому размер печатается в лог
    #   строкой «68059/68056 растянуты на экран: WxH» — по ней и судим о кадре.

    unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
    les.editor_request_begin_play()
    log("PIE запущен, уровень %s" % LEVEL)

    st = {"tick": 0, "step": -1, "next_at": WARMUP, "handle": None, "busy": False, "shots": []}

    def game_world():
        try:
            return ues.get_game_world()
        except Exception:
            return None

    def take_shot(tag):
        unreal.SystemLibrary.execute_console_command(game_world(), "shot showui")
        st["shots"].append(tag)
        log("запрошен снимок '%s' на тике %d" % (tag, st["tick"]))

    def harvest(tag):
        pngs = glob.glob(os.path.join(SS_DIR, "**", "*.png"), recursive=True)
        if not pngs:
            log("⚠ снимок '%s' НЕ появился" % tag)
            return
        pngs.sort(key=os.path.getmtime)
        dst = os.path.join(OUT_DIR, "%s.png" % tag)
        shutil.copy(pngs[-1], dst)
        os.remove(pngs[-1])
        log("снимок '%s' -> %s (%d байт)" % (tag, dst, os.path.getsize(dst)))

    def s_open():
        w = game_world()
        if not w:
            raise RuntimeError("нет игрового мира — PIE не стартовал")
        unreal.SystemLibrary.execute_console_command(w, "asda.Gacha.Open")
        log("asda.Gacha.Open отправлена")

    def s_check():
        w = game_world()
        acts = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.AsdaGachaSceneActor)
        if not acts:
            raise RuntimeError("AAsdaGachaSceneActor в PIE не найден — сцена не создана")
        log("автоматов в сцене: %d" % acts[0].get_machine_count())
        # ★★★ ДОКАЗАТЕЛЬСТВА В ЛОГ, А НЕ «НА ГЛАЗ»: эффективная видимость каждой полосы, UV
        #   плана-цифры (номер автомата — кусок атласа) и наличие обеих кнопок.
        unreal.SystemLibrary.execute_console_command(w, "asda.Gacha.BarsDump")
        # ★★★ И САМИ КНОПКИ — НАЖАТИЕМ: у всех пяти полос дёргается `OnClicked` (тот же мультикаст,
        #   что вызывает мышь), в лог идёт номер слота, размер витрины и цена. Сеть не трогается.
        unreal.SystemLibrary.execute_console_command(w, "asda.Gacha.BarsClick")
        # Заморозка: полосы садятся на проекцию каждый кадр, снимки должны быть одинаковы.
        unreal.SystemLibrary.execute_console_command(w, "pause")
        log("мир на паузе")

    def s_shot():
        take_shot("bars5_live")

    def s_get():
        harvest("bars5_live")

    PLAN = [s_open, s_check, s_shot, s_get]
    DELAYS = [AFTER_OPEN, SETTLE, SETTLE, SETTLE]

    def finish():
        if st["handle"] is not None:
            unreal.unregister_slate_post_tick_callback(st["handle"])
            st["handle"] = None
        try:
            les.editor_request_end_play()
        except Exception:
            pass
        log("=== готово, снимков запрошено %d ===" % len(st["shots"]))
        unreal.SystemLibrary.quit_editor()

    def on_tick(_dt):
        if st["busy"]:
            return
        st["busy"] = True
        try:
            st["tick"] += 1
            if st["tick"] > MAX_TICKS:
                unreal.log_error("[пять-полос] предел тиков")
                finish()
                return
            if st["tick"] < st["next_at"]:
                return
            st["step"] += 1
            if st["step"] >= len(PLAN):
                finish()
                return
            PLAN[st["step"]]()
            st["next_at"] = st["tick"] + DELAYS[st["step"]]
        except Exception:
            import traceback
            unreal.log_error("[пять-полос] ошибка:\n" + traceback.format_exc())
            finish()
        finally:
            st["busy"] = False

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    log("tick-callback зарегистрирован")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
