# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_mat_graph.py — ЕДИНАЯ точка правды по пересборке графа мастер-материала «на месте»
#   (импортируемый модуль, БЕЗ побочных действий при импорте — закон [[tools-no-import-side-effects]]).
#
# ★★★ ГРАБЛЯ UE 5.8 (доказано ревизией `audit_2026-09-02/review_soul_ghost_charselect.md` §1):
#   `UMaterialEditingLibrary::DeleteAllMaterialExpressions` (`MaterialEditingLibrary.cpp:559-568`)
#   удаляет узлы ИЗ МАССИВА ПО ХОДУ ИТЕРАЦИИ ПО НЕМУ ЖЕ:
#       for (UMaterialExpression* Expression : Material->GetExpressions())
#           DeleteMaterialExpression(Material, Expression);
#   ⇒ после каждого удаления массив сдвигается и итератор перескакивает через соседа — ВЫЖИВАЕТ
#   КАЖДЫЙ ВТОРОЙ узел. Один вызов на графе из 11 узлов оставил 5, из 5 — 2, из 2 — 0.
#   Последствие для генераторов «чистим граф существующего ассета + собираем заново»: в ассете
#   остаются ФАНТОМЫ старого графа — в том числе ScalarParameter с тем же именем, что и новый, но
#   со СТАРЫМ дефолтом. Кэш параметров (`FMaterialCachedExpressionData`) берёт первый попавшийся ⇒
#   редактор и все MIC отдают старое значение (у `M_AsdaSoulSilhouette` OpacityScale 1.0 вместо 0.3;
#   у `M_AsdaCharLit` — дубли AmbientFloor×4, SpecBase×3, Roughness×2). Промежуточный
#   `recompile_material` на «пустом» графе этого НЕ чинит — граф не был пуст.
#
# ЛЕЧЕНИЕ: чистить В ЦИКЛЕ до `get_material_expressions == 0` и ПОСЛЕ сборки контролировать граф
#   (число узлов и отсутствие дублей имён параметров); при расхождении — ошибка и НЕ сохранять.
#
# Используется: asda_build_soul_silhouette_mat.py, asda_fix_charlit_lighting.py,
#   asda_build_sky_dome.py, asda_add_cloud_shadows.py, asda_build_effect_mod_masters.py,
#   asda_gacha_light_node.py (через него — все сборщики гачи).
import unreal

MEL = unreal.MaterialEditingLibrary


def scalar_param(mat, name, value, x, y):
    """Узел ScalarParameter `name` = `value` в точке (x, y) графа `mat` (редактируемый скаляр мастера).
    Одна реализация (дедуп №2 В1 Т19, 11.10.2026): было 4 копии `scal` — asda_build_blend_material,
    asda_build_materials_lit, asda_build_scroll_lit, asda_fix_terrainlit_vertexcolor."""
    s = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    s.set_editor_property("parameter_name", name)
    s.set_editor_property("default_value", value)
    return s

# Потолок проходов чистки: каждый проход удаляет ≥ половину узлов, 20 проходов хватает на граф
#   из ~10^6 узлов — практически «пока не пусто», но без риска вечного цикла на неудаляемом узле.
MAX_CLEAR_PASSES = 20


def expression_count(mat):
    """Число выражений в графе мастера (то, что реально лежит в ассете, а не в кэше параметров)."""
    return len(MEL.get_material_expressions(mat))


def clear_expressions(mat, log=None, tag="[mat-graph]"):
    """Вычистить граф мастера ДО ПУСТОГО — обход бага `DeleteAllMaterialExpressions` (см. шапку).

    Возвращает число проходов. Если после MAX_CLEAR_PASSES граф не пуст — RuntimeError (ассет
    в таком состоянии собирать нельзя: получим фантомы и дубли параметров)."""
    n0 = expression_count(mat)
    passes = 0
    while expression_count(mat) > 0 and passes < MAX_CLEAR_PASSES:
        MEL.delete_all_material_expressions(mat)
        passes += 1
        if log:
            log("%s чистка: проход %d → осталось %d" % (tag, passes, expression_count(mat)))
    left = expression_count(mat)
    if left:
        raise RuntimeError("%s граф %s НЕ вычищен за %d проходов: осталось %d узлов — сборка отменена"
                           % (tag, mat.get_name(), passes, left))
    if log:
        log("%s граф %s вычищен: было %d узлов, проходов %d" % (tag, mat.get_name(), n0, passes))
    return passes


def parameter_duplicates(mat):
    """{имя параметра: сколько узлов} — только для имён, встречающихся БОЛЬШЕ одного раза.
    Дубль имени = признак фантомов старого графа (или ошибки генератора)."""
    seen = {}
    for e in MEL.get_material_expressions(mat):
        try:
            pn = str(e.get_editor_property("parameter_name"))
        except Exception:
            continue                      # узел без параметра (Multiply, Constant, ...)
        if not pn or pn == "None":
            continue
        seen[pn] = seen.get(pn, 0) + 1
    return {k: v for k, v in seen.items() if v > 1}


def check_graph(mat, expected=None, log=None, tag="[mat-graph]"):
    """Контроль графа ПОСЛЕ сборки, ДО сохранения.

    expected — ожидаемое число узлов (None = не проверять число, только дубли). При расхождении
    или при дублях имён параметров — RuntimeError: вызывающий обязан НЕ сохранять ассет.
    Возвращает фактическое число узлов."""
    n = expression_count(mat)
    dups = parameter_duplicates(mat)
    problems = []
    if expected is not None and n != expected:
        problems.append("узлов %d, ожидалось %d" % (n, expected))
    if dups:
        problems.append("дубли параметров %s" % dups)
    if problems:
        raise RuntimeError("%s граф %s НЕ СООТВЕТСТВУЕТ генератору (%s) — ассет НЕ сохранён"
                           % (tag, mat.get_name(), "; ".join(problems)))
    if log:
        log("%s граф %s: узлов %d%s, дублей параметров нет" % (
            tag, mat.get_name(), n, (" (= ожидаемым)" if expected is not None else "")))
    return n
