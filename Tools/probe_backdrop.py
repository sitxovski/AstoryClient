# Copyright (c) 2026 @sitxovski (Discord)
import unreal


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    m = unreal.load_object(None, "/Game/AsdaAssets/Models/Char/cre_creback-a_00/StaticMeshes/cre_creback-a_00")
    if m:
        b = m.get_bounding_box()
        unreal.log("[probe] bounds min=%s max=%s" % (b.min, b.max))
        ext = b.max - b.min
        unreal.log("[probe] size XYZ = %.1f %.1f %.1f" % (ext.x, ext.y, ext.z))
        unreal.log("[probe] center = %s" % ((b.min+b.max)*0.5))
    else:
        unreal.log("[probe] меш не найден")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
