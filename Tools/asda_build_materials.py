# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_materials.py — строит мастер-материал террейна M_AsdaTerrain (UNLIT splat, реплика ps.1.1 игры).
# Запуск в редакторе: Tools -> Execute Python Script.
# Шейдер (из MapViewer, точная реплика AsdaStory.exe ps.1.1):
#   t0=detail0(uv*scale0).rgb ; t1=detail1(uv*scale1).rgb ; mask=TerrainAlpha(uv) ; cmap=TerrainColor(uv)
#   g = lerp(t0, t1, mask.a*HasD1) ; g = lerp(g, cmap.rgb, cmap.a) ; g *= vertexColor.rgb ; g *= mask.b
#   Emissive = g (UNLIT — всё запечено: colormap/AO/vertex-color).
# Контракт параметров (C++ MID биндит по этим именам):
#   Текстуры: Detail0, Detail1, Mask, Colormap   Скаляры: Scale0, Scale1, HasD1
import unreal

PKG = "/Game/Materials"
NAME = "M_AsdaTerrain"
mel = unreal.MaterialEditingLibrary
atools = unreal.AssetToolsHelpers.get_asset_tools()

def make():
    path = "%s/%s" % (PKG, NAME)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    mat = atools.create_asset(NAME, PKG, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)

    def expr(cls, x, y): return mel.create_material_expression(mat, cls, x, y)
    def texparam(nm, x, y, default_white=True):
        t = expr(unreal.MaterialExpressionTextureSampleParameter2D, x, y)
        t.set_editor_property("parameter_name", nm)
        return t
    def scalar(nm, val, x, y):
        s = expr(unreal.MaterialExpressionScalarParameter, x, y)
        s.set_editor_property("parameter_name", nm); s.set_editor_property("default_value", val)
        return s

    uv   = expr(unreal.MaterialExpressionTextureCoordinate, -1100, 0)
    s0   = scalar("Scale0", 8.0, -1100, 200)
    s1   = scalar("Scale1", 8.0, -1100, 320)
    hasD1= scalar("HasD1", 0.0, -1100, 440)

    uv0  = expr(unreal.MaterialExpressionMultiply, -900, 100)   # uv*scale0
    mel.connect_material_expressions(uv, "", uv0, "A"); mel.connect_material_expressions(s0, "", uv0, "B")
    uv1  = expr(unreal.MaterialExpressionMultiply, -900, 250)   # uv*scale1
    mel.connect_material_expressions(uv, "", uv1, "A"); mel.connect_material_expressions(s1, "", uv1, "B")

    d0 = texparam("Detail0", -650, -300); mel.connect_material_expressions(uv0, "", d0, "UVs")
    d1 = texparam("Detail1", -650,  -50); mel.connect_material_expressions(uv1, "", d1, "UVs")
    mk = texparam("Mask",    -650,  220); mel.connect_material_expressions(uv, "", mk, "UVs")
    cm = texparam("Colormap",-650,  480); mel.connect_material_expressions(uv, "", cm, "UVs")

    # w = mask.a * HasD1
    w = expr(unreal.MaterialExpressionMultiply, -350, 250)
    mel.connect_material_expressions(mk, "A", w, "A"); mel.connect_material_expressions(hasD1, "", w, "B")
    # g = lerp(d0.rgb, d1.rgb, w)
    g1 = expr(unreal.MaterialExpressionLinearInterpolate, -150, -100)
    mel.connect_material_expressions(d0, "RGB", g1, "A"); mel.connect_material_expressions(d1, "RGB", g1, "B"); mel.connect_material_expressions(w, "", g1, "Alpha")
    # g = lerp(g, cmap.rgb, cmap.a)
    g2 = expr(unreal.MaterialExpressionLinearInterpolate, 80, 50)
    mel.connect_material_expressions(g1, "", g2, "A"); mel.connect_material_expressions(cm, "RGB", g2, "B"); mel.connect_material_expressions(cm, "A", g2, "Alpha")
    # g *= vertexColor.rgb
    vc = expr(unreal.MaterialExpressionVertexColor, 80, 300)
    g3 = expr(unreal.MaterialExpressionMultiply, 320, 100)
    mel.connect_material_expressions(g2, "", g3, "A"); mel.connect_material_expressions(vc, "RGB", g3, "B")
    # g *= mask.b (AO)
    g4 = expr(unreal.MaterialExpressionMultiply, 520, 150)
    mel.connect_material_expressions(g3, "", g4, "A"); mel.connect_material_expressions(mk, "B", g4, "B")

    # ★ per-map ЛАЙТИНГ (раньше террейн был чистый запечённый Emissive = всегда полная яркость).
    #   lite = Ambient + LightColor*max(NdotL,0); emissive *= lite. Параметры per-map (set_map_lighting).
    def vecparam(nm, r, g, b, x, y):
        v = expr(unreal.MaterialExpressionVectorParameter, x, y)
        v.set_editor_property("parameter_name", nm)
        v.set_editor_property("default_value", unreal.LinearColor(r, g, b, 0.0)); return v
    ambT = scalar("Ambient", 0.6, 760, 400)
    lcolT= vecparam("LightColor", 0.8, 0.8, 0.8, 760, 500)
    ldT  = vecparam("LightDir", 0.0, 0.0, -1.0, 760, 620)
    nrmT = expr(unreal.MaterialExpressionVertexNormalWS, 760, 300)
    negcT= expr(unreal.MaterialExpressionConstant, 760, 700); negcT.set_editor_property("r", -1.0)
    negLdT = expr(unreal.MaterialExpressionMultiply, 960, 620)
    mel.connect_material_expressions(ldT, "RGB", negLdT, "A"); mel.connect_material_expressions(negcT, "", negLdT, "B")
    ndlT = expr(unreal.MaterialExpressionDotProduct, 960, 400)
    mel.connect_material_expressions(nrmT, "", ndlT, "A"); mel.connect_material_expressions(negLdT, "", ndlT, "B")
    z0T  = expr(unreal.MaterialExpressionConstant, 960, 320); z0T.set_editor_property("r", 0.0)
    ndlcT= expr(unreal.MaterialExpressionMax, 1120, 400)
    mel.connect_material_expressions(ndlT, "", ndlcT, "A"); mel.connect_material_expressions(z0T, "", ndlcT, "B")
    diffT= expr(unreal.MaterialExpressionMultiply, 1120, 500)
    mel.connect_material_expressions(lcolT, "RGB", diffT, "A"); mel.connect_material_expressions(ndlcT, "", diffT, "B")
    ambCT= expr(unreal.MaterialExpressionMultiply, 1120, 620)   # ★ ambient ТИНТУЕТСЯ цветом карты (иначе плоский террейн серый)
    mel.connect_material_expressions(lcolT, "RGB", ambCT, "A"); mel.connect_material_expressions(ambT, "", ambCT, "B")
    liteT= expr(unreal.MaterialExpressionAdd, 1300, 450)         # lite = LightColor*Ambient + LightColor*NdotL
    mel.connect_material_expressions(ambCT, "", liteT, "A"); mel.connect_material_expressions(diffT, "", liteT, "B")
    litE = expr(unreal.MaterialExpressionMultiply, 1480, 200)
    mel.connect_material_expressions(g4, "", litE, "A"); mel.connect_material_expressions(liteT, "", litE, "B")

    mel.connect_material_property(litE, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset("%s/%s" % (PKG, NAME))
    unreal.log("M_AsdaTerrain создан: %s/%s" % (PKG, NAME))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    make()


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
