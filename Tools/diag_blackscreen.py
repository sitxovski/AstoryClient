# Copyright (c) 2026 @sitxovski (Discord)
import unreal
def L(m): unreal.log("[DIAG] " + str(m))
# 1. Меш blackscreen
mp = "/Game/AsdaAssets/Models/Char/blackscreen/StaticMeshes/blackscreen.blackscreen"
# 2. Материал
matp = "/Game/Effects/M_AsdaBlackScreen.M_AsdaBlackScreen"


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    sm = unreal.load_object(None, mp)
    if sm:
        b = sm.get_bounds()
        L("MESH bounds origin=%s extent=%s" % (b.origin, b.box_extent))
        L("MESH материалов=%d секций LOD0=%d" % (sm.get_num_sections(0) if hasattr(sm,'get_num_sections') else -1, -1))
        try:
            for i,s in enumerate(sm.static_materials): L("  slot %d: %s" % (i, s.material_interface))
        except Exception as e: L("slots err %s" % e)
    else:
        L("MESH НЕ найден: %s" % mp)
    mat = unreal.load_object(None, matp)
    if mat:
        L("MAT blend=%s shading=%s two_sided=%s" % (mat.get_editor_property("blend_mode"), mat.get_editor_property("shading_model"), mat.get_editor_property("two_sided")))
        exprs = unreal.MaterialEditingLibrary.get_material_expressions(mat) if hasattr(unreal.MaterialEditingLibrary,'get_material_expressions') else []
        L("MAT выражений=%d" % len(exprs))
        for e in exprs: L("  expr %s" % e.get_class().get_name())
    else:
        L("MAT НЕ найден: %s" % matp)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
