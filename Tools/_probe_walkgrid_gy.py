# Copyright (c) 2026 @sitxovski (Discord)
import unreal


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    da = unreal.load_asset("/Game/Map/alpen/DA_WalkGrid")
    unreal.log("[probe] da=%s" % da)
    unreal.log("[probe] W=%s H=%s cell=%s vh=%s" % (da.get_editor_property("width"), da.get_editor_property("height"),
        da.get_editor_property("cell_size"), len(da.get_editor_property("vert_heights"))))
    r = da.get_ground_y(11000.0, 39100.0)
    unreal.log("[probe] get_ground_y -> %r (type %s)" % (r, type(r)))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
