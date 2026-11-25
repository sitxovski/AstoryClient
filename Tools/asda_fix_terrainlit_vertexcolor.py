# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_terrainlit_vertexcolor.py — ПЕРЕСБОРКА ГРАФА мастера террейна M_AsdaTerrainLit НА МЕСТЕ:
#   вершинный цвет террейна (Д-4 аудита света 2026-09-03) подключается по правилу
#   `Reports/vertex_color_re.md` §5:
#       тинт карты      = VertexColor.rgb / max(r,g,b)   → множитель BaseColor;
#       яркость запечки = max(r,g,b)                     → множитель AmbientOcclusion (× Mask.B).
#   Почему так, а не BaseColor·VertexColor целиком: у оригинала (tTerrain, EmptyProject.fx) вершинный цвет
#   — АВТОМАТИЧЕСКАЯ запечка затенения по уклону (яркость ≈ 0.72 + 0.27·n.y) × один тинт на карту, и он
#   множится на всё после сплата. Под UE-светом тот же «Ламберт сверху» уже даёт DirectionalLight/SkyLight,
#   поэтому яркость в BaseColor = ДВОЙНОЕ затемнение склонов. AO трогает только непрямой свет — там запечке
#   и место; тинт (синева nightvalley, голубизна alpen_dungeon) — данные карты, остаётся в цвете поверхности.
#   Разложение через max-компоненту (а не luma): tint·max == исходный цвет РОВНО (8-битная точность), и
#   тинт никогда не превышает 1 (в luma-варианте синий канал nightvalley вылезал бы за 1).
#
# ★ ГРАБЛЯ (память ue-material-script-gotchas / fix_charlit_lighting): у MaterialExpressionVertexColor ВСЕ
#   выходы безымянные (MaterialExpressions.cpp:10027-10031), `connect_material_expressions(vc,"RGB",…)`
#   молча возвращает False — поэтому прежний мастер (asda_build_materials_lit.build_terrain) вершинный цвет
#   НЕ множил вовсе (вход B у Multiply оставался пустым = 1.0). Выход RGB подключается именем "" (индекс 0);
#   альфа (индекс 4) по имени недостижима — потому яркость берётся из RGB через max, а не из A.
#
# ★ БАГ UE 5.8 delete_all_material_expressions (каждый второй узел выживает) — чистка через asda_mat_graph
#   в цикле + контроль числа узлов и дублей параметров ДО сохранения. Мастер НЕ пересоздаётся (delete_asset
#   обнулил бы parent у MIT_patch_* всех карт и usage-флаги).
#
# ★ AmbientFloor (Emissive = BaseColor·AmbientFloor, asda_add_ambient_floor.py) — ВОСПРОИЗВОДИТСЯ, если
#   параметр есть в мастере на момент запуска, с ЕГО ТЕКУЩИМ значением из кэша ассета; если параметра
#   уже нет (снят другим агентом по Д-3) — не добавляется. Так порядок запуска скриптов не важен.
#
# ★ TerrainAOAmount (сет C-1, 05.09.2026; FINAL_synthesis §4 «Сет C», developer_round2 §7): скаляр-рычаг силы материального AO,
#   AO = lerp(1, Mask.B·max(vtx), TerrainAOAmount), дефолт 0.7. Зачем: материальный AO ПЕРЕМНОЖАЕТСЯ с GTAO (review_setB_prep #8:
#   ScreenSpaceAO × GBufferAO в композите непрямого света), т.е. запечка склонов + запечённый AO плитки + экранный AO дают тройное
#   затемнение стыков/склонов; 0.7 оставляет 70 % материального вклада (1.0 = как было до сета C, 0 = только GTAO). Переопределяется
#   на MIC карты без пересборки. Значение 0.7 — предложение стола, окончательное за владельцем (A/B в setC1_report.md).
#
# Запуск (headless, скрыто): python Tools/run_headless_safe.py Tools/asda_fix_terrainlit_vertexcolor.py --marker "[terrainlit] ГОТОВО"
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import unreal
import asda_mat_graph as MG
import asda_build_materials_lit as GEN      # ★ сет C-2: ветки рельефа NormalStudio (wire_normal_studio), тело GEN под __main__

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty
PATH = "/Game/Materials/M_AsdaTerrainLit"
ROUGH = 0.9                       # как asda_build_materials_lit.ROUGH (дефолт скаляра; MIC могут переопределять)
TERRAIN_AO_AMOUNT = 0.7           # ★ сет C-1: сила материального AO (см. шапку); MIC могут переопределять
KEEP_PROPS = ("blend_mode", "shading_model", "two_sided", "used_with_instanced_static_meshes",
              "used_with_skeletal_mesh", "used_with_static_lighting")
# Ожидаемое число узлов: 13 (сплат: uv, Scale0, Scale1, HasD1, uv0, uv1, Detail0, Detail1, Mask, Colormap,
#   w, g1, g2) + 11 (вершинный цвет: vc, maskR, maskG, maskB, max1, max2, eps, mx, tint, bc, ao)
#   + 2 (Roughness, Metallic) + 2 (сет C-1: TerrainAOAmount, aoLerp) = 28; + 2 при AmbientFloor (floor, em).
#   ★ Сет C-2 (05.09.2026): + GEN.NS_EXPRESSIONS_TERRAIN (20) узлов рельефа NormalStudio: Normal ← TexN (UV сплата uv0), AO ×= lerp(1, TexRE.B),
#     Roughness ← lerp(скаляр, TexRE.R); без Delight/Emissive (сплат детали·colormap не имеет одного альбедо). Карты — по Detail0.
NODES_BASE = 28 + GEN.NS_EXPRESSIONS_TERRAIN
NODES_FLOOR = 2
NS_SCALARS = GEN.NS_SCALARS                      # HasMaps 0 / ClassStrength 1 — дефолты мастера


def log(m): unreal.log_warning("[terrainlit] %s" % m)


_links = {"C": 0, "P": 0}


def E(m, cls, x, y): return MEL.create_material_expression(m, cls, x, y)


def C(a, ao, b, bi):
    """Соединение узел→узел С ПРОВЕРКОЙ: False = неверное имя входа/выхода → RuntimeError, ассет не сохраняем."""
    if not MEL.connect_material_expressions(a, ao, b, bi):
        raise RuntimeError("не соединилось: %s.%r -> %s.%r" % (a.get_name(), ao, b.get_name(), bi))
    _links["C"] += 1


def P(e, eo, prop):
    """Подключение узла к выходу материала С ПРОВЕРКОЙ."""
    if not MEL.connect_material_property(e, eo, prop):
        raise RuntimeError("не подключился выход %s <- %s.%r" % (prop, e.get_name(), eo))
    _links["P"] += 1


def texp(m, nm, x, y):
    t = E(m, unreal.MaterialExpressionTextureSampleParameter2D, x, y); t.set_editor_property("parameter_name", nm); return t


scal = MG.scalar_param   # ScalarParameter nm=v в (x, y) графа m (общая реализация asda_mat_graph; дедуп №2 Т19)


def mask(m, src, r, g, b, x, y):
    """ComponentMask одного канала из вершинного цвета (вход безымянный → "")."""
    k = E(m, unreal.MaterialExpressionComponentMask, x, y)
    k.set_editor_property("r", r); k.set_editor_property("g", g); k.set_editor_property("b", b); k.set_editor_property("a", False)
    C(src, "", k, "")
    return k


def wire_terrain(m, ambient_floor):
    """Полный граф M_AsdaTerrainLit. ambient_floor=None → без Emissive-пола."""
    # --- сплат (1:1 asda_build_materials_lit.build_terrain) ---
    uv = E(m, unreal.MaterialExpressionTextureCoordinate, -1100, 0)
    s0 = scal(m, "Scale0", 8.0, -1100, 200); s1 = scal(m, "Scale1", 8.0, -1100, 320); hasD1 = scal(m, "HasD1", 0.0, -1100, 440)
    uv0 = E(m, unreal.MaterialExpressionMultiply, -900, 100); C(uv, "", uv0, "A"); C(s0, "", uv0, "B")
    uv1 = E(m, unreal.MaterialExpressionMultiply, -900, 250); C(uv, "", uv1, "A"); C(s1, "", uv1, "B")
    d0 = texp(m, "Detail0", -650, -300); C(uv0, "", d0, "UVs")
    d1 = texp(m, "Detail1", -650, -50);  C(uv1, "", d1, "UVs")
    mk = texp(m, "Mask", -650, 220);     C(uv, "", mk, "UVs")
    cm = texp(m, "Colormap", -650, 480); C(uv, "", cm, "UVs")
    # ★ 07.09.2026 (швы террейна, terrain_seams_report.md): Mask/Colormap — текстуры НА ПАТЧ (256², UV ровно 0..1).
    #   С адресацией Wrap билинейный фильтр на границе патча смешивает texel 0 с texel 255 ПРОТИВОПОЛОЖНОГО края той же
    #   текстуры → линия в 1 тексель (25 юн) вдоль всей сетки патчей 6400 юн, видна и в unlit. [ОРИГИНАЛ] CD3DManager_Init
    #   (render_core.c:40857-40860) ставит стадиям 2/3 D3DTADDRESS_CLAMP, а CTerrain_SetupRenderState (world_map.c:13343)
    #   вешает на них texcoord 1 = поплиточные alpha/color; стадии 0/1 (detail-тайлы) — Wrap. Повторяем: Clamp у Mask и
    #   Colormap, Detail0/Detail1 остаются Wrap (тайлинг uv·Scale).
    for t in (mk, cm):
        t.set_editor_property("sampler_source", unreal.SamplerSourceMode.SSM_CLAMP_WORLD_GROUP_SETTINGS)
    w = E(m, unreal.MaterialExpressionMultiply, -350, 250); C(mk, "A", w, "A"); C(hasD1, "", w, "B")
    g1 = E(m, unreal.MaterialExpressionLinearInterpolate, -150, -100); C(d0, "RGB", g1, "A"); C(d1, "RGB", g1, "B"); C(w, "", g1, "Alpha")
    g2 = E(m, unreal.MaterialExpressionLinearInterpolate, 80, 50); C(g1, "", g2, "A"); C(cm, "RGB", g2, "B"); C(cm, "A", g2, "Alpha")
    # --- вершинный цвет террейна: тинт → BaseColor, яркость → AO ---
    vc = E(m, unreal.MaterialExpressionVertexColor, -650, 700)
    mR = mask(m, vc, True, False, False, -450, 640)
    mG = mask(m, vc, False, True, False, -450, 720)
    mB = mask(m, vc, False, False, True, -450, 800)
    max1 = E(m, unreal.MaterialExpressionMax, -280, 680); C(mR, "", max1, "A"); C(mG, "", max1, "B")
    max2 = E(m, unreal.MaterialExpressionMax, -130, 720); C(max1, "", max2, "A"); C(mB, "", max2, "B")
    eps = E(m, unreal.MaterialExpressionConstant, -280, 860); eps.set_editor_property("r", 1.0 / 255.0)   # защита от деления на 0 (чёрная вершина)
    mx = E(m, unreal.MaterialExpressionMax, 20, 760); C(max2, "", mx, "A"); C(eps, "", mx, "B")        # яркость запечки = max(r,g,b)
    tint = E(m, unreal.MaterialExpressionDivide, 180, 660); C(vc, "", tint, "A"); C(mx, "", tint, "B")   # тинт = rgb / max (макс-компонента = 1)
    bc = E(m, unreal.MaterialExpressionMultiply, 320, 100); C(g2, "", bc, "A"); C(tint, "", bc, "B")     # BaseColor = сплат · тинт
    ao = E(m, unreal.MaterialExpressionMultiply, 320, 500); C(mk, "B", ao, "A"); C(mx, "", ao, "B")      # AO = запечённый AO плитки · яркость запечки
    # ★ сет C-1: AO_out = lerp(1.0, ao, TerrainAOAmount) — вход A у Lerp НЕ подключён и берётся из const_a = 1.0
    #   (у LinearInterpolate дефолт const_a = 0 → без явной единицы при Amount=0 AO стал бы 0 = чёрные стыки).
    aoAmt = scal(m, "TerrainAOAmount", TERRAIN_AO_AMOUNT, 320, 600)
    aoL = E(m, unreal.MaterialExpressionLinearInterpolate, 550, 520); aoL.set_editor_property("const_a", 1.0)
    C(ao, "", aoL, "B"); C(aoAmt, "", aoL, "Alpha")
    rough = scal(m, "Roughness", ROUGH, 320, 300); metal = scal(m, "Metallic", 0.0, 320, 420)
    P(bc, "", MP.MP_BASE_COLOR)
    P(aoL, "", MP.MP_AMBIENT_OCCLUSION)
    P(rough, "", MP.MP_ROUGHNESS)
    P(metal, "", MP.MP_METALLIC)
    if ambient_floor is not None:
        floor = scal(m, "AmbientFloor", ambient_floor, 550, 400)
        em = E(m, unreal.MaterialExpressionMultiply, 700, 300); C(bc, "", em, "A"); C(floor, "", em, "B")
        P(em, "", MP.MP_EMISSIVE_COLOR)
    # ★ сет C-2: рельеф NormalStudio поверх — сэмплеры карт на UV детали 0 (uv0), AO = aoL × lerp(1, TexRE.B, amt),
    #   Roughness = lerp(rough, TexRE.R, amt); соединения — ПРОВЕРЯЮЩИЕ C/P этого скрипта (адаптер сигнатуры P).
    GEN.wire_normal_studio(m, tex=d0, bc=None, rough=rough, em=None, uv=uv0, ao_in=aoL, with_delight=False, with_emissive=False,
                           C=C, P=lambda _m, e, eo, prop: P(e, eo, prop), x0=-2400, y0=1100)


def snapshot(m, tag):
    log("%s: узлов %d, дубли параметров %s" % (tag, MG.expression_count(m), MG.parameter_duplicates(m) or "нет"))
    log("%s: скаляры=%s текстуры=%s" % (tag, [str(n) for n in MEL.get_scalar_parameter_names(m)],
                                        [str(n) for n in MEL.get_texture_parameter_names(m)]))
    for p, nm in ((MP.MP_BASE_COLOR, "BaseColor"), (MP.MP_AMBIENT_OCCLUSION, "AO"), (MP.MP_EMISSIVE_COLOR, "Emissive"),
                  (MP.MP_ROUGHNESS, "Roughness"), (MP.MP_METALLIC, "Metallic"), (MP.MP_NORMAL, "Normal")):
        n = MEL.get_material_property_input_node(m, p)
        log("   %-9s = %s" % (nm, n.get_name() if n else "<не подключён>"))
    log("   свойства: %s" % ", ".join("%s=%s" % (k, m.get_editor_property(k)) for k in KEEP_PROPS))
    # Значения — ИЗ КЭША АССЕТА (то, что отдадут MIC), не константы скрипта.
    have = [str(n) for n in MEL.get_scalar_parameter_names(m)]
    log("   кэш: %s" % ", ".join("%s=%r" % (k, MEL.get_material_default_scalar_parameter_value(m, k))
                                 for k in ("Roughness", "Metallic", "TerrainAOAmount", "AmbientFloor", "HasMaps", "ClassStrength") if k in have))
    for e in MEL.get_material_expressions(m):      # ★ сет C-2: дефолт-текстуры сэмплеров рельефа — из ассета
        if isinstance(e, unreal.MaterialExpressionTextureSampleParameter2D) and str(e.get_editor_property("parameter_name")) in ("TexN", "TexRE"):
            t = e.get_editor_property("texture")
            log("   сэмплер %s: texture=%s sampler=%s" % (e.get_editor_property("parameter_name"), t.get_path_name() if t else None, e.get_editor_property("sampler_type")))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    m = EAL.load_asset(PATH)
    if not m:
        raise RuntimeError("нет мастера %s" % PATH)
    snapshot(m, "ДО")
    props_before = {k: m.get_editor_property(k) for k in KEEP_PROPS}
    scalars = [str(n) for n in MEL.get_scalar_parameter_names(m)]
    floor = MEL.get_material_default_scalar_parameter_value(m, "AmbientFloor") if "AmbientFloor" in scalars else None
    log("AmbientFloor в мастере: %s" % ("нет — не воспроизводится" if floor is None else "%.3f — воспроизводится" % floor))
    MG.clear_expressions(m, log=log, tag="[terrainlit]")
    MEL.recompile_material(m)          # сброс кэша имён параметров на действительно пустом графе
    wire_terrain(m, floor)
    expected = NODES_BASE + (NODES_FLOOR if floor is not None else 0)
    MG.check_graph(m, expected=expected, log=log, tag="[terrainlit]")
    # Свойства мастера (шейдинг/двусторонность/usage) пережить обязаны — граф их не трогает, но проверяем.
    for k, v in props_before.items():
        if m.get_editor_property(k) != v:
            raise RuntimeError("свойство %s изменилось: %s → %s — ассет НЕ сохранён" % (k, v, m.get_editor_property(k)))
    # Входы материала после сборки: BaseColor, AO и (сет C-2) Normal обязаны быть подключены.
    for p, nm in ((MP.MP_BASE_COLOR, "BaseColor"), (MP.MP_AMBIENT_OCCLUSION, "AO"), (MP.MP_NORMAL, "Normal")):
        if not MEL.get_material_property_input_node(m, p):
            raise RuntimeError("выход %s не подключён — ассет НЕ сохранён" % nm)
    # ★ сет C-2: дефолты рельефа и дефолт-текстуры сэмплеров — из ассета
    for k, v in NS_SCALARS.items():
        got = MEL.get_material_default_scalar_parameter_value(m, k)
        if abs(got - v) > 1e-6:
            raise RuntimeError("%s в кэше %r ≠ генератор %r — ассет НЕ сохранён" % (k, got, v))
    want_tex = {"TexN": GEN.NS_DEFAULT_NORMAL, "TexRE": GEN.NS_DEFAULT_RE}
    for e in MEL.get_material_expressions(m):
        if isinstance(e, unreal.MaterialExpressionTextureSampleParameter2D):
            pn = str(e.get_editor_property("parameter_name"))
            if pn in want_tex:
                t = e.get_editor_property("texture")
                if not t or t.get_path_name().split(".")[0] != want_tex[pn]:
                    raise RuntimeError("сэмплер %s: дефолт %s ≠ %s — ассет НЕ сохранён" % (pn, t.get_path_name() if t else None, want_tex[pn]))
                want_tex.pop(pn)
    if want_tex:
        raise RuntimeError("нет сэмплеров %s — ассет НЕ сохранён" % list(want_tex))
    # ★ сет C-1: дефолт TerrainAOAmount — по КЭШУ АССЕТА (а не константе скрипта), иначе не сохраняем.
    MEL.recompile_material(m)
    got = MEL.get_material_default_scalar_parameter_value(m, "TerrainAOAmount")
    if abs(got - TERRAIN_AO_AMOUNT) > 1e-6:
        raise RuntimeError("TerrainAOAmount в кэше %r ≠ генератор %r — ассет НЕ сохранён" % (got, TERRAIN_AO_AMOUNT))
    MEL.recompile_material(m)
    m.modify()
    ok = EAL.save_loaded_asset(m)
    snapshot(m, "ПОСЛЕ")
    log("соединений: узел→узел %d, узел→выход %d; save=%s" % (_links["C"], _links["P"], ok))
    if not ok:
        raise RuntimeError("ассет не сохранился")
    unreal.log("[terrainlit] ГОТОВО: M_AsdaTerrainLit — VertexColor: тинт→BaseColor, max(rgb)→AO, AO=lerp(1,AO,TerrainAOAmount=%r); узлов %d"
               % (MEL.get_material_default_scalar_parameter_value(m, "TerrainAOAmount"), expected))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
