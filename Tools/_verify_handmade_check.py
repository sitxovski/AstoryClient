# Copyright (c) 2026 @sitxovski (Discord)
import unreal, sys
sys.path.insert(0, r"@@ASTORY_DEV_ROOT@@\AstoryClient\Tools")
import asda_ui_handmade as H


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    try:
        H.verify()
        unreal.log("РЕЗУЛЬТАТ-ПРОВЕРКИ: ручные правки НА МЕСТЕ")
    except Exception as e:
        unreal.log_error("РЕЗУЛЬТАТ-ПРОВЕРКИ: ПРОВАЛ — %s" % e)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
