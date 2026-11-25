# Copyright (c) 2026 @sitxovski (Discord)
import unreal
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    les.load_level("/Game/Map/Login")
    ws = ues.get_editor_world().get_world_settings()
    gm = ws.get_editor_property("default_game_mode")
    unreal.log("[verify] GameMode = %s" % (gm.get_name() if gm else "NONE"))
    for a in eas.get_all_level_actors():
        unreal.log("[verify] actor: %s (%s)" % (a.get_actor_label(), a.get_class().get_name()))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
