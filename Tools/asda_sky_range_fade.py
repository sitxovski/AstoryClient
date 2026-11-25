# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_sky_range_fade.py — точечно добавить растворение дальних силуэтов в M_AsdaSkyDome и M_AsdaSkyHorizon (пачка 14, 07.11.2026).
#   Создаёт MPC_AsdaViewRange (FadeStart, FadeEnd: по умолчанию «ничего не растворять») и дописывает узлы к входу Opacity живых мастеров;
#   остальной граф не пересобирается. Цепочка и разбор — asda_build_sky_dome.append_range_fade. Повторный запуск ничего не меняет.
#   Запуск: python Tools/run_headless_safe.py Tools/asda_sky_range_fade.py --marker "ФЕЙД ЗАВЕРШЕНО"
# ★ Модуль без побочных эффектов при импорте (закон tools-no-import-side-effects).
import os as _os
import sys as _sys

import unreal

_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_build_sky_dome as _SKY   # noqa: E402  общая цепочка и пути мастеров
import asda_mat_graph as _MG         # noqa: E402  контроль графа

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MPC_SCALARS = (("FadeStart", 1.0e9), ("FadeEnd", 1.001e9))   # значения совпадают с kSkyFadeOff* в AsdaViewRangeSubsystem.cpp


def log(m):
    """Строка журнала UE с меткой инструмента."""
    unreal.log("[ФЕЙД] %s" % m)


def ensure_mpc():
    """MPC_AsdaViewRange со скалярами MPC_SCALARS."""
    path = _SKY.RANGE_FADE_MPC
    if EAL.does_asset_exist(path):
        mpc = EAL.load_asset(path)
    else:
        mpc = unreal.AssetToolsHelpers.get_asset_tools().create_asset(path.rsplit("/", 1)[-1], path.rsplit("/", 1)[0],
                                                                      unreal.MaterialParameterCollection,
                                                                      unreal.MaterialParameterCollectionFactoryNew())
    arr = list(mpc.get_editor_property("scalar_parameters"))
    have = {str(p.get_editor_property("parameter_name")) for p in arr}
    for name, val in MPC_SCALARS:
        if name not in have:
            sp = unreal.CollectionScalarParameter()   # конструктор структуры сам выдаёт Id
            sp.set_editor_property("parameter_name", name)
            sp.set_editor_property("default_value", val)
            arr.append(sp)
    mpc.set_editor_property("scalar_parameters", arr)
    mpc.modify()
    log("MPC %s сохранён=%s" % (path, EAL.save_loaded_asset(mpc)))


def patch(path, expected):
    """Дописать цепочку к входу Opacity мастера path; expected — узлов после правки."""
    m = EAL.load_asset(path)
    nodes = MEL.get_material_expressions(m)
    if any(isinstance(e, unreal.MaterialExpressionPixelDepth) for e in nodes):
        log("%s: цепочка уже есть" % path)
        return
    if any(isinstance(e, unreal.MaterialExpressionSceneTexture) for e in nodes):
        extend_radial(m, path, nodes, expected)
        return
    if any(isinstance(e, unreal.MaterialExpressionCollectionParameter) for e in nodes):
        extend_only_sky(m, path, nodes, expected)
        return
    base = MEL.get_material_property_input_node(m, unreal.MaterialProperty.MP_OPACITY)
    if not base:
        raise RuntimeError("%s: вход Opacity пуст" % path)

    def expr(cls, x, y):
        return MEL.create_material_expression(m, cls, x, y)

    out = _SKY.append_range_fade(m, expr, base, "")
    if not MEL.connect_material_property(out, "", unreal.MaterialProperty.MP_OPACITY):
        raise RuntimeError("%s: Opacity не подключился" % path)
    _MG.check_graph(m, expected=expected, log=log, tag="[ФЕЙД]")
    MEL.recompile_material(m)
    m.modify()
    log("%s: узлов %d, сохранён=%s" % (path, _MG.expression_count(m), EAL.save_loaded_asset(m)))


def extend_radial(m, path, nodes, expected):
    """Вторая версия цепочки уже в ассете: начало растворения считать по расстоянию, а не по глубине."""
    s1 = None
    for e in nodes:
        if isinstance(e, unreal.MaterialExpressionSubtract):
            ins = MEL.get_inputs_for_material_expression(m, e)
            if len(ins) > 1 and isinstance(ins[0], unreal.MaterialExpressionSceneDepth) and isinstance(ins[1], unreal.MaterialExpressionCollectionParameter):
                s1, sd = e, ins[0]
    if not s1:
        raise RuntimeError("%s: нет узла глубины минус начало" % path)

    def expr(cls, x, y):
        return MEL.create_material_expression(m, cls, x, y)

    def op(cls, a, b, x, y):
        e = expr(cls, x, y)
        MEL.connect_material_expressions(a, "", e, "A")
        MEL.connect_material_expressions(b, "", e, "B")
        return e

    MEL.connect_material_expressions(_SKY.range_fade_radial(expr, op, sd), "", s1, "A")
    _MG.check_graph(m, expected=expected, log=log, tag="[ФЕЙД]")
    MEL.recompile_material(m)
    m.modify()
    log("%s: узлов %d, сохранён=%s" % (path, _MG.expression_count(m), EAL.save_loaded_asset(m)))


def extend_only_sky(m, path, nodes, expected):
    """Первая версия цепочки уже в ассете: вставить проверку рельефа перед узлом Max."""
    mx = [e for e in nodes if isinstance(e, unreal.MaterialExpressionMax)][0]
    fade, sky = MEL.get_inputs_for_material_expression(m, mx)[:2]

    def expr(cls, x, y):
        return MEL.create_material_expression(m, cls, x, y)

    def const(v, x, y):
        e = expr(unreal.MaterialExpressionConstant, x, y)
        e.set_editor_property("r", v)
        return e

    def op(cls, a, b, x, y):
        e = expr(cls, x, y)
        MEL.connect_material_expressions(a, "", e, "A")
        MEL.connect_material_expressions(b, "", e, "B")
        return e

    MEL.connect_material_expressions(_SKY.range_fade_only_sky(expr, op, const, fade), "", mx, "A")
    _MG.check_graph(m, expected=expected, log=log, tag="[ФЕЙД]")
    MEL.recompile_material(m)
    m.modify()
    log("%s: узлов %d, сохранён=%s" % (path, _MG.expression_count(m), EAL.save_loaded_asset(m)))


def main():
    """Работа инструмента (только при прямом запуске)."""
    try:
        ensure_mpc()
        patch(_SKY.MASTER, _SKY.MASTER_EXPRESSIONS)
        patch(_SKY.MASTER_HORIZON, _SKY.MASTER_HORIZON_EXPRESSIONS)
    except Exception as e:
        unreal.log_error("[ФЕЙД] ОШИБКА: %s" % e)
    log("ФЕЙД ЗАВЕРШЕНО")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
