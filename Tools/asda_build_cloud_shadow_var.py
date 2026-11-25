# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_cloud_shadow_var.py — ★ МАСТЕР ВАРИАНТОВ ТЕНЕЙ ОБЛАКОВ `M_AsdaCloudShadowLFVar` (волна cloud_shadows, 01.11.2026).
#
# ЗАЧЕМ. Владелец: тени облаков почти чёрные. Рабочий мастер `M_AsdaCloudShadowLF` (asda_add_cloud_shadows.py) даёт
#   свет = 1 − saturate(A·5)·1 — ядро облака гасит солнце ПОЛНОСТЬЮ, край резкий (A 0→0.2 = вся глубина). Этот мастер —
#   ОТДЕЛЬНЫЙ (рабочий не трогается), с параметрами для вариантов, которые выбирает ключ `asda.Clouds.ShadowStyle`
#   (`AAsdaCloudShadowsActor::Apply`, 0 = прежний мастер). Домен Light Function: выход СЕРЫЙ (LightFunctionPixelShader.usf:63
#   `dot(Color, .3333)`) — оттенок тени функцией света не задаётся.
#
# ГРАФ (по UV как у рабочего: WorldXY / TileSize + Time × (SpeedU, SpeedV)):
#   A      = CloudTex(UV).a                                          — маска облака
#   edge   = saturate((A − EdgeLo) / (EdgeHi − EdgeLo))              — ширина края
#   mask   = lerp(edge, edge²·(3 − 2·edge), Smooth)                  — мягкий (smoothstep) край при Smooth = 1
#   n      = saturate(CloudTex(UV·NoiseScale + (0.37, 0.61)).a · NoiseGain) — крупное «случайное» поле той же текстуры
#   dens   = lerp(DensMin, 1, n)                                     — плотность облака: DensMin = 1 → все одинаковы
#   свет   = 1 − mask · dens · Strength
#   Дефолты = картинка рабочего мастера (Strength 1, EdgeLo 0, EdgeHi 0.2, Smooth 0, DensMin 1): не заданный
#   параметр ничего не меняет.
#
# Запуск: python Tools/run_headless_safe.py Tools/asda_build_cloud_shadow_var.py --marker "CSV ЗАВЕРШЕНО"
# ★ Модуль без побочных эффектов при импорте (закон tools-no-import-side-effects).
import os as _os
import sys as _sys

import unreal

_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_mat_graph as _MG   # noqa: E402  чистка графа до пустого + контроль после сборки

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
MASTER = "/Game/Materials/M_AsdaCloudShadowLFVar"   # мастер вариантов
# Узлы build(): UV 10 (wp mxy tile dv tm su sv ap mult uv) + текстура 2 (tobj t1) + край 6 (elo ehi sub rng div sat) +
#   smoothstep 8 (x2 two k3 tx t3 sm smooth mask) + плотность 11 (nsc nmul noff nadd t2 ngain nm nsat dmin one dl) +
#   выход 4 (strength m1 m2 om) = 41.
EXPECTED = 41


def log(m):
    """Строка в журнал UE с меткой инструмента."""
    unreal.log("[CSV] %s" % m)


def build():
    """Собрать граф мастера вариантов (создать ассет, если его нет) и сохранить."""
    name = MASTER.rsplit("/", 1)[-1]
    if EAL.does_asset_exist(MASTER):
        m = EAL.load_asset(MASTER)
        _MG.clear_expressions(m, log=log, tag="[CSV]")
    else:
        m = AT.create_asset(name, MASTER.rsplit("/", 1)[0], unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("material_domain", unreal.MaterialDomain.MD_LIGHT_FUNCTION)

    def ex(cls, x, y):
        return MEL.create_material_expression(m, cls, x, y)

    def con(a, b, pin="", out=""):
        MEL.connect_material_expressions(a, out, b, pin)

    def sp(nm, v, x, y):
        return _MG.scalar_param(m, nm, v, x, y)

    def binop(cls, a, b, x, y, pa="A", pb="B"):
        n = ex(cls, x, y)
        con(a, n, pa)
        con(b, n, pb)
        return n

    # --- UV: как у рабочего мастера (код ставит SpeedU/V × множитель asda.Clouds.SlowSky) ---
    wp = ex(unreal.MaterialExpressionWorldPosition, -2000, 0)
    mxy = ex(unreal.MaterialExpressionComponentMask, -1850, 0)
    for c, on in (("r", True), ("g", True), ("b", False), ("a", False)):
        mxy.set_editor_property(c, on)
    con(wp, mxy)
    tile = sp("TileSize", 60000.0, -1850, 120)
    dv = binop(unreal.MaterialExpressionDivide, mxy, tile, -1700, 0)
    tm = ex(unreal.MaterialExpressionTime, -1850, 260)
    su = sp("SpeedU", 0.01, -1850, 340)
    sv = sp("SpeedV", 0.0, -1850, 420)
    ap = binop(unreal.MaterialExpressionAppendVector, su, sv, -1700, 360)
    mult = binop(unreal.MaterialExpressionMultiply, ap, tm, -1550, 300)
    uv = binop(unreal.MaterialExpressionAdd, dv, mult, -1400, 100)
    # --- одна текстура карты, два сэмпла (маска и поле плотности) ---
    tobj = ex(unreal.MaterialExpressionTextureObjectParameter, -1400, 500)
    tobj.set_editor_property("parameter_name", "CloudTex")
    t1 = ex(unreal.MaterialExpressionTextureSample, -1200, 60)
    con(uv, t1, "UVs")
    con(tobj, t1, "Tex")
    # --- край ---
    elo = sp("EdgeLo", 0.0, -1200, 260)
    ehi = sp("EdgeHi", 0.2, -1200, 340)
    sub = ex(unreal.MaterialExpressionSubtract, -1000, 100)
    MEL.connect_material_expressions(t1, "A", sub, "A")   # альфа cloude2 = маска облака
    con(elo, sub, "B")
    rng = binop(unreal.MaterialExpressionSubtract, ehi, elo, -1000, 300)
    div = binop(unreal.MaterialExpressionDivide, sub, rng, -850, 150)
    sat = ex(unreal.MaterialExpressionSaturate, -700, 150)
    con(div, sat)
    # --- smoothstep: x²(3 − 2x) ---
    x2 = binop(unreal.MaterialExpressionMultiply, sat, sat, -550, 220)
    two = ex(unreal.MaterialExpressionConstant, -700, 300)
    two.set_editor_property("r", -2.0)
    k3 = ex(unreal.MaterialExpressionConstant, -700, 360)
    k3.set_editor_property("r", 3.0)
    tx = binop(unreal.MaterialExpressionMultiply, sat, two, -550, 320)
    t3 = binop(unreal.MaterialExpressionAdd, tx, k3, -420, 320)
    sm = binop(unreal.MaterialExpressionMultiply, x2, t3, -300, 260)
    smooth = sp("Smooth", 0.0, -300, 380)
    mask = ex(unreal.MaterialExpressionLinearInterpolate, -150, 180)
    con(sat, mask, "A")
    con(sm, mask, "B")
    con(smooth, mask, "Alpha")
    # --- поле плотности ---
    nsc = sp("NoiseScale", 0.37, -1400, 640)
    nmul = binop(unreal.MaterialExpressionMultiply, uv, nsc, -1250, 620)
    noff = ex(unreal.MaterialExpressionConstant2Vector, -1400, 740)
    noff.set_editor_property("r", 0.37)
    noff.set_editor_property("g", 0.61)
    nadd = binop(unreal.MaterialExpressionAdd, nmul, noff, -1100, 640)
    t2 = ex(unreal.MaterialExpressionTextureSample, -950, 600)
    con(nadd, t2, "UVs")
    con(tobj, t2, "Tex")
    ngain = sp("NoiseGain", 3.0, -950, 800)
    nm = ex(unreal.MaterialExpressionMultiply, -750, 640)
    MEL.connect_material_expressions(t2, "A", nm, "A")
    con(ngain, nm, "B")
    nsat = ex(unreal.MaterialExpressionSaturate, -620, 640)
    con(nm, nsat)
    dmin = sp("DensMin", 1.0, -620, 760)
    one = ex(unreal.MaterialExpressionConstant, -620, 840)
    one.set_editor_property("r", 1.0)
    dl = ex(unreal.MaterialExpressionLinearInterpolate, -450, 680)
    con(dmin, dl, "A")
    con(one, dl, "B")
    con(nsat, dl, "Alpha")
    # --- свет = 1 − mask · dens · Strength ---
    strength = sp("Strength", 1.0, -150, 420)
    m1 = binop(unreal.MaterialExpressionMultiply, mask, dl, 0, 300)
    m2 = binop(unreal.MaterialExpressionMultiply, m1, strength, 120, 300)
    om = ex(unreal.MaterialExpressionOneMinus, 250, 300)
    con(m2, om)
    MEL.connect_material_property(om, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    _MG.check_graph(m, expected=EXPECTED, log=log, tag="[CSV]")
    MEL.recompile_material(m)
    m.modify()
    EAL.save_loaded_asset(m)
    node = MEL.get_material_property_input_node(m, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    log("мастер %s: узлов %d, Emissive=%s" % (MASTER, _MG.expression_count(m), node.get_class().get_name() if node else "ОБРЫВ!"))


def main():
    """Работа инструмента (вызывается только при прямом запуске)."""
    try:
        build()
    except Exception as e:
        unreal.log_error("[CSV] ОШИБКА: %s" % e)
    log("CSV ЗАВЕРШЕНО")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
