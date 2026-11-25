# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_lit_ambient_floor.py — снять «двойной свет» в Lit-мастерах (правка Д-3 аудита света 2026-09-03).
#
# ЧТО БЫЛО: во всех Lit-мастерах Emissive = BaseColor · AmbientFloor(0.25) (asda_add_ambient_floor.py,
#   wire_char) — безусловный самосвет 25 % альбедо ПОВЕРХ UE-света: не зависит от карты/тени/ночи,
#   складывается с SkyLight ⇒ тень никогда темнее ~0.5·tex, ночные карты не тёмные, «плоско/серо».
# ЧТО ДЕЛАЕМ: дефолт параметра AmbientFloor → 0.0 В АССЕТЕ (граф не перестраиваем — узел остаётся
#   рычагом: владелец может поднять пол на мастере/MIC без пересборки). Честный ambient теперь даёт
#   SkyLight per-map (asda_convert_map_lit.py, SKY_K·max(ambient)).
#   Мастера с ГЕНЕРАТОРОМ «на месте» (M_AsdaCharLit → asda_fix_charlit_lighting.py, M_AsdaCharLitSoul →
#   asda_build_charlit_soul_mat.py) перегоняются своими скриптами (AMBIENT_FLOOR=0 в генераторе);
#   здесь — мастера, у которых пол дописывался поверх готового графа: M_AsdaObjectLit, M_AsdaTerrainLit,
#   и Lit-материалы выбора персонажа M_AsdaSoulSilhouette, M_AsdaPreviewFade («как у M_AsdaCharLit»).
#   ★ 15.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 15): M_AsdaSoulSilhouette — пакет сцены логина, без ASDA_ALLOW_LOGIN_WRITE=1 пропускается.
#
# КОНТРОЛЬ (asda_mat_graph, баг UE 5.8 DeleteAllMaterialExpressions здесь не задействован — граф не
#   чистим): ровно ОДИН ScalarParameter AmbientFloor в графе, число узлов и дубли параметров ДО/ПОСЛЕ
#   одинаковы, дефолт читается ИЗ КЭША АССЕТА после сохранения = 0.0; иначе RuntimeError и ассет не сохранён.
#   Бэкапы .uasset — в AstoryClient/_backup/lit_ambient_floor_2026-09-03/ (делает вызывающий).
#
# Запуск (СКРЫТО, редактор закрыт):
#   python Tools/run_headless_safe.py Tools/asda_fix_lit_ambient_floor.py --marker FLOOR-FINISHED
#   ASDA_FLOOR=0.05 — иной дефолт (рычаг); ASDA_FLOOR_MASTERS="M_AsdaObjectLit||..." — подмножество.
import os, sys
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_mat_graph as MG

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty
PKG = "/Game/Materials"
MASTERS = ["M_AsdaObjectLit", "M_AsdaTerrainLit", "M_AsdaSoulSilhouette", "M_AsdaPreviewFade"]
FLOOR = float(os.environ.get("ASDA_FLOOR", "0.0"))


def log(m): unreal.log_warning("[floor] %s" % m)


def floor_nodes(m):
    return [e for e in MEL.get_material_expressions(m)
            if isinstance(e, unreal.MaterialExpressionScalarParameter)
            and str(e.get_editor_property("parameter_name")) == "AmbientFloor"]


def snapshot(m, tag):
    em = MEL.get_material_property_input_node(m, MP.MP_EMISSIVE_COLOR)
    log("%s %s: узлов %d, дубли %s, AmbientFloor(кэш)=%r, Emissive <- %s, скаляры=%s векторы=%s" % (
        m.get_name(), tag, MG.expression_count(m), MG.parameter_duplicates(m) or "нет",
        MEL.get_material_default_scalar_parameter_value(m, "AmbientFloor"),
        em.get_name() if em else "<не подключён>",
        [str(n) for n in MEL.get_scalar_parameter_names(m)], [str(n) for n in MEL.get_vector_parameter_names(m)]))


def fix_one(name):
    path = "%s/%s" % (PKG, name)
    m = EAL.load_asset(path)
    if not m:
        raise RuntimeError("нет мастера %s" % path)
    snapshot(m, "ДО")
    n0 = MG.expression_count(m); d0 = MG.parameter_duplicates(m)
    nodes = floor_nodes(m)
    if len(nodes) != 1:
        raise RuntimeError("%s: узлов AmbientFloor %d, ожидался ровно 1 — ассет НЕ тронут" % (name, len(nodes)))
    nodes[0].set_editor_property("default_value", FLOOR)
    MG.check_graph(m, expected=n0, log=log, tag="[floor] %s" % name)
    if MG.parameter_duplicates(m) != d0:
        raise RuntimeError("%s: дубли параметров изменились — ассет НЕ сохранён" % name)
    MEL.recompile_material(m)
    m.modify()
    ok = EAL.save_loaded_asset(m, False)     # правки через MEL не помечают пакет dirty → only_if_is_dirty=False
    if not ok:
        raise RuntimeError("%s: save_loaded_asset вернул False" % name)
    got = MEL.get_material_default_scalar_parameter_value(m, "AmbientFloor")   # ИЗ КЭША АССЕТА
    if abs(got - FLOOR) > 1e-6:
        raise RuntimeError("%s: кэш AmbientFloor=%r ≠ %r после сохранения" % (name, got, FLOOR))
    snapshot(m, "ПОСЛЕ")


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    only = os.environ.get("ASDA_FLOOR_MASTERS", "")
    names = [n for n in MASTERS if (not only or n in only.split("||"))]
    # ★★★ ЗАЩИТА СЦЕНЫ ЛОГИНА (15.09.2026, Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 15): M_AsdaSoulSilhouette — пакет сцены логина
    #   (мастер только 14 MIC силуэтов cre_cresoul*_preview). Без ASDA_ALLOW_LOGIN_WRITE=1 он пропускается ДО загрузки, остальные
    #   мастера (общие с игрой) — как раньше. Итог пропуска — одной строкой [LOGIN-GUARD].
    from asda_login_guard import login_package_blocked   # Tools уже в sys.path (строка перед импортом asda_mat_graph)
    login_skipped = [n for n in names if login_package_blocked("%s/%s" % (PKG, n), "asda_fix_lit_ambient_floor.py", quiet=True)]
    if login_skipped:
        names = [n for n in names if n not in login_skipped]
        unreal.log_warning("[LOGIN-GUARD] [floor] мастеров сцены логина пропущено: %d (%s) — AmbientFloor в них не менялся "
                           "(только с явного разрешения владельца и ASDA_ALLOW_LOGIN_WRITE=1)" % (len(login_skipped), ", ".join(login_skipped)))
    for n in names:
        fix_one(n)
    log("ЗАВЕРШЕНО (FLOOR-FINISHED): %s → AmbientFloor=%.3f" % (", ".join(names), FLOOR))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
