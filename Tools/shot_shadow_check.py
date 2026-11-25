# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# Скриншоты для проверки полос теней (shadow acne): Login (PIE) + alpen (editor viewport).
# Реальный RHI (без -NullRHI), RDP-safe, паттерн shot_login.py. Тег до/после: env ASDA_SHOT_TAG.
import unreal, os, glob, shutil
TAG   = os.environ.get("ASDA_SHOT_TAG", "before")
OUTD  = r"@@ASTORY_DEV_ROOT_FWD@@/Reports"
SSDIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
def log(m): unreal.log("[shot_shadow] " + str(m))
def clear_ss():
    if os.path.isdir(SSDIR): shutil.rmtree(SSDIR, ignore_errors=True)
def grab(dest):
    pngs = glob.glob(os.path.join(SSDIR, "**", "*.png"), recursive=True)
    if pngs:
        pngs.sort(key=os.path.getmtime); shutil.copy(pngs[-1], dest); log("СОХРАНЁН -> " + dest)
    else:
        log("НЕТ PNG для " + dest)

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

ONLY = os.environ.get("ASDA_SHOT_ONLY", "")  # "alpen" = пропустить Login-фазу
state = {"n": 0, "phase": "login", "handle": None}

def get_world():
    try:
        w = ues.get_game_world()
        if w: return w
    except Exception: pass
    return unreal.EditorLevelLibrary.get_editor_world()

def start_alpen():
    clear_ss()
    unreal.EditorLoadingAndSavingUtils.load_map("/Game/Map/alpen/alpen")
    # камера: центр union-баундов террейн-мешей, сверху под 50° — земля+объекты в кадре
    mn = [1e12, 1e12, 1e12]; mx = [-1e12, -1e12, -1e12]; found = False
    for a in eas.get_all_level_actors():
        lb = a.get_actor_label().lower()
        if (lb.startswith("ter") or "terrain" in lb) and isinstance(a, unreal.StaticMeshActor):
            o, e = a.get_actor_bounds(False)
            mn[0] = min(mn[0], o.x - e.x); mx[0] = max(mx[0], o.x + e.x)
            mn[1] = min(mn[1], o.y - e.y); mx[1] = max(mx[1], o.y + e.y)
            mn[2] = min(mn[2], o.z - e.z); mx[2] = max(mx[2], o.z + e.z)
            found = True
    if found:
        cx = (mn[0] + mx[0]) * 0.5; cy = (mn[1] + mx[1]) * 0.5; topz = mx[2]
    else:
        cx, cy, topz = 25600.0, 25600.0, 500.0
    # alpen: карта X[-51200..0] Y[0..51200]; точка у кластера объектов (дамп alpen_actors.txt)
    cam = unreal.Vector(-39800.0, 14200.0, 3600.0)
    ues.set_level_viewport_camera_info(cam, unreal.Rotator(roll=0.0, pitch=-40.0, yaw=45.0))
    log("alpen bounds found=%s cam=(%.0f,%.0f,%.0f)" % (found, cam.x, cam.y, cam.z))
    log("alpen загружена, камера поставлена")

def on_tick(_):
    state["n"] += 1
    n = state["n"]
    if state["n"] == 60:
        extra = os.environ.get("ASDA_SHOT_CMDS", "")
        if extra:
            for c in extra.split(";"):
                unreal.SystemLibrary.execute_console_command(get_world(), c.strip())
                log("extra cmd: " + c.strip())
    if state["phase"] == "prealpen":
        if n == 30:
            start_alpen(); state["phase"] = "alpen"; state["n"] = 0
    elif state["phase"] == "login":
        if n == 150:
            unreal.SystemLibrary.execute_console_command(get_world(), "HighResShot 1280x720")
            log("HighResShot Login @%d" % n)
        if n == 240:
            grab(os.path.join(OUTD, "shadow_login_%s.png" % TAG))
            les.editor_request_end_play()
        if n == 300:
            start_alpen(); state["phase"] = "alpen"; state["n"] = 0
    elif state["phase"] == "alpen":
        if n == 150:
            unreal.SystemLibrary.execute_console_command(get_world(), "HighResShot 1280x720")
            log("HighResShot alpen @%d" % n)
        if n == 260:
            grab(os.path.join(OUTD, "shadow_alpen_%s.png" % TAG))
            unreal.unregister_slate_post_tick_callback(state["handle"])
            log("ГОТОВО, выходим")
            unreal.SystemLibrary.quit_editor()


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    if ONLY == "alpen":
        state["phase"] = "prealpen"
    else:
        clear_ss()
        unreal.EditorLoadingAndSavingUtils.load_map("/Game/Map/Login")
        log("Login загружена; PIE")
        les.editor_request_begin_play()
    state["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    log("tick-callback зарегистрирован, TAG=" + TAG)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
