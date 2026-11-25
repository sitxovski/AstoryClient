# Copyright (c) 2026 @sitxovski (Discord)
# M_AsdaPreviewFade — ВРЕМЕННЫЙ материал фейд-ина превью char-select (честный translucent-фейд).
#   Изолирован: общий M_AsdaObjectLit НЕ трогаем (дизер в общей маске ломал весь мир — грабля 2026-07-23).
#   Params: Tex (текстура части), FadeIn (0..1). BaseColor=tex, Emissive=tex·AmbientFloor (0 с 2026-09-03, Д-3), Opacity=tex.A·FadeIn.
#   Рантайм: CollectPreviewFadeComps свапает материалы превью на MID отсюда на ~1с и возвращает исходные.
import unreal
mel = unreal.MaterialEditingLibrary
atools = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
PKG = "/Game/Materials"; NAME = "M_AsdaPreviewFade"
p = "%s/%s" % (PKG, NAME)
def E(cls, x, y): return mel.create_material_expression(m, cls, x, y)
def C(a, ao, b, bi): mel.connect_material_expressions(a, ao, b, bi)
def P(e, eo, prop): mel.connect_material_property(e, eo, prop)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global m
    if EAL.does_asset_exist(p): EAL.delete_asset(p)
    m = atools.create_asset(NAME, PKG, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    m.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("used_with_skeletal_mesh", True)
    tex = E(unreal.MaterialExpressionTextureSampleParameter2D, -600, -100); tex.set_editor_property("parameter_name", "Tex")
    floor_ = E(unreal.MaterialExpressionScalarParameter, -600, 250); floor_.set_editor_property("parameter_name", "AmbientFloor"); floor_.set_editor_property("default_value", 0.0)   # Д-3 2026-09-03: пол 0 как у всех Lit-мастеров (узел-рычаг)
    emis = E(unreal.MaterialExpressionMultiply, -300, 200); C(tex, "RGB", emis, "A"); C(floor_, "", emis, "B")
    fade = E(unreal.MaterialExpressionScalarParameter, -600, 420); fade.set_editor_property("parameter_name", "FadeIn"); fade.set_editor_property("default_value", 1.0)
    op = E(unreal.MaterialExpressionMultiply, -300, 400); C(tex, "A", op, "A"); C(fade, "", op, "B")
    P(tex, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    P(emis, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    P(op, "", unreal.MaterialProperty.MP_OPACITY)
    m.modify()
    ok = EAL.save_loaded_asset(m)
    unreal.log("[PF] M_AsdaPreviewFade save=%s" % ok)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
