# Copyright (c) 2026 @sitxovski (Discord)
# Диагностика: мировые позиции реперных костей LoginBackdrop (сверка glTF→UE конверсии осей).
# Запуск: run_headless.bat ue_probe_creback_bones.py
import unreal

BONES = ["cre_creback-a_00_all link", "a00_00", "b00_00", "a00_01", "c01_00", "g00_00"]

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    les.load_level("/Game/Map/Login")
    for a in eas.get_all_level_actors():
        if a.get_actor_label() == "LoginBackdrop_cre_creback":
            comp = a.skeletal_mesh_component
            unreal.log("[probe] actor at %s" % a.get_actor_location())
            for bn in BONES:
                t = comp.get_socket_transform(unreal.Name(bn), unreal.RelativeTransformSpace.RTS_WORLD)
                l = t.translation
                unreal.log("[probe] %-28s UE=(%.2f, %.2f, %.2f)" % (bn, l.x, l.y, l.z))
            break
    else:
        unreal.log("[probe] АКТЁР НЕ НАЙДЕН")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
