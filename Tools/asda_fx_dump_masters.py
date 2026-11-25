# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_dump_masters.py — ПОЛНЫЙ структурный дамп четырёх мастер-материалов эффектов
#   (M_AsdaEffectAdd / Alpha / Mod / ModAdd) ДО и ПОСЛЕ правки (закон full-structural-analysis-always).
#   Печатает: режим бленда, модель затенения, число выражений, список всех узлов с их параметрами,
#   и рекурсивную цепь каждого подключённого входа материала. Ничего не меняет и не сохраняет.
# Запуск: python Tools/run_headless_safe.py Tools/asda_fx_dump_masters.py --marker "FX-DUMP ЗАВЕРШЕНО"
import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

# Мастера эффектов в порядке «сначала самостоятельные, потом пара D3D 6:4».
MASTERS = ("/Game/Effects/M_AsdaEffectAdd", "/Game/Effects/M_AsdaEffectAlpha",
           "/Game/Effects/M_AsdaEffectMod", "/Game/Effects/M_AsdaEffectModAdd")
# Входы материала, которые вообще может использовать наш unlit-конвейер эффектов.
PROPS = (("MP_EMISSIVE_COLOR", unreal.MaterialProperty.MP_EMISSIVE_COLOR),
         ("MP_OPACITY", unreal.MaterialProperty.MP_OPACITY),
         ("MP_BASE_COLOR", unreal.MaterialProperty.MP_BASE_COLOR),
         ("MP_OPACITY_MASK", unreal.MaterialProperty.MP_OPACITY_MASK))
# Имена входных пинов, по которым идём вглубь при рекурсивном обходе цепи.
INPUT_PINS = ("a", "b", "input", "coordinates", "alpha", "value", "min", "max")


def describe(expr):
    """Короткое описание узла: класс + значимые свойства (имя параметра, дефолт)."""
    if expr is None:
        return "НЕТ"
    cls = type(expr).__name__
    bits = []
    for prop in ("parameter_name", "default_value", "r", "value", "const_a", "const_b"):
        try:
            v = expr.get_editor_property(prop)
        except Exception:
            continue
        bits.append("%s=%s" % (prop, v))
    return cls + ((" [" + ", ".join(bits) + "]") if bits else "")


def walk(expr, depth, seen):
    """Рекурсивная печать цепи узлов (защита от циклов и от бесконечной глубины)."""
    if expr is None or depth > 8:
        return
    key = expr.get_name()
    unreal.log("%s%s" % ("    " * depth, describe(expr)))
    if key in seen:
        unreal.log("%s(уже показан выше)" % ("    " * (depth + 1)))
        return
    seen.add(key)
    for pin in INPUT_PINS:
        try:
            inp = expr.get_editor_property(pin)
        except Exception:
            continue
        sub = None
        try:
            sub = inp.get_editor_property("expression") if inp else None
        except Exception:
            sub = None
        if sub:
            unreal.log("%s.%s:" % ("    " * (depth + 1), pin))
            walk(sub, depth + 2, seen)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон tools-no-import-side-effects)."""
    for path in MASTERS:
        m = EAL.load_asset(path)
        if not m:
            unreal.log_warning("[FX-DUMP] нет ассета %s" % path)
            continue
        exprs = MEL.get_material_expressions(m)
        unreal.log("")
        unreal.log("======== %s ========" % path)
        unreal.log("  blend=%s shading=%s two_sided=%s выражений=%d" % (
            m.get_editor_property("blend_mode"), m.get_editor_property("shading_model"),
            m.get_editor_property("two_sided"), len(exprs)))
        for e in exprs:
            unreal.log("  узел: %s" % describe(e))
        for nm, prop in PROPS:
            node = MEL.get_material_property_input_node(m, prop)
            if not node:
                continue
            try:
                out = str(MEL.get_material_property_input_node_output_name(m, prop))
            except Exception:
                out = "?"
            unreal.log("  -- вход %s (выход узла %r):" % (nm, out))
            walk(node, 2, set())
    unreal.log("FX-DUMP ЗАВЕРШЕНО")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
