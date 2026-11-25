# Copyright (c) 2026 @sitxovski (Discord)
# _make_fxlag_map.py — пустой уровень для headless-замера отставания эффектов на персонаже
# (`asda.FxFollowLagTest`). Ничего в проекте не меняет, кроме /Game/Temp/_fxlag.
import unreal

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    les.new_level("/Game/Temp/_fxlag")
    les.save_current_level()
    unreal.log_warning("[fxlag] уровень /Game/Temp/_fxlag создан")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
