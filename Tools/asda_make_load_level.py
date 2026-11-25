# Copyright (c) 2026 @sitxovski (Discord)
# asda_make_load_level.py — создать стартовый уровень /Game/Maps/LOAD (загрузочный экран).
# Запуск: Tools -> Execute Python Script. Пока пустой уровень (на него повесим Start_logo + логин позже).
import unreal

# ★ D5 (волна 2 п. 7, 14.09.2026, сверка с Content): /Game/Maps/LOAD нет (загрузочный уровень порта — L_Loading); путь не менялся.
PATH = "/Game/Maps/LOAD"
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    if unreal.EditorAssetLibrary.does_asset_exist(PATH):
        unreal.log("LOAD уровень уже существует: %s" % PATH)
    else:
        if les.new_level(PATH):
            les.save_current_level()
            unreal.log("Создан стартовый уровень: %s" % PATH)
        else:
            unreal.log_warning("Не удалось создать уровень %s" % PATH)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
