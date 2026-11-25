# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_objectlit_graph.py — ПЕРЕСБОРКА ГРАФА мастера объектов M_AsdaObjectLit НА МЕСТЕ (сет C-2, 05.09.2026).
#
# ЗАЧЕМ. До сета C-2 у M_AsdaObjectLit не было генератора «на месте»: build_object() в asda_build_materials_lit.py делал fresh()
#   (delete_asset + create) — это обнулило бы parent у 6771 MIC и usage-флаги (used_with_static_lighting нужен сцене логина —
#   память material-usage-flags-packaged). Живой мастер = build_object (5 узлов) + asda_add_ambient_floor.py (AmbientFloor, Multiply→Emissive)
#   + asda_add_fadein_param.py (FadeIn, Multiply→OpacityMask) = 9 узлов (дамп setC2_audit_before.json). Теперь тот же граф строит
#   GEN.wire_object() + ветки рельефа NormalStudio (GEN.wire_normal_studio, 28 узлов) ⇒ 37.
#
# КАК (образец — asda_fix_charlit_lighting.py): чистка графа В ЦИКЛЕ до нуля (баг UE 5.8 DeleteAllMaterialExpressions — каждый второй узел
#   выживает; asda_mat_graph.clear_expressions), пересборка GEN.wire_object() с ПРОВЕРЯЮЩИМИ соединениями (тихий False недопустим),
#   контроль ДО сохранения: ровно WIRE_OBJECT_EXPRESSIONS узлов, ни одного дубля имён параметров, число соединений, дефолты скаляров
#   ИЗ КЭША АССЕТА, дефолт-текстуры сэмплеров рельефа нужного сжатия, свойства мастера (blend/shading/usage) как были. Иначе RuntimeError —
#   ассет на диске остаётся прежним.
#
# ★ Намеренно воспроизводимый дефект генератора (как у wire_char): C(vc,"RGB",bc,"B") у VertexColor молча не соединяется (выход
#   безымянный) — BaseColor = Tex.RGB. Менять только с разрешения владельца (KNOWN_DANGLING из asda_fix_charlit_lighting).
#
# Запуск (СКРЫТО, редактор закрыт; ⚠ живой мастер ВСЕХ объектов карт — бэкап .uasset до прогона; файл read-only от P4 → снимается тут же):
#   python Tools/run_headless_safe.py Tools/asda_fix_objectlit_graph.py --marker OBJECTLIT-FINISHED
import os, sys, stat
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_build_materials_lit as GEN     # тело генератора под __main__, импорт безопасен
import asda_mat_graph as MG                # чистка графа до пустого + контроль после сборки
from asda_fix_charlit_lighting import KNOWN_DANGLING   # заведомо пустой вход VertexColor→BaseColor

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty
PATH = "/Game/Materials/M_AsdaObjectLit"
FILE = "@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Content/Materials/M_AsdaObjectLit.uasset"
READONLY_LIST = "@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/_backup/2026-09-05_setC2/_readonly_list.txt"
EXPECTED = GEN.WIRE_OBJECT_EXPRESSIONS                       # ★ сет D: (9 + 7 листва) + 28 = 44
# Соединений wire_object(): C — Tex.RGB→bc.A, bc→em.A, floor→em.B, Tex.A→om.A, fade→om.B = 5 (+1 заведомо пустой VertexColor→bc.B);
#   P — BaseColor, Roughness, Metallic, Emissive, OpacityMask = 5. + листва (сет D): C 6, P 2 (ShadingModel, SubsurfaceColor).
#   + рельеф: C 34, P 4 (Normal, AO, Roughness/Emissive перевязаны).
LINKS_C = 5 + GEN.FOLIAGE_LINKS + GEN.NS_LINKS_FULL          # 45
LINKS_P = 5 + GEN.FOLIAGE_OUTPUTS + GEN.NS_OUTPUTS_FULL      # 11
DANGLING = 1
SCALARS = {"Roughness": GEN.ROUGH, "Metallic": 0.0, "AmbientFloor": GEN.AMBIENT_FLOOR, "FadeIn": GEN.FADE_IN_DEFAULT,
           "SSSScale": GEN.FOLIAGE_SSS_SCALE}                # ★ сет D
SCALARS.update(GEN.NS_SCALARS)
TEXTURES = {"TexN": GEN.NS_DEFAULT_NORMAL, "TexRE": GEN.NS_DEFAULT_RE, "TexC": GEN.NS_DEFAULT_C}
# ★ сет D: shading_model больше НЕ «как было» — ожидается MSM_FROM_MATERIAL_EXPRESSION (свитч Foliage выбирает DefaultLit/TwoSidedFoliage);
#   остальные свойства (blend, two_sided, clip, usage — в т.ч. used_with_static_lighting для сцены логина) обязаны пережить пересборку.
EXPECTED_SHADING = unreal.MaterialShadingModel.MSM_FROM_MATERIAL_EXPRESSION
KEEP_PROPS = ("blend_mode", "two_sided", "opacity_mask_clip_value", "used_with_skeletal_mesh",
              "used_with_instanced_static_meshes", "used_with_static_lighting", "cast_dynamic_shadow_as_masked")


def log(m): unreal.log_warning("[objectlit] %s" % m)


_links = {"C": 0, "P": 0, "dangling": 0}


def _checked_C(a, ao, b, bi):
    if not MEL.connect_material_expressions(a, ao, b, bi):
        key = (a.get_class().get_name(), ao, b.get_class().get_name(), bi)
        if key in KNOWN_DANGLING:
            _links["dangling"] += 1
            log("вход %s.%s остаётся ПУСТЫМ (%s.%s — известный дефект генератора, воспроизводится намеренно)" % (b.get_name(), bi, a.get_name(), ao))
            return
        raise RuntimeError("не соединилось: %s.%s -> %s.%s" % (a.get_name(), ao, b.get_name(), bi))
    _links["C"] += 1


def _checked_P(m, e, eo, prop):
    if prop == GEN.SHADING_MODEL_PROP:                      # ★ сет D: вход ShadingModel через C++ мостик (MP_ShadingModel скрыт от Python)
        if not GEN.connect_shading_model(m, e, eo):
            raise RuntimeError("не подключился вход ShadingModel <- %s.%s" % (e.get_name(), eo))
        _links["P"] += 1
        return
    if not MEL.connect_material_property(e, eo, prop):
        raise RuntimeError("не подключился выход %s <- %s.%s" % (prop, e.get_name(), eo))
    _links["P"] += 1


def snapshot(m, tag):
    log("%s: узлов %d, дубли параметров %s" % (tag, MG.expression_count(m), MG.parameter_duplicates(m) or "нет"))
    log("%s: скаляры=%s текстуры=%s" % (tag, [str(n) for n in MEL.get_scalar_parameter_names(m)], [str(n) for n in MEL.get_texture_parameter_names(m)]))
    for p, nm in ((MP.MP_BASE_COLOR, "BaseColor"), (MP.MP_NORMAL, "Normal"), (MP.MP_AMBIENT_OCCLUSION, "AO"), (MP.MP_EMISSIVE_COLOR, "Emissive"),
                  (MP.MP_ROUGHNESS, "Roughness"), (MP.MP_METALLIC, "Metallic"), (MP.MP_OPACITY_MASK, "OpacityMask"),
                  (MP.MP_SUBSURFACE_COLOR, "SubsurfaceColor")):
        n = MEL.get_material_property_input_node(m, p)
        log("   %-15s = %s" % (nm, n.get_name() if n else "<не подключён>"))
    n = GEN.shading_model_input_node(m)
    log("   %-15s = %s" % ("ShadingModel", n.get_name() if n else "<не подключён>"))
    log("   shading_model=%s, static switches=%s, векторы=%s" % (m.get_editor_property("shading_model"),
        [str(n) for n in MEL.get_static_switch_parameter_names(m)], [str(n) for n in MEL.get_vector_parameter_names(m)]))
    if "FoliageSSSTint" in [str(n) for n in MEL.get_vector_parameter_names(m)]:
        log("   кэш FoliageSSSTint = %s" % MEL.get_material_default_vector_parameter_value(m, "FoliageSSSTint"))
    if "Foliage" in [str(n) for n in MEL.get_static_switch_parameter_names(m)]:
        log("   кэш Foliage (дефолт свитча) = %s" % MEL.get_material_default_static_switch_parameter_value(m, "Foliage"))
    have = [str(n) for n in MEL.get_scalar_parameter_names(m)]
    log("   кэш: %s" % ", ".join("%s=%r" % (k, MEL.get_material_default_scalar_parameter_value(m, k)) for k in sorted(SCALARS) if k in have))
    for e in MEL.get_material_expressions(m):
        if isinstance(e, unreal.MaterialExpressionTextureSampleParameter2D):
            t = e.get_editor_property("texture")
            log("   сэмплер %s: texture=%s sampler=%s" % (e.get_editor_property("parameter_name"), t.get_path_name() if t else None, e.get_editor_property("sampler_type")))
    log("   свойства: %s" % ", ".join("%s=%s" % (k, m.get_editor_property(k)) for k in KEEP_PROPS))


def make_writable():
    """Файл мастера read-only от Perforce → снять и записать путь в список для reconcile (память p4v-revert-wiped-assets)."""
    if os.path.isfile(FILE) and not os.access(FILE, os.W_OK):
        os.chmod(FILE, stat.S_IWRITE | stat.S_IREAD)
        os.makedirs(os.path.dirname(READONLY_LIST), exist_ok=True)
        with open(READONLY_LIST, "a", encoding="utf-8") as f:
            f.write("Content/Materials/M_AsdaObjectLit.uasset\n")
        log("снят read-only: %s" % FILE)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал (закон [[tools-no-import-side-effects]])."""
    m = EAL.load_asset(PATH)
    if not m:
        raise RuntimeError("нет мастера %s" % PATH)
    snapshot(m, "ДО")
    props_before = {k: m.get_editor_property(k) for k in KEEP_PROPS}
    make_writable()
    MG.clear_expressions(m, log=log, tag="[objectlit]")
    MEL.recompile_material(m)
    orig_C, orig_P = GEN.C, GEN.P
    GEN.C, GEN.P = _checked_C, _checked_P
    try:
        GEN.wire_object(m)
    finally:
        GEN.C, GEN.P = orig_C, orig_P
    if (_links["C"], _links["P"], _links["dangling"]) != (LINKS_C, LINKS_P, DANGLING):
        raise RuntimeError("соединений %s, ожидалось C=%d P=%d dangling=%d — ассет НЕ сохранён" % (_links, LINKS_C, LINKS_P, DANGLING))
    log("соединений: узел→узел %d, узел→выход %d, заведомо пустых %d (= ожидаемым)" % (_links["C"], _links["P"], _links["dangling"]))
    MG.check_graph(m, expected=EXPECTED, log=log, tag="[objectlit]")
    bad = []
    for k, v in SCALARS.items():
        got = MEL.get_material_default_scalar_parameter_value(m, k)
        if abs(got - v) > 1e-6:
            bad.append("%s: кэш %r ≠ генератор %r" % (k, got, v))
    for k, v in props_before.items():
        if m.get_editor_property(k) != v:
            bad.append("%s: было %s, стало %s" % (k, v, m.get_editor_property(k)))
    # ★ сет D: листва — shading_model из графа, свитч Foliage (дефолт False), входы ShadingModel/SubsurfaceColor, тинт из кэша
    if m.get_editor_property("shading_model") != EXPECTED_SHADING:
        bad.append("shading_model %s ≠ FromMaterialExpression" % m.get_editor_property("shading_model"))
    if "Foliage" not in [str(n) for n in MEL.get_static_switch_parameter_names(m)]:
        bad.append("нет StaticSwitchParameter Foliage")
    elif MEL.get_material_default_static_switch_parameter_value(m, "Foliage"):
        bad.append("дефолт свитча Foliage = True (ожидался False: DefaultLit для всех MIC без оверрайда)")
    tv = MEL.get_material_default_vector_parameter_value(m, "FoliageSSSTint")
    if max(abs(tv.r - GEN.FOLIAGE_SSS_TINT[0]), abs(tv.g - GEN.FOLIAGE_SSS_TINT[1]), abs(tv.b - GEN.FOLIAGE_SSS_TINT[2])) > 1e-5:
        bad.append("FoliageSSSTint кэш %s ≠ %s" % (tv, GEN.FOLIAGE_SSS_TINT))
    if not MEL.get_material_property_input_node(m, MP.MP_SUBSURFACE_COLOR):
        bad.append("вход SubsurfaceColor пуст")
    if not GEN.shading_model_input_node(m):
        bad.append("вход ShadingModel пуст")
    seen = {}
    for e in MEL.get_material_expressions(m):
        if isinstance(e, unreal.MaterialExpressionTextureSampleParameter2D):
            pn = str(e.get_editor_property("parameter_name"))
            if pn in TEXTURES:
                t = e.get_editor_property("texture")
                seen[pn] = t.get_path_name().split(".")[0] if t else None
                if seen[pn] != TEXTURES[pn]:
                    bad.append("сэмплер %s: дефолт %s ≠ %s" % (pn, seen[pn], TEXTURES[pn]))
    for pn in TEXTURES:
        if pn not in seen:
            bad.append("нет сэмплера %s" % pn)
    for p, nm in ((MP.MP_BASE_COLOR, "BaseColor"), (MP.MP_NORMAL, "Normal"), (MP.MP_AMBIENT_OCCLUSION, "AO"), (MP.MP_EMISSIVE_COLOR, "Emissive"),
                  (MP.MP_OPACITY_MASK, "OpacityMask"), (MP.MP_ROUGHNESS, "Roughness")):
        if not MEL.get_material_property_input_node(m, p):
            bad.append("вход %s пуст" % nm)
    if bad:
        raise RuntimeError("контроль после сборки: %s — ассет НЕ сохранён" % "; ".join(bad))
    m.modify()
    ok = EAL.save_loaded_asset(m, False)
    log("сохранено: %s" % ok)
    if not ok:
        raise RuntimeError("save_loaded_asset вернул False")
    snapshot(m, "ПОСЛЕ")
    log("ЗАВЕРШЕНО (OBJECTLIT-FINISHED)")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
