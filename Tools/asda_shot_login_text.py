# Copyright (c) 2026 @sitxovski (Discord)
# asda_shot_login_text.py — кадр экрана логина С ИНТЕРФЕЙСОМ, чтобы увидеть, как рисуется текст.
#
# Зачем: после возврата альфы двум растровым атласам `font` надо убедиться, что текст в игре не
# изменился. Атласы в проекте ни на что не ссылаются (проверено реестром и двоичным поиском по
# Content), текст рисуют UFont-ассеты `AsdaUIFont*` — этот кадр закрывает проверку глазами.
#
# ★ Отличие от `shot_login.py`: тот снимает на 120-м тике, когда виджеты логина ещё не построены,
#   и в кадре одна сцена без интерфейса. Здесь снимок делается позже (SHOT_TICK), когда окно
#   логина уже на экране.
#
# Запуск: Tools/run_headless_rhi_tick.bat Tools/asda_shot_login_text.py
# Выход:  Reports/alpha_fix/login_text.png
import glob
import os
import shutil

import unreal

DEST = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/alpha_fix/login_text.png"
SSDIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
SHOT_TICK = 900          # виджеты логина строятся сильно позже прогрева сцены
END_TICK = 1050

state = {"n": 0, "shot": False, "handle": None}


def log(m):
    unreal.log("[текст] " + str(m))


def get_world():
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    try:
        w = ues.get_game_world()
        if w:
            return w
    except Exception:
        pass
    return unreal.EditorLevelLibrary.get_editor_world()


def on_tick(_delta):
    state["n"] += 1
    n = state["n"]
    if n == SHOT_TICK and not state["shot"]:
        # ★ `HighResShot <ширина>x<высота>` снимает ТОЛЬКО сцену — интерфейс в кадр не попадает
        #   (проверено: два прогона дали пейзаж логина без единой надписи). Нужен снимок С UI,
        #   поэтому идём через AutomationLibrary с `capture_ui=True`, а консольная команда
        #   остаётся запасным вариантом.
        done = False
        try:
            unreal.AutomationLibrary.take_high_res_screenshot(1024, 768, "login_ui", None,
                                                              False, False, 0.0, "", 0.0, True)
            done = True
            log("снимок с UI (AutomationLibrary) на тике %d" % n)
        except Exception as exc:
            log("AutomationLibrary не сработала (%s) — пробуем консоль" % exc)
        if not done:
            unreal.SystemLibrary.execute_console_command(get_world(), "HighResShot 1024x768")
            log("снимок консолью на тике %d" % n)
        state["shot"] = True
    if n == END_TICK:
        pngs = glob.glob(os.path.join(SSDIR, "**", "*.png"), recursive=True)
        if pngs:
            pngs.sort(key=os.path.getmtime)
            if not os.path.isdir(os.path.dirname(DEST)):
                os.makedirs(os.path.dirname(DEST))
            shutil.copy(pngs[-1], DEST)
            log("СОХРАНЁН -> %s" % DEST)
        else:
            log("НЕТ PNG")
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
        log("ГОТОВО")
        # ★ Одного `editor_request_end_play` МАЛО: редактор остаётся жить, и headless-процесс
        #   висит до тайм-аута (замер: два процесса UnrealEditor-Cmd остались после прогонов).
        #   Выход просим явно, следующим тиком — чтобы PIE успел свернуться.
        state["quit_at"] = state["n"] + 30
    if state.get("quit_at") and state["n"] >= state["quit_at"]:
        if state["handle"] is not None:
            unreal.unregister_slate_post_tick_callback(state["handle"])
            state["handle"] = None
        unreal.SystemLibrary.quit_editor()


def main():
    if os.path.isdir(SSDIR):
        shutil.rmtree(SSDIR, ignore_errors=True)
    unreal.EditorLoadingAndSavingUtils.load_map("/Game/Map/Login")
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
    state["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    log("PIE запущен, снимок будет на тике %d" % SHOT_TICK)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
