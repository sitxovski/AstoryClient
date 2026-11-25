# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_materials2.py — мастер-материалы объектов/воды/неба (реплики шейдеров AsdaStory из MapViewer).
# Запуск: Tools -> Execute Python Script.
#  M_AsdaObject — vs.1.1 BLOCK-7: tex*(ambient + coolwhite*ndl)*vtxColor, alpha-test 0.5 (Masked, Unlit).
#  M_AsdaWater  — translucent: base tint/tex + fresnel-отражение, анимированная дисторсия (Translucent, Unlit).
#  M_AsdaSky    — unlit emissive = sky tex.
# Контракты параметров (C++ MID): Object{Tex,LightDir,Ambient} Water{BaseTex,HasBase} Sky{SkyTex}.
import unreal

PKG = "/Game/Materials"
mel = unreal.MaterialEditingLibrary
atools = unreal.AssetToolsHelpers.get_asset_tools()
LC = unreal.LinearColor

def fresh(name):
    p = "%s/%s" % (PKG, name)
    if unreal.EditorAssetLibrary.does_asset_exist(p):
        unreal.EditorAssetLibrary.delete_asset(p)
    return atools.create_asset(name, PKG, unreal.Material, unreal.MaterialFactoryNew())

def E(mat, cls, x, y): return mel.create_material_expression(mat, cls, x, y)
def C(a, ao, b, bi): mel.connect_material_expressions(a, ao, b, bi)
def P(mat, a, ao, prop): mel.connect_material_property(a, ao, prop)
def texparam(mat, nm, x, y):
    t = E(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y); t.set_editor_property("parameter_name", nm); return t
def scalar(mat, nm, v, x, y):
    s = E(mat, unreal.MaterialExpressionScalarParameter, x, y); s.set_editor_property("parameter_name", nm); s.set_editor_property("default_value", v); return s
def const3(mat, col, x, y):
    c = E(mat, unreal.MaterialExpressionConstant3Vector, x, y); c.set_editor_property("constant", col); return c
def save(name): unreal.EditorAssetLibrary.save_asset("%s/%s" % (PKG, name))

# ---------- M_AsdaObject ----------
def build_object():
    m = fresh("M_AsdaObject")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("opacity_mask_clip_value", 0.5)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("used_with_skeletal_mesh", True)   # ★ иначе персонаж (скелетал) = серый дефолт-материал
    m.set_editor_property("used_with_instanced_static_meshes", True)   # ★ HISM-объекты карт: без флага в -game ЧЁРНЫЙ фолбэк (в редакторе авто-usage маскирует)
    tex   = texparam(m, "Tex", -800, -200)
    amb   = scalar(m, "Ambient", 0.8, -800, 200)
    ld    = E(m, unreal.MaterialExpressionVectorParameter, -800, 320); ld.set_editor_property("parameter_name", "LightDir"); ld.set_editor_property("default_value", LC(0,0,-1,0))
    nrm   = E(m, unreal.MaterialExpressionVertexNormalWS, -800, 60)
    negLd = E(m, unreal.MaterialExpressionMultiply, -560, 320)  # -LightDir
    negc  = E(m, unreal.MaterialExpressionConstant, -700, 420); negc.set_editor_property("r", -1.0)
    C(ld, "RGB", negLd, "A"); C(negc, "", negLd, "B")
    ndl   = E(m, unreal.MaterialExpressionDotProduct, -360, 180)
    C(nrm, "", ndl, "A"); C(negLd, "", ndl, "B")
    ndlc  = E(m, unreal.MaterialExpressionMax, -200, 180)          # max(ndl,0)
    z0    = E(m, unreal.MaterialExpressionConstant, -360, 280); z0.set_editor_property("r", 0.0)
    C(ndl, "", ndlc, "A"); C(z0, "", ndlc, "B")
    # ★ directional-цвет = per-map ПАРАМЕТР "LightColor" (был константой 0.80,0.99,1.00 = холодно-белый →
    #   цвет карты игнорился). Дефолт = эталонный Diffuse (0.8). set_map_lighting ставит per-map (scene.ambient).
    cool  = E(m, unreal.MaterialExpressionVectorParameter, -200, 300)
    cool.set_editor_property("parameter_name", "LightColor"); cool.set_editor_property("default_value", LC(0.8, 0.8, 0.8, 0.0))
    diff  = E(m, unreal.MaterialExpressionMultiply, 0, 220); C(cool, "RGB", diff, "A"); C(ndlc, "", diff, "B")
    ambc  = E(m, unreal.MaterialExpressionMultiply, 0, 120); C(cool, "RGB", ambc, "A"); C(amb, "", ambc, "B")  # ★ ambient тинтуется LightColor
    lite  = E(m, unreal.MaterialExpressionAdd, 180, 200); C(ambc, "", lite, "A"); C(diff, "", lite, "B")
    vc    = E(m, unreal.MaterialExpressionVertexColor, 180, 360)
    t_l   = E(m, unreal.MaterialExpressionMultiply, 360, 0); C(tex, "RGB", t_l, "A"); C(lite, "", t_l, "B")
    emis  = E(m, unreal.MaterialExpressionMultiply, 540, 40); C(t_l, "", emis, "A"); C(vc, "RGB", emis, "B")
    P(m, emis, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    P(m, tex, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
    mel.recompile_material(m); save("M_AsdaObject"); unreal.log("M_AsdaObject OK")

# ---------- M_AsdaWater ----------
def build_water():
    m = fresh("M_AsdaWater")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)
    # ★ Эталон: base = tex(fragW.xz * 0.0012 + d). Семплим по МИРОВОМУ планарному UV (а не UV меша —
    # у воды UV вырождены → семпл одного белого текселя = «белая вода»). Вода горизонтальна (UE Z-up),
    # поэтому планарный UV = WorldPosition.XY * 0.0012, с анимацией через Panner.
    wp    = E(m, unreal.MaterialExpressionWorldPosition, -1100, -300)
    wpxy  = E(m, unreal.MaterialExpressionComponentMask, -900, -300)
    wpxy.set_editor_property("r", True); wpxy.set_editor_property("g", True); wpxy.set_editor_property("b", False); wpxy.set_editor_property("a", False)
    C(wp, "", wpxy, "")
    uvsc  = E(m, unreal.MaterialExpressionMultiply, -740, -300); C(wpxy, "", uvsc, "A")
    uvk   = E(m, unreal.MaterialExpressionConstant, -900, -180); uvk.set_editor_property("r", 0.0012); C(uvk, "", uvsc, "B")
    pan   = E(m, unreal.MaterialExpressionPanner, -560, -300); pan.set_editor_property("speed_x", 0.004); pan.set_editor_property("speed_y", 0.003)
    C(uvsc, "", pan, "Coordinate")
    base  = texparam(m, "BaseTex", -380, -340); C(pan, "", base, "UVs")
    tint  = const3(m, LC(0.55, 0.72, 0.85), -380, -80)
    btex  = E(m, unreal.MaterialExpressionMultiply, -160, -240); C(base, "RGB", btex, "A"); C(tint, "", btex, "B")
    solid = const3(m, LC(0.16, 0.30, 0.40), -160, 60)
    hasb  = scalar(m, "HasBase", 0.0, -160, 180)
    basec = E(m, unreal.MaterialExpressionLinearInterpolate, 60, -60); C(solid, "", basec, "A"); C(btex, "", basec, "B"); C(hasb, "", basec, "Alpha")
    # Отражение-приближение неба (без planar-RT). Приглушено, чтобы не пересвечивать на скользящих углах.
    fres  = E(m, unreal.MaterialExpressionFresnel, 60, 240); fres.set_editor_property("exponent", 4.0); fres.set_editor_property("base_reflect_fraction", 0.12)
    fresk = E(m, unreal.MaterialExpressionMultiply, 220, 280); C(fres, "", fresk, "A")
    fkk   = E(m, unreal.MaterialExpressionConstant, 60, 360); fkk.set_editor_property("r", 0.55); C(fkk, "", fresk, "B")  # потолок отражения ~0.55
    refl  = const3(m, LC(0.42, 0.56, 0.72), 60, 470)            # приглушённое небо (было 0.62,0.76,0.92 — пересвет)
    col   = E(m, unreal.MaterialExpressionLinearInterpolate, 400, 60); C(basec, "", col, "A"); C(refl, "", col, "B"); C(fresk, "", col, "Alpha")
    op    = E(m, unreal.MaterialExpressionConstant, 400, 320); op.set_editor_property("r", 0.80)
    P(m, col, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    P(m, op, "", unreal.MaterialProperty.MP_OPACITY)
    mel.recompile_material(m); save("M_AsdaWater"); unreal.log("M_AsdaWater OK")

# ---------- M_AsdaSky (фишай cloud-cap + azimuth mountain-band, проекция по направлению из камеры) ----------
def build_sky():
    m = fresh("M_AsdaSky")
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)

    def mask(src, r, g, b):
        cm = E(m, unreal.MaterialExpressionComponentMask, 0, 0)
        cm.set_editor_property("r", r); cm.set_editor_property("g", g); cm.set_editor_property("b", b); cm.set_editor_property("a", False)
        C(src, "", cm, ""); return cm
    def mul(a, b, ao="", bi="A"):  # generic 2-input
        n = E(m, unreal.MaterialExpressionMultiply, 0, 0); C(a, ao, n, "A"); C(b, "", n, "B"); return n
    def add(a, b):
        n = E(m, unreal.MaterialExpressionAdd, 0, 0); C(a, "", n, "A"); C(b, "", n, "B"); return n
    def sub(a, b):
        n = E(m, unreal.MaterialExpressionSubtract, 0, 0); C(a, "", n, "A"); C(b, "", n, "B"); return n
    def cst(v):
        n = E(m, unreal.MaterialExpressionConstant, 0, 0); n.set_editor_property("r", v); return n
    def sat(x):  # saturate(x) = min(max(x,0),1) — без Clamp-ноды (у неё вход не подключался)
        mx = E(m, unreal.MaterialExpressionMax, 0, 0); C(x, "", mx, "A"); C(cst(0.0), "", mx, "B")
        mn = E(m, unreal.MaterialExpressionMin, 0, 0); C(mx, "", mn, "A"); C(cst(1.0), "", mn, "B")
        return mn

    wpos = E(m, unreal.MaterialExpressionWorldPosition, -1400, -200)
    cpos = E(m, unreal.MaterialExpressionCameraPositionWS, -1400, 0)
    diff = sub(wpos, cpos)
    ndir = E(m, unreal.MaterialExpressionNormalize, -1200, -100); C(diff, "", ndir, "")
    dx = mask(ndir, True, False, False)
    dy = mask(ndir, False, True, False)
    dz = mask(ndir, False, False, True)
    horRG = mask(ndir, True, True, False)

    skyScale = scalar(m, "SkyScale", 1.0, -1200, 200)
    raise_p  = scalar(m, "Raise", -0.05, -1200, 320)
    stretch  = scalar(m, "Stretch", 0.35, -1200, 440)

    # cloud cap: up=clamp(dz,0,1); r=(1-up)*SkyScale; hor=normalize(dx,dy); cuv=0.5+0.5*r*hor
    up = sat(dz)
    oneMinus = E(m, unreal.MaterialExpressionOneMinus, -850, 0); C(up, "", oneMinus, "")
    rr = mul(oneMinus, skyScale)
    hor = E(m, unreal.MaterialExpressionNormalize, -850, 150); C(horRG, "", hor, "")
    halfr = mul(rr, cst(0.5))
    cuvoff = E(m, unreal.MaterialExpressionMultiply, -650, 100); C(hor, "", cuvoff, "A"); C(halfr, "", cuvoff, "B")
    half2 = E(m, unreal.MaterialExpressionConstant2Vector, -650, 220); half2.set_editor_property("r", 0.5); half2.set_editor_property("g", 0.5)
    cuv = E(m, unreal.MaterialExpressionAdd, -480, 120); C(half2, "", cuv, "A"); C(cuvoff, "", cuv, "B")
    cloud = texparam(m, "CloudTex", -300, -150); C(cuv, "", cloud, "UVs")

    # mountain band: u=atan2(dy,dx)/2pi+0.5 ; vB=clamp((Raise+Stretch-dz)/Stretch,0,1)
    at2 = E(m, unreal.MaterialExpressionArctangent2, -1000, 320); C(dy, "", at2, "Y"); C(dx, "", at2, "X")
    uu = add(mul(at2, cst(0.15915494)), cst(0.5))
    rs = add(raise_p, stretch)
    num = sub(rs, dz)
    den = E(m, unreal.MaterialExpressionMax, -1000, 520); C(stretch, "", den, "A"); C(cst(0.001), "", den, "B")
    vbraw = E(m, unreal.MaterialExpressionDivide, -800, 440); C(num, "", vbraw, "A"); C(den, "", vbraw, "B")
    vb = sat(vbraw)
    mntuv = E(m, unreal.MaterialExpressionAppendVector, -480, 360); C(uu, "", mntuv, "A"); C(vb, "", mntuv, "B")
    mtn = texparam(m, "MountainTex", -300, 320); C(mntuv, "", mtn, "UVs")

    col = E(m, unreal.MaterialExpressionLinearInterpolate, 0, 0)
    C(cloud, "RGB", col, "A"); C(mtn, "RGB", col, "B"); C(mtn, "A", col, "Alpha")
    P(m, col, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m); save("M_AsdaSky"); unreal.log("M_AsdaSky (фишай) OK")


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    build_object()
    build_water()
    build_sky()
    unreal.log("Материалы объектов/воды/неба готовы.")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
