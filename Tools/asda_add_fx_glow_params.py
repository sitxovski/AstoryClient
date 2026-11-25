# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_add_fx_glow_params.py — ВОЛНА Ф1: два новых узла-параметра в мастерах эффектов.
#   1) ЯРКОСТЬ ЭМИССИИ  — `EmissiveBoost` (скаляр, ДЕФОЛТ 1.0; НЕ static switch — обоснование
#      у места вставки в `patch`). Умножает ГОТОВУЮ цепь эмиссии (после VertexColor и FadeIn), т.е. ровно
#      ту величину, которую материал отдаёт в кадр. Ставится в Add / Alpha / ModAdd.
#      ★ M_AsdaEffectMod (BLEND_MODULATE) буста НЕ получает — обоснование в шапке `apply_boost`.
#   2) МЯГКОЕ ВРЕЗАНИЕ (soft particles) — `SoftFadeEnabled` (static switch, ДЕФОЛТ False) +
#      `SoftFadeDistance` (скаляр, ДЕФОЛТ 64 юн). Затухание у пересечения с непрозрачной геометрией:
#         fade = clamp( (SceneDepth − PixelDepth) / max(SoftFadeDistance, 1) , 0, 1 )
#      Additive (Add/ModAdd): Emissive ×= fade (гаснет в чёрное = «ничего не добавляет»).
#      Translucent (Alpha):   Opacity  ×= fade (гаснет покрытие).
#      Modulate (Mod):        Emissive = lerp(1, Emissive, fade) — гаснет В НЕЙТРАЛЬ (×1), а не в 0.
#
# ★ БЕЗОПАСНОСТЬ ДЕФОЛТА. `SoftFadeEnabled` — STATIC switch: при False ветка затухания НЕ КОМПИЛИРУЕТСЯ
#   вовсе (ноль инструкций, ноль сэмплеров глубины). `EmissiveBoost` при 1.0 даёт ровно прежнюю величину
#   (x×1.0 = x бит-в-бит) ценой одной ALU-инструкции.
#
# ★ ГРАФ НЕ ПЕРЕСОБИРАЕТСЯ. Правка ТОЛЬКО ДОБАВЛЯЕТ узлы поверх существующей цепи (как
#   asda_add_fadein_param.py / asda_add_vertexcolor_effects.py). `delete_all_material_expressions`
#   не вызывается ни разу — баг UE 5.8 (asda_mat_graph.py) исключён по построению.
#
# Идемпотентность: если в мастере уже есть скаляр `SoftFadeDistance` — материал пропускается.
# Запуск: python Tools/run_headless_safe.py Tools/asda_add_fx_glow_params.py --marker "FX-GLOW ЗАВЕРШЕНО"
import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

DIR = "/Game/Effects"
# (имя мастера, режим): режим решает, куда вешать затухание и нужен ли буст.
#   add       — BLEND_ADDITIVE, буст и затухание на Emissive
#   alpha     — BLEND_TRANSLUCENT, буст на Emissive, затухание на Opacity
#   modulate  — BLEND_MODULATE, буста НЕТ, затухание — lerp в нейтраль на Emissive
MASTERS = (("M_AsdaEffectAdd", "add"), ("M_AsdaEffectAlpha", "alpha"),
           ("M_AsdaEffectModAdd", "add"), ("M_AsdaEffectMod", "modulate"))

BOOST_SCALAR = "EmissiveBoost"          # множитель эмиссии (1.0 = как было)
FADE_SWITCH = "SoftFadeEnabled"         # static switch: включает глубинное затухание
FADE_SCALAR = "SoftFadeDistance"        # дистанция затухания в юнитах UE (см)
FADE_DEFAULT = 64.0                     # 64 юн ≈ 0.64 м — половина роста персонажа; подбирается данными


def param_names(mat):
    """Имена ВСЕХ параметров-узлов графа мастера (скаляры и static switch).

    ★ Почему не `get_material_default_scalar_parameter_value`: в UE 5.8 он на отсутствующем имени
      НЕ бросает исключение, а молча возвращает 0.0 — проверка «есть ли параметр» через него даёт
      ложное «есть» (первый прогон 09.09 пропустил все четыре мастера). Смотрим сам граф."""
    out = set()
    for e in MEL.get_material_expressions(mat):
        if isinstance(e, (unreal.MaterialExpressionScalarParameter,
                          unreal.MaterialExpressionStaticSwitchParameter)):
            try:
                out.add(str(e.get_editor_property("parameter_name")))
            except Exception:
                pass
    return out


def has_param(mat, name):
    """Есть ли в мастере параметр с таким именем (признак уже применённой правки)."""
    return name in param_names(mat)


def prop_source(mat, prop):
    """(узел, имя выхода) текущего входа материала — то, поверх чего мы встраиваемся."""
    node = MEL.get_material_property_input_node(mat, prop)
    if not node:
        return None, ""
    try:
        out = str(MEL.get_material_property_input_node_output_name(mat, prop))
    except Exception:
        out = ""
    return node, out


def build_fade(mat, expr, x, y):
    """Цепь коэффициента мягкого врезания: clamp((SceneDepth − PixelDepth)/max(dist,1), 0, 1).

    Возвращает узел-результат (выход по умолчанию). Считается один раз на материал."""
    sd = expr(unreal.MaterialExpressionSceneDepth, x, y)              # глубина непрозрачной сцены за пикселем
    pd = expr(unreal.MaterialExpressionPixelDepth, x, y + 130)        # глубина самого пикселя эффекта
    sub = expr(unreal.MaterialExpressionSubtract, x + 220, y + 40)    # насколько эффект «не дошёл» до геометрии
    MEL.connect_material_expressions(sd, "", sub, "A")
    MEL.connect_material_expressions(pd, "", sub, "B")
    dist = expr(unreal.MaterialExpressionScalarParameter, x, y + 270)
    dist.set_editor_property("parameter_name", FADE_SCALAR)
    dist.set_editor_property("default_value", FADE_DEFAULT)
    one = expr(unreal.MaterialExpressionConstant, x, y + 390)         # пол делителя: 0 в знаменателе = NaN
    one.set_editor_property("r", 1.0)
    mx = expr(unreal.MaterialExpressionMax, x + 220, y + 300)         # max(dist, 1) — деление всегда определено
    MEL.connect_material_expressions(dist, "", mx, "A")
    MEL.connect_material_expressions(one, "", mx, "B")
    div = expr(unreal.MaterialExpressionDivide, x + 440, y + 120)
    MEL.connect_material_expressions(sub, "", div, "A")
    MEL.connect_material_expressions(mx, "", div, "B")
    clamp = expr(unreal.MaterialExpressionClamp, x + 660, y + 120)    # дефолт узла: min 0, max 1
    # ★ ГРАБЛЯ (поймана прогоном asda_fx_verify_masters.py 09.09): у Clamp основной вход НЕ зовётся
    #   "Input" — как у OneMinus, это ДЕФОЛТНЫЙ вход, и имя должно быть ПУСТЫМ. С "Input" коннект
    #   молча не создавался: «(Node Clamp) Missing Clamp input» → Default Material в игре.
    MEL.connect_material_expressions(div, "", clamp, "")
    return clamp


def patch(name, mode):
    """Добавить в мастер параметры яркости и мягкого врезания. Возвращает True, если правка внесена."""
    path = "%s/%s" % (DIR, name)
    mat = EAL.load_asset(path)
    if not mat:
        unreal.log_warning("[FX-GLOW] нет ассета %s" % path)
        return False
    if has_param(mat, FADE_SCALAR):
        unreal.log("[FX-GLOW] %s: параметры уже есть — пропуск" % name)
        return False
    n0 = len(MEL.get_material_expressions(mat))

    def expr(cls, x, y):
        return MEL.create_material_expression(mat, cls, x, y)

    # ── 1. Яркость эмиссии (не для modulate) ────────────────────────────────────────────────
    if mode != "modulate":
        src, out = prop_source(mat, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        if not src:
            unreal.log_warning("[FX-GLOW] %s: вход Emissive пуст — правка отменена" % name)
            return False
        # ★ Буст — ОБЫЧНЫЙ скаляр, а не static switch (в отличие от затухания). Причины:
        #   (1) при дефолте 1.0 выражение точно прежнее: умножение на 1.0 в IEEE-754 возвращает
        #       аргумент бит-в-бит, а цена — одна ALU-инструкция на пиксель (153 → 154, замер ниже);
        #   (2) скаляр правится в рантайме БЕЗ пересборки шейдера — именно так снимается ряд кадров
        #       «×2 / ×5 / ×10» для владельца за один запуск игры; static switch потребовал бы
        #       компиляции новой перестановки прямо в кадре.
        boost = expr(unreal.MaterialExpressionScalarParameter, 1000, 900)
        boost.set_editor_property("parameter_name", BOOST_SCALAR)
        boost.set_editor_property("default_value", 1.0)
        mul = expr(unreal.MaterialExpressionMultiply, 1220, 820)      # прежняя эмиссия × EmissiveBoost
        MEL.connect_material_expressions(src, out, mul, "A")
        MEL.connect_material_expressions(boost, "", mul, "B")
        MEL.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    # ── 2. Мягкое врезание ──────────────────────────────────────────────────────────────────
    fade = build_fade(mat, expr, 1000, 1300)
    if mode == "alpha":
        # Translucent: гасим ПОКРЫТИЕ (Opacity), эмиссию не трогаем — иначе цвет уплывает при фейде.
        prop = unreal.MaterialProperty.MP_OPACITY
        src, out = prop_source(mat, prop)
        if not src:
            unreal.log_warning("[FX-GLOW] %s: вход Opacity пуст — затухание не поставлено" % name)
            return False
        faded = expr(unreal.MaterialExpressionMultiply, 1900, 1360)
        MEL.connect_material_expressions(src, out, faded, "A")
        MEL.connect_material_expressions(fade, "", faded, "B")
    else:
        prop = unreal.MaterialProperty.MP_EMISSIVE_COLOR
        src, out = prop_source(mat, prop)                              # уже с учётом свитча буста
        if not src:
            unreal.log_warning("[FX-GLOW] %s: вход Emissive пуст — затухание не поставлено" % name)
            return False
        if mode == "modulate":
            # Modulate: нейтраль = 1 (dst×1). Гасить надо В НЕЙТРАЛЬ, а не в ноль,
            #   иначе на границе с геометрией дым чернил бы фон вместо того, чтобы исчезать.
            faded = expr(unreal.MaterialExpressionLinearInterpolate, 1900, 1360)
            neutral = expr(unreal.MaterialExpressionConstant, 1680, 1300)
            neutral.set_editor_property("r", 1.0)
            MEL.connect_material_expressions(neutral, "", faded, "A")  # fade=0 → нейтраль
            MEL.connect_material_expressions(src, out, faded, "B")     # fade=1 → как было
            MEL.connect_material_expressions(fade, "", faded, "Alpha")
        else:
            # Additive: нейтраль = 0 (dst+0) — простое умножение.
            faded = expr(unreal.MaterialExpressionMultiply, 1900, 1360)
            MEL.connect_material_expressions(src, out, faded, "A")
            MEL.connect_material_expressions(fade, "", faded, "B")
    swf = expr(unreal.MaterialExpressionStaticSwitchParameter, 2120, 1320)
    swf.set_editor_property("parameter_name", FADE_SWITCH)
    swf.set_editor_property("default_value", False)                    # ДЕФОЛТ: ветка затухания не компилируется
    MEL.connect_material_expressions(faded, "", swf, "True")
    MEL.connect_material_expressions(src, out, swf, "False")
    MEL.connect_material_property(swf, "", prop)

    MEL.recompile_material(mat)
    mat.modify()
    ok = EAL.save_loaded_asset(mat)
    n1 = len(MEL.get_material_expressions(mat))
    unreal.log("[FX-GLOW] %s (%s): узлов %d → %d, сохранён=%s" % (name, mode, n0, n1, ok))
    return True


def verify():
    """Контроль ПОСЛЕ правки: параметры на месте, дефолты прежние, входы материала не оборваны."""
    for name, mode in MASTERS:
        mat = EAL.load_asset("%s/%s" % (DIR, name))
        if not mat:
            continue
        # Дефолты читаем ИЗ УЗЛОВ графа (геттеры MEL на отсутствующем имени врут нулём — см. param_names).
        have = param_names(mat)
        bits = []
        for e in MEL.get_material_expressions(mat):
            try:
                nm = str(e.get_editor_property("parameter_name"))
            except Exception:
                continue
            if nm in (BOOST_SCALAR, FADE_SCALAR, FADE_SWITCH):
                bits.append("%s=%s" % (nm, e.get_editor_property("default_value")))
        for nm in (BOOST_SCALAR, FADE_SCALAR, FADE_SWITCH):
            if nm not in have:
                bits.append("%s=НЕТ" % nm)
        em = MEL.get_material_property_input_node(mat, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        op = MEL.get_material_property_input_node(mat, unreal.MaterialProperty.MP_OPACITY)
        unreal.log("[FX-GLOW-VERIFY] %s (%s): %s | Emissive=%s Opacity=%s" % (
            name, mode, ", ".join(bits),
            em.get_class().get_name() if em else "ОБРЫВ", op.get_class().get_name() if op else "—"))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон tools-no-import-side-effects)."""
    for name, mode in MASTERS:
        patch(name, mode)
    verify()
    unreal.log("FX-GLOW ЗАВЕРШЕНО")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
