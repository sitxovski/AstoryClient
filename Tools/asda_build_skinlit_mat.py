# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_skinlit_mat.py — SSS-МАСТЕР ЛИЦА M_AsdaSkinLit (сет C-1 «материалы поверхностей», 05.09.2026;
#   FINAL_synthesis §4 «Сет C», developer_round2 §4) + репарент MIC лица по критерию кода.
#
# КОМУ: только MIC ЛИЦА базового скелета игрока. Критерий — ровно тот, по которому C++ ищет секцию лица
#   (Source/AsdaReborn/Private/Characters/AsdaCharAssembly.cpp:120: M->GetName().Contains("face")): класс
#   MaterialInstanceConstant под /Game/AsdaAssets/Models, имя содержит "face" (без учёта регистра), parent = M_AsdaCharLit.
#   ApplyFace меняет ТОЛЬКО параметр "Tex" через MID → любой родитель с сэмплером Tex совместим.
#   Волосы (198 MIC hair*) — НЕ SSS (developer_round2 §4: DefaultLit, MIC-оверрайд Roughness); кожа рук/ног нарисована на
#   текстурах chst/pant/shoe без маски — раздать SSS «только коже» невозможно без выдумки; NPC/мобы — один атлас; не раздаём.
#
# ГРАФ: копия живого M_AsdaCharLit (GEN.wire_char — ЕДИНЫЙ источник: Tex, VertexColor, BaseColor, Roughness, Metallic,
#   SpecBase/SpecAlphaBoost/Specular, AmbientFloor/Emissive = 11 узлов) + ПОВЕРХ:
#     • shading_model = MSM_SUBSURFACE (без Subsurface Profile: у профиля свой экранный проход и отдельный ассет; для семи MIC
#       и стилизованного лица достаточно wrap-SSS модели Subsurface — решение можно пересмотреть за владельцем);
#     • SubsurfaceColor = BaseColor × VectorParameter SubsurfaceTint (дефолт SSS_TINT — тёплый тон «под кожей»; значение —
#       предложение, за владельцем);
#     • Opacity = ScalarParameter SSSAmount (у Subsurface вход Opacity = сила просвета; дефолт 0.7 по developer_round2 §4).
#   Итого 11 + 3 = 14 узлов; ★ сет C-2 (05.09.2026): + 28 узлов рельефа NormalStudio внутри wire_char ⇒ 42. Blend Opaque, two_sided/usage-флаги — как у мастера-источника (копия), проверяются из ассета.
#
# ★★★ КОНТРОЛЬ ГРАФА (asda_mat_graph.py; баг UE 5.8 DeleteAllMaterialExpressions — каждый второй узел выживает):
#   мастер-источник обязан быть чистым (ровно WIRE_CHAR_EXPRESSIONS узлов), копия чистится В ЦИКЛЕ до нуля и собирается заново
#   тем же wire_char() (соединения с проверкой, как в asda_fix_charlit_lighting.py), ПОСЛЕ — ровно EXPECTED_EXPRESSIONS узлов и ни
#   одного дубля, иначе RuntimeError и ассет не сохраняется. Итог печатается ИЗ АССЕТА (кэш параметров, флаги, входы).
#   Если M_AsdaSkinLit уже есть — он НЕ удаляется (delete_asset обнулил бы parent у уже репарентнутых MIC), а пересобирается на месте.
#
# РЕЖИМЫ (env ASDA_SKIN_MODE): build (умолч.) — собрать/пересобрать мастер; reparent — MIC лица M_AsdaCharLit → M_AsdaSkinLit;
#   revert — обратно M_AsdaSkinLit → M_AsdaCharLit; audit — только перечислить кандидатов и детей мастера (ничего не меняет).
#   Каждый режим печатает реестр: сколько MIC под M_AsdaSkinLit (по тегу Parent реестра ассетов, без загрузки) — приёмка «ровно лица».
# Запуск (СКРЫТО, редактор закрыт, ПОСЛЕ asda_fix_charlit_lighting.py; MIC-файлы лица должны быть записываемыми):
#   python Tools/run_headless_safe.py Tools/asda_build_skinlit_mat.py --marker SKINLIT-FINISHED      (ASDA_SKIN_MODE=build|reparent|revert|audit)
import os, sys
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_build_materials_lit as GEN                                          # wire_char — единый источник графа персонажа
import asda_mat_graph as MG                                                     # чистка в цикле + контроль узлов/дублей
from asda_fix_charlit_lighting import WIRE_CHAR_EXPRESSIONS, KNOWN_DANGLING, WIRE_CHAR_LINKS, WIRE_CHAR_OUTPUTS   # 39 узлов (сет C-2: 11 + 28 рельеф); заведомо пустой вход VertexColor→BaseColor

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty
AR = unreal.AssetRegistryHelpers.get_asset_registry()
PKG = "/Game/Materials"
SRC = "%s/M_AsdaCharLit" % PKG
NAME = "M_AsdaSkinLit"
DST = "%s/%s" % (PKG, NAME)
MODELS_ROOT = "/Game/AsdaAssets/Models"
FACE_KEY = "face"                 # критерий AsdaCharAssembly.cpp:120 (Contains, регистр у нас игнорируется — имена MIC в нижнем)
SSS_AMOUNT = 0.7                  # Opacity у Subsurface = сила просвета (developer_round2 §4); за владельцем
SSS_TINT = (1.0, 0.62, 0.50)      # SubsurfaceColor = BaseColor × тинт; предложение, за владельцем (только режим wrap)
# ★★★ Сет D (05.09.2026; review_setC1.md ВАЖНО №4, developer_round2 §4): режим SSS — env ASDA_SKIN_SSS:
#   ★ Умолчание = `off` (финальная доводка 05.09, review_setD.md ВАЖНО №3): на диске M_AsdaSkinLit = DefaultLit без SSS (сет D снял его —
#     профиль ≈ DefaultLit по кадрам, 100.0/76.3 vs 100.5/76.4); прежний дефолт `profile` при прогоне без env молча возвращал бы профиль + SP_AsdaSkin.
#   profile          — MSM_SUBSURFACE_PROFILE + ассет SP_AsdaSkin (Burley, радиус SP_RADIUS_CM см; экранный проход, энергия сохраняется —
#                      лицо в тени НЕ светлее тела, в отличие от wrap-Subsurface, который «досвечивал» лицо (+15 ед. на кадре C-1));
#                      Opacity = SSSAmount (у профиля — маска силы SSS); SubsurfaceTint/Multiply не нужны (вход SubsurfaceColor профилем не читается);
#   wrap             — прежний C-1: MSM_SUBSURFACE, SubsurfaceColor = BaseColor × SubsurfaceTint, Opacity = SSSAmount;
#   off              — SSS снят: DefaultLit, тот же roughness (граф = wire_char без добавок) — если профиль не согласует яркость лица и тела.
SSS_MODE = os.environ.get("ASDA_SKIN_SSS", "off").lower()      # ★ дефолт = состояние на диске (off), см. выше
SP_NAME = "SP_AsdaSkin"
SP_PATH = "%s/%s" % (PKG, SP_NAME)
SP_RADIUS_CM = 1.2                # developer_round2 §4: 1.0–1.2 см (мировые единицы проекта = см; WorldUnitScale 1)
SKIN_EXTRA_EXPRESSIONS = {"profile": 1, "wrap": 3, "off": 0}[SSS_MODE if SSS_MODE in ("profile", "wrap", "off") else "off"]
EXPECTED_EXPRESSIONS = WIRE_CHAR_EXPRESSIONS + SKIN_EXTRA_EXPRESSIONS   # off (умолч.): 64 + 0 = 64; profile 65; wrap 67 (WIRE_CHAR = 16 + NS 28 + rim 20)
KEEP_PROPS = ("blend_mode", "two_sided", "used_with_skeletal_mesh", "used_with_instanced_static_meshes",
              "cast_dynamic_shadow_as_masked")
MODE = os.environ.get("ASDA_SKIN_MODE", "build").lower()


def log(m): unreal.log_warning("[skinlit] %s" % m)


_links = {"C": 0, "P": 0, "dangling": 0}


def _checked_C(a, ao, b, bi):
    if not MEL.connect_material_expressions(a, ao, b, bi):
        key = (a.get_class().get_name(), ao, b.get_class().get_name(), bi)
        if key in KNOWN_DANGLING:
            _links["dangling"] += 1
            log("вход %s.%s остаётся ПУСТЫМ (известный дефект генератора, воспроизводится как у M_AsdaCharLit)" % (b.get_name(), bi))
            return
        raise RuntimeError("не соединилось: %s.%s -> %s.%s" % (a.get_name(), ao, b.get_name(), bi))
    _links["C"] += 1


def _checked_P(m, e, eo, prop):
    if not MEL.connect_material_property(e, eo, prop):
        raise RuntimeError("не подключился выход %s <- %s.%s" % (prop, e.get_name(), eo))
    _links["P"] += 1


def parent_path_of(ad):
    """Путь родителя MIC из тега реестра (без загрузки): "/Script/Engine.Material'/Game/…/M_X.M_X'" → "/Game/…/M_X"."""
    try:
        v = str(ad.get_tag_value("Parent"))
    except Exception:
        return ""
    if "'" in v:
        v = v.split("'")[1]
    return v.split(".")[0] if "." in v else v


def all_mics(root):
    return [ad for ad in AR.get_assets_by_path(root, recursive=True)
            if str(ad.asset_class_path.asset_name) == "MaterialInstanceConstant"]


def face_candidates():
    """MIC под Models с "face" в имени — кандидаты по критерию кода; вместе с родителем (из тега)."""
    out = []
    for ad in all_mics(MODELS_ROOT):
        nm = str(ad.asset_name)
        if FACE_KEY in nm.lower():
            out.append((str(ad.package_name), nm, parent_path_of(ad)))
    return sorted(out)


def registry_children(master_path, root="/Game"):
    """Все MIC проекта с parent == master_path (по тегу реестра)."""
    return sorted(str(ad.package_name) for ad in all_mics(root) if parent_path_of(ad) == master_path)


def snapshot(m, tag):
    log("%s: узлов %d, дубли параметров %s" % (tag, MG.expression_count(m), MG.parameter_duplicates(m) or "нет"))
    log("%s: скаляры=%s векторы=%s текстуры=%s" % (
        tag, [str(n) for n in MEL.get_scalar_parameter_names(m)], [str(n) for n in MEL.get_vector_parameter_names(m)],
        [str(n) for n in MEL.get_texture_parameter_names(m)]))
    for p, nm in ((MP.MP_BASE_COLOR, "BaseColor"), (MP.MP_SUBSURFACE_COLOR, "SubsurfaceColor"), (MP.MP_OPACITY, "Opacity"),
                  (MP.MP_ROUGHNESS, "Roughness"), (MP.MP_SPECULAR, "Specular"), (MP.MP_EMISSIVE_COLOR, "Emissive")):
        n = MEL.get_material_property_input_node(m, p)
        log("   %-15s = %s" % (nm, n.get_name() if n else "<не подключён>"))
    have = [str(n) for n in MEL.get_scalar_parameter_names(m)]
    log("   кэш скаляров: %s" % ", ".join("%s=%r" % (k, MEL.get_material_default_scalar_parameter_value(m, k))
                                          for k in ("Roughness", "Metallic", "SpecBase", "SpecAlphaBoost", "AmbientFloor", "SSSAmount") if k in have))
    if "SubsurfaceTint" in [str(n) for n in MEL.get_vector_parameter_names(m)]:
        log("   кэш SubsurfaceTint = %s" % MEL.get_material_default_vector_parameter_value(m, "SubsurfaceTint"))
    sp = m.get_editor_property("subsurface_profile")
    log("   shading_model=%s, subsurface_profile=%s, свойства: %s" % (m.get_editor_property("shading_model"), sp.get_path_name() if sp else None,
                                              ", ".join("%s=%s" % (k, m.get_editor_property(k)) for k in KEEP_PROPS)))
    if sp:
        st = sp.get_editor_property("settings")
        log("   профиль ИЗ АССЕТА: burley=%s scatter_radius=%s mfp_distance=%s world_unit_scale=%s surface_albedo=%s mfp_color=%s subsurface_color=%s falloff=%s" % (
            st.get_editor_property("enable_burley"), st.get_editor_property("scatter_radius"), st.get_editor_property("mean_free_path_distance"),
            st.get_editor_property("world_unit_scale"), st.get_editor_property("surface_albedo"), st.get_editor_property("mean_free_path_color"),
            st.get_editor_property("subsurface_color"), st.get_editor_property("falloff_color")))


def ensure_profile():
    """Ассет профиля SP_AsdaSkin (USubsurfaceProfile). Идемпотентно; параметры печатаются ИЗ АССЕТА. check-existing: find Content -iname "SP_*" = 0 до сета D."""
    if not EAL.does_asset_exist(SP_PATH):
        fac = getattr(unreal, "SubsurfaceProfileFactory", None)
        if fac is None:
            raise RuntimeError("в Python нет unreal.SubsurfaceProfileFactory — профиль создать нельзя (нужен режим ASDA_SKIN_SSS=wrap|off)")
        sp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(SP_NAME, PKG, unreal.SubsurfaceProfile, fac())
        if not sp:
            raise RuntimeError("create_asset %s вернул None" % SP_PATH)
        log("создан профиль %s" % SP_PATH)
    sp = EAL.load_asset(SP_PATH)
    st = sp.get_editor_property("settings")
    # Burley (физическая модель 5.x): радиус/длина пробега в СМ (мировые единицы проекта); цвета — движковые дефолты (настроены на кожу),
    #   без «воска»: радиус мал (1.2 см на голове ~25 см). Значения — предложение, за владельцем.
    st.set_editor_property("enable_burley", True)
    st.set_editor_property("world_unit_scale", 1.0)
    st.set_editor_property("scatter_radius", SP_RADIUS_CM)
    st.set_editor_property("mean_free_path_distance", SP_RADIUS_CM)
    sp.set_editor_property("settings", st)
    sp.modify()
    if not EAL.save_loaded_asset(sp, False):
        raise RuntimeError("не сохранился %s" % SP_PATH)
    return sp


def build():
    src = EAL.load_asset(SRC)
    if not src:
        raise RuntimeError("нет мастера %s" % SRC)
    log("источник %s: узлов %d, дубли %s" % (SRC, MG.expression_count(src), MG.parameter_duplicates(src) or "нет"))
    MG.check_graph(src, expected=WIRE_CHAR_EXPRESSIONS, log=log, tag="[skinlit] источник")
    if EAL.does_asset_exist(DST):
        log("%s уже есть — пересборка НА МЕСТЕ (без delete_asset: parent у MIC лица должен пережить)" % DST)
        m = EAL.load_asset(DST)
    else:
        if not EAL.duplicate_asset(SRC, DST):
            raise RuntimeError("duplicate_asset %s -> %s вернул None" % (SRC, DST))
        m = EAL.load_asset(DST)
        log("создан дубликат %s" % DST)
    if not isinstance(m, unreal.Material):
        raise RuntimeError("%s не Material: %r" % (DST, m))
    props_src = {k: src.get_editor_property(k) for k in KEEP_PROPS}
    snapshot(m, "ДО")
    # ── граф: чистка в цикле → тот же wire_char(), что у M_AsdaCharLit (соединения с проверкой) ──
    MG.clear_expressions(m, log=log, tag="[skinlit]")
    MEL.recompile_material(m)
    orig_C, orig_P = GEN.C, GEN.P
    GEN.C, GEN.P = _checked_C, _checked_P
    try:
        GEN.wire_char(m)
    finally:
        GEN.C, GEN.P = orig_C, orig_P
    log("соединений wire_char: узел→узел %d, узел→выход %d, заведомо пустых %d" % (_links["C"], _links["P"], _links["dangling"]))
    if _links["P"] != WIRE_CHAR_OUTPUTS or _links["C"] != WIRE_CHAR_LINKS:   # ★ сет C-2: 41/9 (7/5 + ветки рельефа 34/4)
        raise RuntimeError("wire_char дал соединений %s, ожидалось C=%d P=%d — ассет НЕ сохранён" % (_links, WIRE_CHAR_LINKS, WIRE_CHAR_OUTPUTS))
    bc = MEL.get_material_property_input_node(m, MP.MP_BASE_COLOR)
    if not bc:
        raise RuntimeError("после wire_char вход BaseColor пуст — ассет НЕ сохранён")
    # ── SSS поверх (сет D: режим ASDA_SKIN_SSS) ──
    log("режим SSS: %s" % SSS_MODE)
    want_sm = {"profile": unreal.MaterialShadingModel.MSM_SUBSURFACE_PROFILE, "wrap": unreal.MaterialShadingModel.MSM_SUBSURFACE,
               "off": unreal.MaterialShadingModel.MSM_DEFAULT_LIT}[SSS_MODE]
    m.set_editor_property("shading_model", want_sm)
    want_inputs = []
    if SSS_MODE == "profile":
        sp = ensure_profile()
        m.set_editor_property("subsurface_profile", sp)
        amt = MEL.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -300, 850)
        amt.set_editor_property("parameter_name", "SSSAmount")
        amt.set_editor_property("default_value", SSS_AMOUNT)
        _checked_P(m, amt, "", MP.MP_OPACITY)              # у профиля Opacity = маска силы SSS
        want_inputs = [(MP.MP_OPACITY, "Opacity")]
    elif SSS_MODE == "wrap":
        m.set_editor_property("subsurface_profile", None)
        tint = MEL.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -300, 700)
        tint.set_editor_property("parameter_name", "SubsurfaceTint")
        tint.set_editor_property("default_value", unreal.LinearColor(SSS_TINT[0], SSS_TINT[1], SSS_TINT[2], 1.0))
        ssc = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, 0, 650)
        _checked_C(bc, "", ssc, "A")
        _checked_C(tint, "", ssc, "B")
        amt = MEL.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -300, 850)
        amt.set_editor_property("parameter_name", "SSSAmount")
        amt.set_editor_property("default_value", SSS_AMOUNT)
        _checked_P(m, ssc, "", MP.MP_SUBSURFACE_COLOR)
        _checked_P(m, amt, "", MP.MP_OPACITY)
        want_inputs = [(MP.MP_SUBSURFACE_COLOR, "SubsurfaceColor"), (MP.MP_OPACITY, "Opacity")]
    else:
        m.set_editor_property("subsurface_profile", None)
    # ── контроль ДО сохранения ──
    MG.check_graph(m, expected=EXPECTED_EXPRESSIONS, log=log, tag="[skinlit]")
    MEL.recompile_material(m)
    bad = []
    for k, v in props_src.items():
        if m.get_editor_property(k) != v:
            bad.append("%s: у источника %s, у копии %s" % (k, v, m.get_editor_property(k)))
    if m.get_editor_property("shading_model") != want_sm:
        bad.append("shading_model %s ≠ %s" % (m.get_editor_property("shading_model"), want_sm))
    if SSS_MODE == "profile":
        spv = m.get_editor_property("subsurface_profile")
        if not spv or spv.get_path_name().split(".")[0] != SP_PATH:
            bad.append("subsurface_profile %s ≠ %s" % (spv.get_path_name() if spv else None, SP_PATH))
    if SSS_MODE != "off":
        got = MEL.get_material_default_scalar_parameter_value(m, "SSSAmount")
        if abs(got - SSS_AMOUNT) > 1e-6:
            bad.append("SSSAmount кэш %r ≠ %r" % (got, SSS_AMOUNT))
    got_r = MEL.get_material_default_scalar_parameter_value(m, "Roughness")
    if abs(got_r - GEN.ROUGH_CHAR) > 1e-6:
        bad.append("Roughness кэш %r ≠ GEN.ROUGH_CHAR %r" % (got_r, GEN.ROUGH_CHAR))
    for p, nm in want_inputs:
        if not MEL.get_material_property_input_node(m, p):
            bad.append("вход %s пуст" % nm)
    if bad:
        raise RuntimeError("контроль после сборки: %s — ассет НЕ сохранён" % "; ".join(bad))
    m.modify()
    ok = EAL.save_loaded_asset(m, False)
    if not ok:
        raise RuntimeError("save_loaded_asset вернул False")
    snapshot(m, "ПОСЛЕ")
    log("сохранено %s: save=%s" % (DST, ok))


def reparent(to_skin):
    if not EAL.does_asset_exist(DST):
        raise RuntimeError("нет %s — сначала ASDA_SKIN_MODE=build" % DST)
    frm, to = (SRC, DST) if to_skin else (DST, SRC)
    target = EAL.load_asset(to)
    n_done, n_skip = 0, 0
    for pkg, nm, par in face_candidates():
        if par != frm:
            log("пропуск %s (parent %s)" % (pkg, par or "?"))
            n_skip += 1
            continue
        mic = EAL.load_asset(pkg)
        if not mic:
            raise RuntimeError("не загрузился %s" % pkg)
        mic.set_editor_property("parent", target)
        mic.modify()
        if not EAL.save_loaded_asset(mic, False):
            raise RuntimeError("не сохранился %s (файл read-only?)" % pkg)
        # контроль ИЗ АССЕТА
        p2 = mic.get_editor_property("parent")
        log("REPARENT %s: %s -> %s (в ассете parent=%s)" % (pkg, frm.rsplit("/", 1)[-1], to.rsplit("/", 1)[-1], p2.get_name() if p2 else None))
        n_done += 1
    log("репарент %s: сделано %d, пропущено %d" % ("→ SkinLit" if to_skin else "→ CharLit", n_done, n_skip))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал (закон [[tools-no-import-side-effects]])."""
    log("режим %s" % MODE)
    if MODE == "build":
        build()
    elif MODE == "reparent":
        reparent(True)
    elif MODE == "revert":
        reparent(False)
    elif MODE != "audit":
        raise RuntimeError("неизвестный ASDA_SKIN_MODE=%s" % MODE)
    # ── реестр (любой режим): кандидаты по критерию и фактические дети мастера ──
    cands = face_candidates()
    log("кандидаты по критерию '%s' под %s: %d" % (FACE_KEY, MODELS_ROOT, len(cands)))
    for pkg, nm, par in cands:
        log("   %s  parent=%s" % (pkg, par.rsplit("/", 1)[-1] if par else "?"))
    if EAL.does_asset_exist(DST):
        kids = registry_children(DST)
        log("MIC под %s (реестр, весь /Game): %d — %s" % (NAME, len(kids), kids))
        bad = [k for k in kids if FACE_KEY not in k.rsplit("/", 1)[-1].lower()]
        if bad:
            log("!!! под %s есть MIC НЕ лица: %s" % (NAME, bad))
    log("ЗАВЕРШЕНО (SKINLIT-FINISHED)")   # ASCII-маркер для run_headless_safe.py --marker SKINLIT-FINISHED


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
