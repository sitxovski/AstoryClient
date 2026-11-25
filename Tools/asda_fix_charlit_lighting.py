# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_charlit_lighting.py — ФИКС РЕГРЕССИИ СВЕТОТЕНИ у M_AsdaCharLit (2026-07-31).
#
# СИМПТОМ (живой тест): «персонаж слишком чувствителен к свету, очень легко затемняется,
#   тени отбрасываются неадекватно» — сразу после перевода 2585 материалов персонажа/экипа/
#   оружия/причёсок с masked-мастера M_AsdaObjectLit на новый M_AsdaCharLit (OPAQUE).
#
# КОРЕНЬ (дифф «параметр в параметр»: Tools/asda_dump_material_diff.py →
#   Reports/material_master_diff.txt). Кроме blend_mode различались РОВНО два входа:
#   ★ 2026-09-03 (Д-3 аудита света): AmbientFloor в генераторе = 0.0 (GEN.AMBIENT_FLOOR) — узел-рычаг
#     остаётся, Emissive по умолчанию 0; этот скрипт применяет новый дефолт к живому мастеру.
#   1) MP_EMISSIVE_COLOR: у M_AsdaObjectLit = BaseColor·AmbientFloor(0.25), у M_AsdaCharLit — НЕ
#      ПОДКЛЮЧЕН. Причина: M_AsdaObjectLit после генератора дорабатывался отдельным скриптом
#      asda_add_ambient_floor.py («аниме-правило»: тень НИКОГДА не чёрная), а M_AsdaCharLit был
#      собран генератором С НУЛЯ ⇒ доработка в него не попала.  ← ГЛАВНАЯ ПРИЧИНА ЗАТЕМНЕНИЯ
#   2) MP_SPECULAR: у M_AsdaObjectLit НЕ подключён (дефолт UE 0.5), у M_AsdaCharLit = SpecScale·Tex.A.
#      У части текстур служебная альфа мала/нулевая ⇒ Specular→0. В ЭТАЛОНЕ (system/EmptyProject.fx,
#      PShade) Tex.w — АДДИТИВНАЯ добавка блика ПОВЕРХ базового уровня:
#          ... + ((vSpecular.xyz * Tex.w) * ShadowAlpha.z);  += texCUBE(...)*(Tex.w*0.3f)*ShadowAlpha.z
#      а НЕ множитель базового Specular. Умножение — чужая механика, вносила затемнение.
#   Всё остальное (two_sided, shading_model, opacity_mask_clip_value*, usage-флаги,
#   cast_dynamic_shadow_as_masked, contact_shadows, screen_space_reflections, translucency-*)
#   у обоих мастеров УЖЕ идентично — тени материал не менял ни одним флагом.
#   (* clip value у opaque не используется.)
#
# ЧТО ДЕЛАЕТ: чистит граф СУЩЕСТВУЮЩЕГО ассета (asda_mat_graph.clear_expressions — В ЦИКЛЕ до
#   пустого, обход бага UE 5.8 DeleteAllMaterialExpressions) и пересобирает его функцией wire_char()
#   из asda_build_materials_lit.py — единый источник правды; перед сохранением проверяет число узлов
#   и отсутствие дублей имён параметров.
#   ★ БЕЗ delete_asset: пересоздание мастера обнулило бы parent у 2585 MIC и usage-флаги.
#   Идемпотентно: повторный запуск даёт тот же граф.
#
# Запуск (СКРЫТО, редактор закрыт; ⚠ живой мастер ВСЕХ персонажей — бэкап .uasset до прогона):
#   python Tools/run_headless_safe.py Tools/asda_fix_charlit_lighting.py --marker CHARLIT-FINISHED
import os, sys
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_build_materials_lit as GEN     # тело генератора под __main__, импорт безопасен
import asda_mat_graph as MG                # ★ чистка графа до пустого + контроль после сборки (баг UE 5.8)

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty
PATH = "/Game/Materials/M_AsdaCharLit"
# Ровно столько узлов строит GEN.wire_char(): Tex, VertexColor, Multiply(BaseColor), Roughness, Metallic,
#   SpecBase, SpecAlphaBoost, Multiply(spec), Add(spec), AmbientFloor, Multiply(Emissive) = 11.
#   При изменении wire_char() — обновить. ⚠ Живой мастер может нести доработки ПОВЕРХ генератора
#   (asda_add_toon_rim.py) — этот скрипт их СНОСИТ, как и раньше; после него доработки перегонять.
#   ★ Сет C-2 (05.09.2026): + GEN.NS_EXPRESSIONS_FULL (28) узлов рельефа NormalStudio (GEN.wire_normal_studio, зовётся из wire_char) = 39.
#   ★ Сет D (05.09.2026): + GEN.ROUGH_AB_EXPRESSIONS (5: RoughHi/RoughLo/Lerp/RoughAlphaMix/Lerp — roughness по маске, дефолт = константа)
#     + GEN.RIM_EXPRESSIONS (20: рим с теневой стороны из MPC_AsdaToon, RimEnable=0 ⇒ 0) = 11 + 5 + 28 + 20 = 64.
WIRE_CHAR_BASE_EXPRESSIONS = 11 + GEN.ROUGH_AB_EXPRESSIONS
WIRE_CHAR_EXPRESSIONS = WIRE_CHAR_BASE_EXPRESSIONS + GEN.NS_EXPRESSIONS_FULL + GEN.RIM_EXPRESSIONS
# Соединений, которые делает wire_char(): 7 «узел→узел» (Tex.RGB→bc.A, SpecAlphaBoost→sbm.A,
#   Tex.A→sbm.B, SpecBase→sadd.A, sbm→sadd.B, bc→em.A, AmbientFloor→em.B), 5 «узел→выход материала»
#   (BaseColor, Roughness, Metallic, Specular, Emissive) и ОДНО ЗАВЕДОМО ПУСТОЕ (см. KNOWN_DANGLING).
#   При изменении wire_char() — обновить.
# ★★★ [ПОРТ] ДЕФЕКТ ГЕНЕРАТОРА, НАМЕРЕННО ВОСПРОИЗВОДИМЫЙ (найден 03.09.2026 проверкой соединений):
#   wire_char() зовёт C(vc,"RGB",bc,"B"), но у MaterialExpressionVertexColor ВСЕ выходы безымянные
#   (MaterialExpressions.cpp:10027-10031: FExpressionOutput(TEXT(""),…)), и ConnectMaterialExpressions
#   (MaterialEditingLibrary.cpp:928-943) по имени "RGB" возвращает False ⇒ вход Multiply.B у BaseColor
#   ВСЕГДА оставался пустым (ConstB=1.0), BaseColor = Tex.RGB, а не Tex·VertexColor. Так живой мастер
#   собран с 31.07.2026 и так выглядят все персонажи в игре. Меши персонажей вершинных цветов не несут
#   (asda_jbm_to_gltf.py не пишет COLOR_0), поэтому подключение дало бы численно то же, но это смена
#   шейдера ВСЕХ персонажей — решение владельца, здесь воспроизводим ТОЧНО текущий подключённый граф.
#   Лечение (когда разрешат): в wire_char() использовать выход "" вместо "RGB" и убрать KNOWN_DANGLING.
KNOWN_DANGLING = {("MaterialExpressionVertexColor", "RGB", "MaterialExpressionMultiply", "B")}
#   ★ Сет C-2: + GEN.NS_LINKS_FULL (34) узел→узел и GEN.NS_OUTPUTS_FULL (4: Normal, AO, Roughness, Emissive — два последних ПЕРЕВЯЗЫВАЮТСЯ
#     с прежних узлов на lerp/Add и считаются повторно) — итого C=41, P=9, dangling=1.
#   ★ Сет D: + GEN.ROUGH_AB_LINKS (6) узел→узел; + рим GEN.RIM_LINKS (21) узел→узел и GEN.RIM_OUTPUTS (1: Emissive перевязан ещё раз) —
#     итого C = 7 + 6 + 34 + 21 = 68, P = 5 + 4 + 1 = 10, dangling = 1.
WIRE_CHAR_LINKS = 7 + GEN.ROUGH_AB_LINKS + GEN.NS_LINKS_FULL + GEN.RIM_LINKS
WIRE_CHAR_DANGLING = 1
WIRE_CHAR_OUTPUTS = 5 + GEN.NS_OUTPUTS_FULL + GEN.RIM_OUTPUTS
# Скаляры мастера и их дефолты по генератору — сверяются С КЭШЕМ АССЕТА после сборки (не с константами).
#   ★ сет C-1 (05.09.2026): Roughness персонажей = GEN.ROUGH_CHAR (0.65; объекты/террейн остаются GEN.ROUGH 0.9) — обоснование в GEN.
WIRE_CHAR_SCALARS = {"Roughness": GEN.ROUGH_CHAR, "Metallic": 0.0, "SpecBase": 0.5,
                     "SpecAlphaBoost": 0.0, "AmbientFloor": GEN.AMBIENT_FLOOR,
                     "RoughHi": GEN.ROUGH_HI, "RoughLo": GEN.ROUGH_LO, "RoughAlphaMix": 0.0}   # ★ сет D: A/B roughness по маске (0 = константа)
WIRE_CHAR_SCALARS.update(GEN.NS_SCALARS)          # ★ сет C-2: HasMaps 0, ClassStrength 1 (дефолты мастера; MIC переопределяют)
# ★ сет C-2: сэмплеры рельефа обязаны иметь дефолт-текстуру НУЖНОГО сжатия (иначе материал молча серый на живом RHI)
WIRE_CHAR_TEXTURES = {"TexN": GEN.NS_DEFAULT_NORMAL, "TexRE": GEN.NS_DEFAULT_RE, "TexC": GEN.NS_DEFAULT_C}
# Свойства мастера, которые обязаны пережить пересборку (usage-флаги нужны в -game: без них чёрный фолбэк).
KEEP_PROPS = ("blend_mode", "shading_model", "two_sided", "used_with_skeletal_mesh",
              "used_with_instanced_static_meshes", "cast_dynamic_shadow_as_masked", "opacity_mask_clip_value")


def log(m): unreal.log_warning("[charlit] %s" % m)


# ★ Соединения С ПРОВЕРКОЙ. GEN.C/GEN.P молча глотают False от connect_material_expressions /
#   connect_material_property (грабля [[ue-material-script-gotchas]] №2: неверное имя входа = тихий
#   обрыв цепочки). На ЖИВОМ мастере всех персонажей тихий обрыв недопустим — подменяем на время
#   wire_char() версиями, которые считают соединения и падают при первом же False.
_links = {"C": 0, "P": 0, "dangling": 0}


def _checked_C(a, ao, b, bi):
    if not MEL.connect_material_expressions(a, ao, b, bi):
        key = (a.get_class().get_name(), ao, b.get_class().get_name(), bi)
        if key in KNOWN_DANGLING:
            _links["dangling"] += 1
            log("вход %s.%s остаётся ПУСТЫМ (%s.%s — известный дефект генератора, воспроизводится намеренно)"
                % (b.get_name(), bi, a.get_name(), ao))
            return
        raise RuntimeError("не соединилось: %s.%s -> %s.%s" % (a.get_name(), ao, b.get_name(), bi))
    _links["C"] += 1


def _checked_P(m, e, eo, prop):
    if not MEL.connect_material_property(e, eo, prop):
        raise RuntimeError("не подключился выход %s <- %s.%s" % (prop, e.get_name(), eo))
    _links["P"] += 1


def snapshot(m, tag):
    log("%s: узлов %d, дубли параметров %s" % (tag, MG.expression_count(m), MG.parameter_duplicates(m) or "нет"))
    log("%s: скаляры=%s текстуры=%s векторы=%s" % (
        tag, [str(n) for n in MEL.get_scalar_parameter_names(m)],
        [str(n) for n in MEL.get_texture_parameter_names(m)],
        [str(n) for n in MEL.get_vector_parameter_names(m)]))
    for p, nm in ((MP.MP_BASE_COLOR, "BaseColor"), (MP.MP_EMISSIVE_COLOR, "Emissive"),
                  (MP.MP_SPECULAR, "Specular"), (MP.MP_ROUGHNESS, "Roughness"), (MP.MP_NORMAL, "Normal"),
                  (MP.MP_AMBIENT_OCCLUSION, "AO"),
                  (MP.MP_METALLIC, "Metallic"), (MP.MP_OPACITY, "Opacity"), (MP.MP_OPACITY_MASK, "OpacityMask")):
        n = MEL.get_material_property_input_node(m, p)
        log("   %-11s = %s" % (nm, n.get_name() if n else "<не подключён>"))
    # Значения — ИЗ КЭША АССЕТА (то, что увидят редактор и 2585 MIC), а не константы скрипта.
    log("   кэш: %s" % ", ".join("%s=%r" % (k, MEL.get_material_default_scalar_parameter_value(m, k))
                                 for k in sorted(WIRE_CHAR_SCALARS)))
    for e in MEL.get_material_expressions(m):      # ★ сет C-2: дефолт-текстуры и тип сэмплера у сэмплеров рельефа — из ассета
        if isinstance(e, unreal.MaterialExpressionTextureSampleParameter2D) and str(e.get_editor_property("parameter_name")) in WIRE_CHAR_TEXTURES:
            t = e.get_editor_property("texture")
            log("   сэмплер %s: texture=%s sampler=%s" % (e.get_editor_property("parameter_name"), t.get_path_name() if t else None, e.get_editor_property("sampler_type")))
    log("   свойства: %s" % ", ".join("%s=%s" % (k, m.get_editor_property(k)) for k in KEEP_PROPS))


def read_props(m):
    return {k: m.get_editor_property(k) for k in KEEP_PROPS}


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    m = EAL.load_asset(PATH)
    if not m:
        raise RuntimeError("нет мастера %s" % PATH)
    snapshot(m, "ДО")
    props_before = read_props(m)
    # ★★★ БАГ UE 5.8 (ревизия 03.09.2026, review_soul_ghost_charselect.md §1/Н2): ОДИН вызов
    #   delete_all_material_expressions удаляет только каждый второй узел (удаление по ходу итерации,
    #   MaterialEditingLibrary.cpp:559-568). Именно так в M_AsdaCharLit появились дубли AmbientFloor×4,
    #   SpecBase×3, Roughness×2 (20 узлов вместо 11): «фантом SpecScale» ниже был тем же багом, а не
    #   «задержкой кэша». Чистим В ЦИКЛЕ до пустого графа.
    MG.clear_expressions(m, log=log, tag="[charlit]")
    # Промежуточный recompile на (теперь действительно) ПУСТОМ графе сбрасывает кэш имён параметров.
    MEL.recompile_material(m)
    # Пересборка графа = ровно то, что строит генератор; соединения — с проверкой (см. _checked_C/_checked_P).
    orig_C, orig_P = GEN.C, GEN.P
    GEN.C, GEN.P = _checked_C, _checked_P
    try:
        GEN.wire_char(m)
    finally:
        GEN.C, GEN.P = orig_C, orig_P
    if (_links["C"], _links["P"], _links["dangling"]) != (WIRE_CHAR_LINKS, WIRE_CHAR_OUTPUTS, WIRE_CHAR_DANGLING):
        raise RuntimeError("соединений %s, ожидалось C=%d P=%d dangling=%d — ассет НЕ сохранён"
                           % (_links, WIRE_CHAR_LINKS, WIRE_CHAR_OUTPUTS, WIRE_CHAR_DANGLING))
    log("соединений: узел→узел %d, узел→выход %d, заведомо пустых %d (= ожидаемым)"
        % (_links["C"], _links["P"], _links["dangling"]))
    # ★ Контроль ДО сохранения: ровно WIRE_CHAR_EXPRESSIONS узлов и ни одного дубля имени параметра;
    #   иначе RuntimeError — ассет на диске остаётся прежним.
    MG.check_graph(m, expected=WIRE_CHAR_EXPRESSIONS, log=log, tag="[charlit]")
    # ★ Дефолты параметров — по КЭШУ АССЕТА (то, что отдадут MIC), и свойства мастера — как были.
    bad = []
    for k, v in WIRE_CHAR_SCALARS.items():
        got = MEL.get_material_default_scalar_parameter_value(m, k)
        if abs(got - v) > 1e-6:
            bad.append("%s: кэш %r ≠ генератор %r" % (k, got, v))
    for k, v in read_props(m).items():
        if v != props_before[k]:
            bad.append("%s: было %s, стало %s" % (k, props_before[k], v))
    # ★ сет C-2: у сэмплеров рельефа дефолт-текстура задана и совпадает по сжатию с типом сэмплера (иначе серый дефолт на живом RHI)
    seen_tex = {}
    for e in MEL.get_material_expressions(m):
        if isinstance(e, unreal.MaterialExpressionTextureSampleParameter2D):
            pn = str(e.get_editor_property("parameter_name"))
            if pn in WIRE_CHAR_TEXTURES:
                t = e.get_editor_property("texture")
                seen_tex[pn] = t.get_path_name().split(".")[0] if t else None
                if seen_tex[pn] != WIRE_CHAR_TEXTURES[pn]:
                    bad.append("сэмплер %s: дефолт %s ≠ %s" % (pn, seen_tex[pn], WIRE_CHAR_TEXTURES[pn]))
    for pn in WIRE_CHAR_TEXTURES:
        if pn not in seen_tex:
            bad.append("нет сэмплера %s" % pn)
    for p, nm in ((MP.MP_NORMAL, "Normal"), (MP.MP_AMBIENT_OCCLUSION, "AO")):
        if not MEL.get_material_property_input_node(m, p):
            bad.append("вход %s пуст" % nm)
    # ★ сет D: Emissive обязан быть суммой рима (Add) — иначе рим не подключён; Roughness — Lerp (выбор константа/маска)
    emn = MEL.get_material_property_input_node(m, MP.MP_EMISSIVE_COLOR)
    if not isinstance(emn, unreal.MaterialExpressionAdd):
        bad.append("вход Emissive %s — не Add (рим не подключён)" % (emn.get_name() if emn else None))
    rn = MEL.get_material_property_input_node(m, MP.MP_ROUGHNESS)
    if not isinstance(rn, unreal.MaterialExpressionLinearInterpolate):
        bad.append("вход Roughness %s — не Lerp" % (rn.get_name() if rn else None))
    if bad:
        raise RuntimeError("контроль после сборки: %s — ассет НЕ сохранён" % "; ".join(bad))
    m.modify()
    ok = EAL.save_loaded_asset(m, False)       # ГРАБЛЯ: правки через MEL не помечают пакет dirty → only_if_is_dirty=False
    log("сохранено: %s" % ok)
    if not ok:
        raise RuntimeError("save_loaded_asset вернул False")
    snapshot(m, "ПОСЛЕ")
    # ASCII-маркер для run_headless_safe.py --marker CHARLIT-FINISHED (кириллица/пробелы в аргументах
    #   Start-Process ломаются кодировкой консоли).
    log("ЗАВЕРШЕНО (CHARLIT-FINISHED)")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
