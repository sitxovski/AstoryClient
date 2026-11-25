# Copyright (c) 2026 @sitxovski (Discord)
# Проверка: какие имена входов принимает connect_material_expressions + ВОСПРОИЗВЕДЕНИЕ старого графа
import unreal
mel=unreal.MaterialEditingLibrary; at=unreal.AssetToolsHelpers.get_asset_tools(); EAL=unreal.EditorAssetLibrary
p="/Game/Materials/__probe_conn"
def E(c,x,y): return mel.create_material_expression(m,c,x,y)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global m
    if EAL.does_asset_exist(p): EAL.delete_asset(p)
    m=at.create_asset("__probe_conn","/Game/Materials",unreal.Material,unreal.MaterialFactoryNew())
    src=E(unreal.MaterialExpressionConstant,-600,0); src.set_editor_property("r",0.5)
    for cls,names in [(unreal.MaterialExpressionClamp,["","Input","In"]),
                      (unreal.MaterialExpressionOneMinus,["","Input","In"]),
                      (unreal.MaterialExpressionPower,["Base","Exp","Exponent",""]),
                      (unreal.MaterialExpressionMin,["A","B",""]),
                      (unreal.MaterialExpressionFresnel,["ExponentIn","Exponent","","Normal"])]:
        n=E(cls,-300,0)
        for nm in names:
            unreal.log("[N] %s.'%s' = %s" % (cls.__name__, nm, mel.connect_material_expressions(src,"",n,nm)))
    # --- воспроизведение СТАРОГО графа (что было при белом силуэте) ---
    col=E(unreal.MaterialExpressionVectorParameter,-900,-300); col.set_editor_property("parameter_name","Color")
    mul=E(unreal.MaterialExpressionMultiply,-100,-300)
    unreal.log("[OLD] VectorParameter['RGB']->Multiply.A = %s" % mel.connect_material_expressions(col,"RGB",mul,"A"))
    unreal.log("[OLD] VectorParameter['']->Multiply.A   = %s" % mel.connect_material_expressions(col,"",mul,"A"))
    unreal.log("[OLD] Multiply const_a=%s const_b=%s" % (mul.get_editor_property("const_a"), mul.get_editor_property("const_b")))
    add=E(unreal.MaterialExpressionAdd,-400,-300)
    unreal.log("[OLD] Add const_a=%s const_b=%s" % (add.get_editor_property("const_a"), add.get_editor_property("const_b")))
    fr=E(unreal.MaterialExpressionFresnel,-700,300)
    unreal.log("[OLD] Fresnel Exponent=%s BaseReflectFraction=%s" % (fr.get_editor_property("exponent"), fr.get_editor_property("base_reflect_fraction")))
    EAL.delete_asset(p)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
