# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_charlit_regressed.py — ВОССОЗДАНИЕ регрессировавшего мастера для ЧИСТОГО замера.
#   Строит /Game/Temp/M_AsdaCharLit_REGRESSED — точную копию M_AsdaCharLit В ТОМ ВИДЕ, В КАКОМ
#   он вызвал жалобу («слишком чувствителен к свету, легко затемняется»):
#     OPAQUE, Default Lit, two-sided, BaseColor = Tex.RGB·VertexColor,
#     Roughness=0.9, Metallic=0, Specular = SpecScale(0.5)·Tex.A, Emissive НЕ подключён.
#   Нужен только пробнику (asda_probe_material_light.py): даёт колонку «регрессия» в таблице
#   яркостей, не трогая боевой мастер. В игре НЕ используется.
#   Запуск: run_headless.bat asda_build_charlit_regressed.py
import unreal

mel = unreal.MaterialEditingLibrary
atools = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
PKG = "/Game/Temp"
NAME = "M_AsdaCharLit_REGRESSED"
ROUGH = 0.9

p = "%s/%s" % (PKG, NAME)


def E(cls, x, y): return mel.create_material_expression(m, cls, x, y)
def C(a, ao, b, bi): mel.connect_material_expressions(a, ao, b, bi)
def P(e, eo, prop): mel.connect_material_property(e, eo, prop)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global m
    if EAL.does_asset_exist(p):
        EAL.delete_asset(p)
    m = atools.create_asset(NAME, PKG, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("used_with_skeletal_mesh", True)
    m.set_editor_property("used_with_instanced_static_meshes", True)
    tex = E(unreal.MaterialExpressionTextureSampleParameter2D, -700, -100)
    tex.set_editor_property("parameter_name", "Tex")
    vc = E(unreal.MaterialExpressionVertexColor, -700, 150)
    bc = E(unreal.MaterialExpressionMultiply, -300, 0); C(tex, "RGB", bc, "A"); C(vc, "RGB", bc, "B")
    rough = E(unreal.MaterialExpressionScalarParameter, -300, 200)
    rough.set_editor_property("parameter_name", "Roughness"); rough.set_editor_property("default_value", ROUGH)
    metal = E(unreal.MaterialExpressionScalarParameter, -300, 320)
    metal.set_editor_property("parameter_name", "Metallic"); metal.set_editor_property("default_value", 0.0)
    spec = E(unreal.MaterialExpressionScalarParameter, -700, 330)
    spec.set_editor_property("parameter_name", "SpecScale"); spec.set_editor_property("default_value", 0.5)
    sm = E(unreal.MaterialExpressionMultiply, -300, 430); C(spec, "", sm, "A"); C(tex, "A", sm, "B")
    P(bc, "", unreal.MaterialProperty.MP_BASE_COLOR)
    P(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    P(metal, "", unreal.MaterialProperty.MP_METALLIC)
    P(sm, "", unreal.MaterialProperty.MP_SPECULAR)      # ← Emissive НАМЕРЕННО не подключаем: это и есть регрессия
    mel.recompile_material(m)
    m.modify()
    unreal.log_warning("[regressed] %s save=%s" % (NAME, EAL.save_loaded_asset(m, False)))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
