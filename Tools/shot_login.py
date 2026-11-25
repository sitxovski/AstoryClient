# Copyright (c) 2026 @sitxovski (Discord)
# Editor headless-скриншот Login через PIE + отложенный tick-callback (реальный RHI, RDP-safe).
# Не блокирует game thread: считает тики, на N-м делает HighResShot, копирует PNG в Reports.
import unreal, os, glob, shutil
def log(m): unreal.log("[shot_login] " + str(m))
DEST  = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/casket_login_shot.png"
SSDIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

state = {"n": 0, "shot": False, "handle": None}
def get_world():
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    try:
        w = ues.get_game_world()
        if w: return w
    except Exception: pass
    return unreal.EditorLevelLibrary.get_editor_world()

def on_tick(delta):
    state["n"] += 1
    n = state["n"]
    if n == 120 and not state["shot"]:      # ~120 тиков прогрева (материалы, streaming, заморозка)
        w = get_world()
        unreal.SystemLibrary.execute_console_command(w, "HighResShot 1024x768")
        log("HighResShot @tick %d world=%s" % (n, w))
        state["shot"] = True
    if n == 200:                            # ещё кадры на запись файла, затем финализируем
        pngs = glob.glob(os.path.join(SSDIR, "**", "*.png"), recursive=True)
        if pngs:
            pngs.sort(key=os.path.getmtime); shutil.copy(pngs[-1], DEST)
            log("СОХРАНЁН -> %s (%s)" % (DEST, os.path.basename(pngs[-1])))
        else:
            log("НЕТ PNG")
        unreal.unregister_slate_post_tick_callback(state["handle"])
        les.editor_request_end_play()
        log("ГОТОВО")


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    if os.path.isdir(SSDIR): shutil.rmtree(SSDIR, ignore_errors=True)
    unreal.EditorLoadingAndSavingUtils.load_map("/Game/Map/Login")
    log("Login загружена")
    les.editor_request_begin_play()   # старт PIE (GameMode StartPlay -> FreezeCasketClosed)
    log("PIE запущен")
    state["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    log("tick-callback зарегистрирован")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
