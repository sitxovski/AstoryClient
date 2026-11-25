# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_effect_mod_masters.py — мастера для бленда D3D 6:4 (INVSRCALPHA:INVSRCCOLOR, «тёмный дым»:
#   eff_flammio-a_09/10, mdel_worker02-a_00/01; текстуры JPG без альфы → srcA = вершинная альфа).
# Оригинал (лоадер effects_particles.c:113269, srcC = Tex×diffuse, MODULATE ×1):
#   Final = srcC×(1−srcA) + dst×(1−srcC)
# Точная декомпозиция в UE (две секции, mod рисуется раньше add — секции PMC идут по индексу):
#   M_AsdaEffectMod    (BLEND_Modulate): Emissive = 1 − Tex.rgb×VC.rgb          → dst×(1−srcC)
#   M_AsdaEffectModAdd (BLEND_Additive): Emissive = Tex.rgb×VC.rgb×(1−VC.a)     → +srcC×(1−srcA)
# Оба Unlit, TwoSided (fx-проход оригинала: CULL_NONE, ZWRITE=0, Z-тест вкл).
import os, sys
import unreal
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_mat_graph as _MG   # ★ чистка графа до пустого + контроль после сборки (баг UE 5.8 DeleteAllMaterialExpressions)
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
DIR = "/Game/Effects"
# Ровно столько узлов строит build(): Modulate — tex, vc, m1, om = 4; Additive — tex, vc, m1, oa, m2 = 5.
#   При изменении графа — обновить.
EXPECTED_EXPRESSIONS = {True: 4, False: 5}

def build(name, modulate):
    path = "%s/%s" % (DIR, name)
    if EAL.does_asset_exist(path):
        # пересборка IN-PLACE (delete_asset ломал бы parent у существующих MIC_*_mod/modadd)
        mat = EAL.load_asset(path)
        # ★ В ЦИКЛЕ до пустого: один delete_all_material_expressions оставляет каждый второй узел
        #   (UE 5.8, MaterialEditingLibrary.cpp:559-568) → фантомы и дубли параметров в ассете.
        _MG.clear_expressions(mat, log=unreal.log, tag="[MOD-MASTER]")
    else:
        mat = ATOOLS.create_asset(name, DIR, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MODULATE if modulate else unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    def expr(cls, x, y): return MEL.create_material_expression(mat, cls, x, y)
    tex = expr(unreal.MaterialExpressionTextureSampleParameter2D, -700, 0)
    tex.set_editor_property("parameter_name", "SpriteTexture")     # имя как у Add/Alpha — MIC-путь общий
    vc  = expr(unreal.MaterialExpressionVertexColor, -700, 250)
    m1  = expr(unreal.MaterialExpressionMultiply, -450, 60)        # Tex.rgb × VC.rgb (= srcC)
    MEL.connect_material_expressions(tex, "RGB", m1, "A")
    MEL.connect_material_expressions(vc, "", m1, "B")
    # ВАЖНО: вход OneMinus называется НЕ "A" — пустая строка = дефолтный вход
    # (с "A" коннект молча не создавался → "(Node OneMinus) Missing 1-x input" → Default Material)
    if modulate:
        om = expr(unreal.MaterialExpressionOneMinus, -250, 60)     # 1 − srcC
        MEL.connect_material_expressions(m1, "", om, "")
        MEL.connect_material_property(om, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    else:
        oa = expr(unreal.MaterialExpressionOneMinus, -450, 300)    # 1 − VC.a (= 1 − srcA)
        MEL.connect_material_expressions(vc, "A", oa, "")
        m2 = expr(unreal.MaterialExpressionMultiply, -250, 120)    # srcC × (1 − srcA)
        MEL.connect_material_expressions(m1, "", m2, "A")
        MEL.connect_material_expressions(oa, "", m2, "B")
        MEL.connect_material_property(m2, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    # ★ Контроль графа ДО сохранения (число узлов + без дублей имён параметров); иначе RuntimeError.
    _MG.check_graph(mat, expected=EXPECTED_EXPRESSIONS[modulate], log=unreal.log, tag="[MOD-MASTER]")
    # ★ 11.10.2026 (fx_alphatest.md): альфа-тест прохода эффектов эталона (ALPHAREF 8, GE) — цепь поверх графа.
    import asda_fx_alphatest as AT
    AT.apply_in_builder(mat, name, unreal.log)
    MEL.recompile_material(mat)
    mat.modify()
    ok = EAL.save_loaded_asset(mat)
    unreal.log("[MOD-MASTER] %s: blend=%s save=%s" % (name, "Modulate" if modulate else "Additive", ok))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    build("M_AsdaEffectMod", True)
    build("M_AsdaEffectModAdd", False)
    unreal.log("=== MOD MASTERS DONE ===")
    # — верификация связности: вход Emissive должен быть подключён (обрыв тут = Default Material в игре)
    for nm in ("M_AsdaEffectMod", "M_AsdaEffectModAdd"):
        m = EAL.load_asset("%s/%s" % (DIR, nm))
        node = MEL.get_material_property_input_node(m, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        unreal.log("[MOD-VERIFY] %s: EmissiveInput=%s" % (nm, node.get_class().get_name() if node else "ОБРЫВ!"))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
