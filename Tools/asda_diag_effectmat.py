# Copyright (c) 2026 @sitxovski (Discord)
# asda_diag_effectmat.py — рекурсивный дамп цепи Emissive/Opacity мастеров эффектов.
import unreal
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

def dump(expr, depth=0):
    if expr is None or depth > 6:
        return
    name = type(expr).__name__
    extra = ""
    try:
        if isinstance(expr, unreal.MaterialExpressionScalarParameter):
            extra = " name=%s def=%.2f" % (expr.get_editor_property("parameter_name"), expr.get_editor_property("default_value"))
        if isinstance(expr, unreal.MaterialExpressionTextureSampleParameter2D):
            extra = " name=%s" % expr.get_editor_property("parameter_name")
    except Exception: pass
    unreal.log("%s%s%s" % ("  "*depth, name, extra))
    for prop in ("a", "b", "input", "coordinates", "alpha"):
        try:
            inp = expr.get_editor_property(prop)
            sub = inp.get_editor_property("expression") if inp else None
            if sub: dump(sub, depth+1)
        except Exception: pass


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global prop
    for p in ("/Game/Effects/M_AsdaEffectAdd",):
        m = EAL.load_asset(p)
        if not m: continue
        for prop, nm in ((unreal.MaterialProperty.MP_EMISSIVE_COLOR, "EMISSIVE"), (unreal.MaterialProperty.MP_OPACITY, "OPACITY")):
            node = MEL.get_material_property_input_node(m, prop)
            unreal.log("== %s %s ==" % (p.rsplit('/',1)[-1], nm))
            dump(node, 1)
    unreal.log("=== MAT DIAG2 DONE ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
