# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_materials_lit.py — LIT-версии мастер-материалов (для перехода на реальный UE-свет).
#   M_AsdaTerrainLit  = BaseColor(detail·colormap·vtxColor), AO=mask.b, Metallic=0, Roughness=ROUGH (Default Lit).
#   M_AsdaObjectLit   = BaseColor(tex·vtxColor), Opacity mask=tex.a, Metallic=0, Roughness=ROUGH (Masked, Lit).
#   M_AsdaCharLit     = ПЕРСОНАЖ/ЭКИП/ОРУЖИЕ: НЕПРОЗРАЧНЫЙ, альфа текстуры → Specular (маска блика).
#     ЭТАЛОН system/EmptyProject.fx, техники tSkin/tRigid → PShade():
#        Output.RGBColor.w = (1 * (In.fog*4.0f)) * (NoSkinTexScroll.w*0.25f);   ← Tex.w в альфу НЕ идёт
#        ... + ((vSpecular.xyz * Tex.w) * ShadowAlpha.z);  += texCUBE(...)*(Tex.w*0.3f)*ShadowAlpha.z
#     т.е. альфа = МАСКА БЛЕСКА/ОТРАЖЕНИЯ, а не прозрачность (у tNoSkin — наоборот, см. asda_material_policy.py).
#   БЕЗ рантайм-света (tex — чистое альбедо, colormap не лайтмап — проверено) → светит реальный DirectionalLight+SkyLight.
#   Все параметры (Roughness/Metallic/Specular) — редактируемые скаляры (полный контроль в редакторе).
#   ★ Сет C-2 (05.09.2026): ветки рельефа NormalStudio — wire_normal_studio() (см. ниже) вплетены В ГЕНЕРАТОРЫ
#     (wire_char / wire_object / asda_fix_terrainlit_vertexcolor.wire_terrain), а не патчем поверх: fix-скрипты ждут ровно N узлов
#     и чистят граф до нуля — патч поверх снесло бы следующим же прогоном (так рельеф и исчез между 17.08 и 03.09).
#   Запуск: run_headless.bat asda_build_materials_lit.py
import unreal, os, struct, zlib
import sys as _sys
_sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_mat_graph as _MG   # ★ дедуп №2 Т19: общий узел-скаляр (scal)
mel=unreal.MaterialEditingLibrary; atools=unreal.AssetToolsHelpers.get_asset_tools()
PKG="/Game/Materials"
ROUGH=0.9   # матовый (стилизованный вид без сильных бликов) — редактируемо на MIC; объекты/террейн
# ★★★ ROUGH_CHAR — roughness ПЕРСОНАЖЕЙ (сет C-1, 05.09.2026). Стол предлагал lerp(RoughHi, RoughLo, Tex.A) — ОТВЕРГНУТО по данным Э5(а)
#   (e5a_char_alpha.md, review_setA_offline.md #11): у оригинала (EmptyProject.fx:818/823) Tex.w — множитель АДДИТИВНОГО блика и кубмапы,
#   roughness-lerp в эталоне нет (never-invent); по данным альфа ≈ 1 у 78 % персонажных PNG порта (RGB → A=1) и у variable-текстур
#   мода 255 на 60–93 % пикселей (остальное — вырез волос/плащей), т.е. lerp по A свёлся бы к константе RoughLo с случайными пятнами
#   RoughHi на кромках выреза. Честный путь — ОДНА константа: 0.65 вместо 0.9 (кубмап-отражение оригинала Tex.w·0.3 у нас = SSR/капчер
#   по Specular, ему нужна поверхность не «бумажная»). Значение — за владельцем по A/B (setC1_report.md); SpecAlphaBoost остаётся 0.
ROUGH_CHAR=0.65
# ★★★ AmbientFloor = 0.0 (правка Д-3 аудита света 2026-09-03, fix_lighting_sun_and_ambient.md).
#   Было 0.25 = Emissive-«пол» BaseColor·0.25 поверх UE-света — «двойной свет»: тень никогда темнее
#   ~0.5·tex, ночные карты не тёмные. Узел в графе ОСТАВЛЕН как рычаг (переопределяется на мастере/MIC
#   без пересборки), но по умолчанию Emissive = 0; честный ambient даёт per-map SkyLight
#   (asda_convert_map_lit.py). Держать ОДИНАКОВО с asda_add_ambient_floor.FLOOR и asda_fix_lit_ambient_floor.
AMBIENT_FLOOR=0.0

def fresh(name):
    p="%s/%s"%(PKG,name)
    if unreal.EditorAssetLibrary.does_asset_exist(p): unreal.EditorAssetLibrary.delete_asset(p)
    return atools.create_asset(name,PKG,unreal.Material,unreal.MaterialFactoryNew())
def E(m,cls,x,y): return mel.create_material_expression(m,cls,x,y)
def C(a,ao,b,bi): mel.connect_material_expressions(a,ao,b,bi)
def connect_shading_model(m,e,eo=""):
    """★ Сет D: вход ShadingModel мастера. MP_ShadingModel скрыт от Python (SceneTypes.h:178 UMETA(Hidden)) → C++ мостик
    UAsdaMaterialEditLib (AsdaImporters). Возвращает bool как connect_material_property."""
    lib=getattr(unreal,"AsdaMaterialEditLib",None)
    if lib is None: raise RuntimeError("нет unreal.AsdaMaterialEditLib — модуль AsdaImporters не собран с сетом D")
    return bool(lib.connect_shading_model(m,e,eo))
def shading_model_input_node(m):
    """Узел на входе ShadingModel (None = пуст) — контроль ИЗ АССЕТА (аналог get_material_property_input_node)."""
    lib=getattr(unreal,"AsdaMaterialEditLib",None)
    return lib.get_shading_model_input_expression(m) if lib is not None else None
SHADING_MODEL_PROP="ShadingModel"   # сентинел для P(): prop не из unreal.MaterialProperty
def P(m,e,eo,prop):
    if prop==SHADING_MODEL_PROP: connect_shading_model(m,e,eo); return
    mel.connect_material_property(e,eo,prop)
def texp(m,nm,x,y):
    t=E(m,unreal.MaterialExpressionTextureSampleParameter2D,x,y); t.set_editor_property("parameter_name",nm); return t
scal = _MG.scalar_param   # ScalarParameter nm=v в (x, y) графа m (общая реализация asda_mat_graph)

# ─────────────────────────────────────────────────────────────────────────────────────────────────────────────
# ★★★ РЕЛЬЕФ NormalStudio (сет C-2, 05.09.2026; roundtable_2026-09-04/developer_round2.md §3, FINAL_synthesis §4 «Сет C»).
#   Карты студии: TexN (_N, TC_NORMALMAP) · TexRE (_RE, TC_MASKS: R шероховатость, G свечение, B затенение щелей) · TexC (_C, делайт-альбедо).
#   ВСЁ входит в граф через lerp с долей  amount = MPC.<param> × gate,  gate = MPC.Enable × HasMaps(MIC) × ClassStrength(MIC):
#       Normal    = normalize(lerp((0,0,1), TexN.RGB,            NormalStrength·gate))
#       AO        = [AO_прежний ×] lerp(1, TexRE.B,             AoAmount·gate)
#       Roughness = lerp(Roughness(скаляр), TexRE.R,             RoughFromMap·gate)
#       BaseColor = Multiply(lerp(Tex.RGB, TexC.RGB, Delight·gate), VertexColor)   ← прежний Multiply, у него меняется только вход A
#       Emissive  = Emissive_прежний + TexRE.G·EmissiveGain·gate·TexC.RGB             ← СУММА (ue_game_materials.py:167 ЗАМЕНЯЛ вход и терял AmbientFloor)
#   ⇒ при Enable=0, HasMaps=0 или ClassStrength=0 каждая ветка вырождается в прежнее значение — граф БУКВАЛЬНО прежний.
#   HasMaps (дефолт 0) и ClassStrength (дефолт 1) — скаляры MIC: раздаёт Tools/NormalStudio/ue_apply_maps_to_materials.py по ключу-ПУТИ
#   альбедо (имена текстур не уникальны — 19 % чужих карт по имени) и классам силы Tools/setC2_class_rules.json.
#   ★ Дефолт-текстуры сэмплеров ОБЯЗАТЕЛЬНЫ и обязаны совпадать со сжатием (память normal-from-albedo-studio): пустой параметр
#     = цветной DefaultTexture ⇒ «Sampler type is Normal, should be Color» ⇒ материал молча серый (видно только на живом RHI).
#     TexN → /Engine/EngineMaterials/DefaultNormal (TC_NORMALMAP); TexRE → /Game/Materials/T_NS_DefaultRE (4×4 TC_MASKS, R=0.9 G=0 B=1,
#     создаётся ensure_ns_default_re() — мастер компилируется и при ПУСТОЙ /Game/_NormalStudio/Maps, урок реверта P4V 03.09);
#     TexC → /Engine/EngineResources/DefaultTexture (цвет).
#   Число узлов: NS_EXPRESSIONS_FULL = 28 (персонаж/объект), NS_EXPRESSIONS_TERRAIN = 20 (без Delight/Emissive, с UV сплата и ×AO).
MPC_NORMALS = "/Game/Materials/MPC_AsdaNormals"
NS_DEFAULT_NORMAL = "/Engine/EngineMaterials/DefaultNormal"
NS_DEFAULT_RE = "/Game/Materials/T_NS_DefaultRE"
NS_DEFAULT_RE_PNG = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/RawAssets/Textures/_NormalStudio/T_NS_DefaultRE.png"
NS_DEFAULT_C = "/Engine/EngineResources/DefaultTexture"
NS_HASMAPS_DEFAULT = 0.0        # слот без карт остаётся прежним
NS_CLASS_STRENGTH_DEFAULT = 1.0 # класс силы задаёт MIC (ue_apply_maps_to_materials.py); 1 = без ослабления
NS_EXPRESSIONS_FULL = 28        # gate 5 + TexN/TexRE/TexC 3 + normal 5 + AO 4 + rough 3 + delight 3 + emissive 4 + Add 1
NS_EXPRESSIONS_TERRAIN = 20     # gate 5 + TexN/TexRE 2 + normal 5 + AO 4 + ×AO 1 + rough 3
# соединений (для fix-скриптов с подсчётом): узел→узел / узел→выход материала
NS_LINKS_FULL, NS_OUTPUTS_FULL = 34, 4          # C: gate 4 + amount 2×5 + normal 4 + AO 3 + rough 3 + delight 4 + emissive 4 + Add 2; P: Normal, AO, Roughness, Emissive
NS_LINKS_TERRAIN, NS_OUTPUTS_TERRAIN = 25, 3    # C: gate 4 + amount 2×3 + uv 2 + normal 4 + AO 3 + ×AO 2 + rough 3; P: Normal, AO, Roughness
NS_SCALARS = {"HasMaps": NS_HASMAPS_DEFAULT, "ClassStrength": NS_CLASS_STRENGTH_DEFAULT}

# ─────────────────────────────────────────────────────────────────────────────────────────────────────────────
# ★★★ СЕТ D (05.09.2026; roundtable_2026-09-04/FINAL_synthesis.md §4 «Сет D», developer_round2.md §5/§7, gamedesigner_round2.md §4 «Слой 3»,
#   review_setC1.md ВАЖНО 2–4). Три ветки, каждая при значении по умолчанию вырождается в ПРЕЖНИЙ граф:
#   (1) ЛИСТВА (wire_foliage, только M_AsdaObjectLit): shading_model = FromMaterialExpression; StaticSwitchParameter «Foliage» (дефолт False):
#       False → ShadingModel DefaultLit (пермутация без SSS — граф = прежний), True → TwoSidedFoliage; SubsurfaceColor = BaseColor × FoliageSSSTint × SSSScale.
#       Foliage=true ставит MIC листвы Tools/asda_set_foliage_switch.py по Tools/setD_foliage_rules.json (класс D минус вода/стекло/эффекты).
#       Пермутаций ×2 у ObjectLit (Скептик/Разработчик §5). Билборды и меши НЕ трогаются.
#   (2) ROUGHNESS ПО МАСКЕ (wire_char): Roughness = lerp(Roughness(=ROUGH_CHAR), lerp(RoughHi, RoughLo, Tex.A), RoughAlphaMix), RoughAlphaMix=0 ⇒ константа
#       (решение C-1); A/B на солнце снимается рантайм-MID героя (set0_shot_game.py ASDA_SET0_HERO_MID="RoughAlphaMix=1"), решение — владельцу.
#   (3) РИМ НА ЖИВЫХ (wire_rim, wire_char ⇒ M_AsdaCharLit / M_AsdaSkinLit / копия M_AsdaCharLitSoul — персонаж, экип, NPC, мобы класса CHAR):
#       Emissive += RimColor · RimWidth · Fresnel(RimPower) · (1 − saturate(N·L)) · RimEnable — только с ТЕНЕВОЙ стороны (ГД §4).
#       Параметры — из СУЩЕСТВУЮЩЕЙ коллекции MPC_AsdaToon (RimEnable/RimPower/RimWidth/RimColor, asda_build_toon_shaders.py; check-existing —
#       MPC_AsdaRim не заводим), per-map их ставит актор AAsdaRimLightActor (Source/AsdaReborn/AsdaRimLightActor.*), который размещает
#       asda_convert_map_lit.py (ASDA_LIT_MODE=rim / ASDA_RIM=auto: цвет = туман карты, сила по mx карты — ночные ↑, дневные 0).
#       L = SkyAtmosphereLightDirection[0] = −направление первого DirectionalLight сцены (SceneRendering.cpp:1546 — работает БЕЗ SkyAtmosphere).
#       RimEnable=0 (дефолт коллекции) ⇒ добавка 0 ⇒ граф численно прежний. Мобы классов MASK/BLEND (M_AsdaObjectLit/BlendLit) рима не получают —
#       мастер общий с объектами карт; отложено (setD_report.md).
FOLIAGE_SSS_TINT = (0.90, 1.00, 0.70)   # тон просвета кроны (слегка зелёный); предложение, за владельцем
FOLIAGE_SSS_SCALE = 0.5                 # developer_round2 §5: SubsurfaceColor = BaseColor·0.5; вниз, если яркость листвы против калибровки > +15 %
FOLIAGE_EXPRESSIONS = 7                 # Foliage(switch), ShadingModel DefaultLit, ShadingModel TwoSidedFoliage, FoliageSSSTint, SSSScale, Multiply×2
FOLIAGE_LINKS, FOLIAGE_OUTPUTS = 6, 2   # C: sm_fol→sw.True, sm_def→sw.False, bc→m1.A, tint→m1.B, m1→m2.A, scale→m2.B; P: ShadingModel, SubsurfaceColor
ROUGH_HI, ROUGH_LO = 0.85, 0.5          # developer_round2 §5 (ГД §5, З4): lerp(RoughHi, RoughLo, Tex.A); Tex.A = маска блика эталона (fx:818/823)
ROUGH_AB_EXPRESSIONS, ROUGH_AB_LINKS = 5, 6   # RoughHi, RoughLo, Lerp(маска), RoughAlphaMix, Lerp(выбор); C: hi→l1.A, lo→l1.B, Tex.A→l1.Alpha, rough→l2.A, l1→l2.B, mix→l2.Alpha
MPC_RIM = "/Game/Materials/MPC_AsdaToon"
RIM_PARAMS = ("RimEnable", "RimPower", "RimWidth")   # скаляры коллекции (RimWidth = сила, «RimStrength» стола); вектор RimColor
# ★ Диагностика (env ASDA_RIM_DEBUG=1 при сборке мастеров): вместо RimColor.rgb в Emissive идёт (sat(N·V), sat(N·L), 1−sat(N·V)) — по цвету кадра видно,
#   куда смотрит нормаль (R = к камере, G = к свету, B = от камеры). Узлов +1 (два AppendVector вместо ComponentMask), соединений +3.
RIM_DEBUG = os.environ.get("ASDA_RIM_DEBUG", "") == "1"
RIM_EXPRESSIONS, RIM_LINKS, RIM_OUTPUTS = 20 + (1 if RIM_DEBUG else 0), 21 + (3 if RIM_DEBUG else 0), 1  # 4 coll + ComponentMask(RimColor.rgb) + VertexNormalWS, CameraVectorWS, Dot, Clamp, OneMinus, Power(Fresnel явно)
#   + SkyAtmosphereLightDirection, Dot, Clamp, OneMinus + Multiply×4 + Add; P: Emissive
#   ★ Узел Fresnel (PixelNormalWS внутри) ЗДЕСЬ НЕ ГОДИТСЯ: на nightvalley давал ≈1 по всему лицу/телу (RimPower 8 ничего не менял —
#     game_setD_nightvalley_diag_pow8.log, люма лица 117 против 118), герой светился целиком (память ue-material-script-gotchas №1 — тот же симптом).
#     Край считаем ЯВНО по вершинной нормали: pow(1 − saturate(dot(VertexNormalWS, CameraVectorWS)), RimPower).
#   ★ ComponentMask обязателен: выход CollectionParameter у вектора — float4 без маски (у VectorParameter — RGB), и Add(Emissive float3, float4)
#     падает на SM6 «Arithmetic between types float3 and float4 are undefined» (headless_setD_rhi_setD_after.log — поймано live-RHI проверкой).


def wire_foliage(m, bc, C=None, P=None, x0=900, y0=-500):
    """Сет D (1): свитч листвы поверх собранного графа объектов. bc — узел BaseColor (Multiply). Создаёт FOLIAGE_EXPRESSIONS узлов."""
    _C = C or globals()["C"]; _P = P or globals()["P"]
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_FROM_MATERIAL_EXPRESSION)
    sw = E(m, unreal.MaterialExpressionStaticSwitchParameter, x0 + 420, y0)
    sw.set_editor_property("parameter_name", "Foliage"); sw.set_editor_property("default_value", False)
    sm_fol = E(m, unreal.MaterialExpressionShadingModel, x0, y0 - 90); sm_fol.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    sm_def = E(m, unreal.MaterialExpressionShadingModel, x0, y0 + 90); sm_def.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    _C(sm_fol, "", sw, "True"); _C(sm_def, "", sw, "False")      # имена входов свитча — GetInputName: "True"/"False" (MaterialExpressions.cpp:8705)
    _P(m, sw, "", SHADING_MODEL_PROP)                        # вход ShadingModel — через C++ мостик (см. connect_shading_model)
    tint = E(m, unreal.MaterialExpressionVectorParameter, x0, y0 + 260)
    tint.set_editor_property("parameter_name", "FoliageSSSTint")
    tint.set_editor_property("default_value", unreal.LinearColor(FOLIAGE_SSS_TINT[0], FOLIAGE_SSS_TINT[1], FOLIAGE_SSS_TINT[2], 1.0))
    m1 = E(m, unreal.MaterialExpressionMultiply, x0 + 250, y0 + 240); _C(bc, "", m1, "A"); _C(tint, "", m1, "B")
    scale = scal(m, "SSSScale", FOLIAGE_SSS_SCALE, x0, y0 + 400)
    m2 = E(m, unreal.MaterialExpressionMultiply, x0 + 450, y0 + 300); _C(m1, "", m2, "A"); _C(scale, "", m2, "B")
    _P(m, m2, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    return {"switch": sw, "sm_def": sm_def, "sm_fol": sm_fol, "tint": tint, "scale": scale, "sss": m2}


def wire_rim(m, em_prev, C=None, P=None, x0=900, y0=-1500):
    """Сет D (3): рим с теневой стороны поверх текущего Emissive (em_prev — узел, подключённый к MP_EMISSIVE_COLOR). RIM_EXPRESSIONS узлов.
    Параметры — MPC_AsdaToon (существующая коллекция); отсутствие любого из RIM_PARAMS/RimColor = RuntimeError (ассет не собирать)."""
    _C = C or globals()["C"]; _P = P or globals()["P"]
    EAL = unreal.EditorAssetLibrary
    mpc = EAL.load_asset(MPC_RIM)
    if not mpc:
        raise RuntimeError("нет коллекции %s (Tools/asda_build_toon_shaders.py)" % MPC_RIM)
    have_s = {str(p.get_editor_property("parameter_name")) for p in mpc.get_editor_property("scalar_parameters")}
    have_v = {str(p.get_editor_property("parameter_name")) for p in mpc.get_editor_property("vector_parameters")}
    miss = [p for p in RIM_PARAMS if p not in have_s] + ([] if "RimColor" in have_v else ["RimColor"])
    if miss:
        raise RuntimeError("в %s нет параметров %s" % (MPC_RIM, miss))
    def coll(name, x, y):
        c = E(m, unreal.MaterialExpressionCollectionParameter, x, y)
        c.set_editor_property("collection", mpc); c.set_editor_property("parameter_name", name); return c
    en = coll("RimEnable", x0, y0); pw = coll("RimPower", x0, y0 + 110); st = coll("RimWidth", x0, y0 + 220); col4 = coll("RimColor", x0, y0 + 330)
    if not RIM_DEBUG:
        col = E(m, unreal.MaterialExpressionComponentMask, x0 + 300, y0 + 400)   # RimColor.rgb (float4 → float3, см. RIM_EXPRESSIONS)
        col.set_editor_property("r", True); col.set_editor_property("g", True); col.set_editor_property("b", True); col.set_editor_property("a", False)
        _C(col4, "", col, "")
    vn = E(m, unreal.MaterialExpressionVertexNormalWS, x0 + 300, y0 - 60)
    cv = E(m, unreal.MaterialExpressionCameraVectorWS, x0 + 300, y0 + 40)
    dnv = E(m, unreal.MaterialExpressionDotProduct, x0 + 520, y0 - 20); _C(vn, "", dnv, "A"); _C(cv, "", dnv, "B")
    snv = E(m, unreal.MaterialExpressionClamp, x0 + 700, y0 - 20); _C(dnv, "", snv, "")
    onv = E(m, unreal.MaterialExpressionOneMinus, x0 + 880, y0 - 20); _C(snv, "", onv, "")
    fr = E(m, unreal.MaterialExpressionPower, x0 + 1060, y0 - 20); _C(onv, "", fr, "Base"); _C(pw, "", fr, "Exp")   # имена входов Power: Base/Exp (память)
    ld = E(m, unreal.MaterialExpressionSkyAtmosphereLightDirection, x0 + 300, y0 + 300); ld.set_editor_property("light_index", 0)
    dot = E(m, unreal.MaterialExpressionDotProduct, x0 + 520, y0 + 240); _C(vn, "", dot, "A"); _C(ld, "", dot, "B")
    sat = E(m, unreal.MaterialExpressionClamp, x0 + 700, y0 + 240); _C(dot, "", sat, "")
    om = E(m, unreal.MaterialExpressionOneMinus, x0 + 880, y0 + 240); _C(sat, "", om, "")
    if RIM_DEBUG:   # цвет = (sat(N·V), sat(N·L), 1−sat(N·V)) вместо RimColor: два AppendVector (float2 → float3); +1 узел, +2 соединения
        ap1 = E(m, unreal.MaterialExpressionAppendVector, x0 + 300, y0 + 400); _C(snv, "", ap1, "A"); _C(sat, "", ap1, "B")
        col = E(m, unreal.MaterialExpressionAppendVector, x0 + 500, y0 + 400); _C(ap1, "", col, "A"); _C(onv, "", col, "B")
    m1 = E(m, unreal.MaterialExpressionMultiply, x0 + 1240, y0 + 120); _C(fr, "", m1, "A"); _C(om, "", m1, "B")
    m2 = E(m, unreal.MaterialExpressionMultiply, x0 + 1240, y0 + 160); _C(m1, "", m2, "A"); _C(st, "", m2, "B")
    m3 = E(m, unreal.MaterialExpressionMultiply, x0 + 1420, y0 + 100); _C(m2, "", m3, "A"); _C(en, "", m3, "B")
    m4 = E(m, unreal.MaterialExpressionMultiply, x0 + 1600, y0 + 200); _C(m3, "", m4, "A"); _C(col, "", m4, "B")
    add = E(m, unreal.MaterialExpressionAdd, x0 + 1800, y0 + 120); _C(em_prev, "", add, "A"); _C(m4, "", add, "B")
    _P(m, add, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return {"enable": en, "power": pw, "width": st, "color": col, "fresnel": fr, "normal": vn, "add": add}


def _png_rgb(w, h, rgb):
    """Минимальный PNG RGB8 без внешних библиотек (для дефолт-маски 4×4)."""
    raw = b"".join(b"\x00" + bytes(rgb) * w for _ in range(h))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def ensure_ns_default_re(log=None):
    """Дефолт-текстура маски T_NS_DefaultRE (4×4, R=0.9 шерох., G=0 свечение, B=1 AO; TC_MASKS, sRGB off). Идемпотентно.
    Возвращает загруженную текстуру. Проверено check-existing: grep T_NS_/DefaultRE по Tools/NormalStudio/Content = 0 до сета C-2."""
    EAL = unreal.EditorAssetLibrary
    if not EAL.does_asset_exist(NS_DEFAULT_RE):
        os.makedirs(os.path.dirname(NS_DEFAULT_RE_PNG), exist_ok=True)
        if not os.path.isfile(NS_DEFAULT_RE_PNG):
            with open(NS_DEFAULT_RE_PNG, "wb") as f:
                f.write(_png_rgb(4, 4, (230, 0, 255)))   # 230/255 = 0.902
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", NS_DEFAULT_RE_PNG); t.set_editor_property("destination_path", PKG)
        t.set_editor_property("destination_name", "T_NS_DefaultRE"); t.set_editor_property("automated", True)
        t.set_editor_property("replace_existing", True); t.set_editor_property("save", False)
        t.set_editor_property("factory", unreal.TextureFactory())
        atools.import_asset_tasks([t])
        tex = EAL.load_asset(NS_DEFAULT_RE)
        if not tex:
            raise RuntimeError("T_NS_DefaultRE не импортировалась из %s" % NS_DEFAULT_RE_PNG)
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        EAL.save_loaded_asset(tex)
        if log: log("создана дефолт-маска %s (TC_MASKS, sRGB=%s)" % (NS_DEFAULT_RE, tex.get_editor_property("srgb")))
    tex = EAL.load_asset(NS_DEFAULT_RE)
    if tex.get_editor_property("compression_settings") != unreal.TextureCompressionSettings.TC_MASKS:
        raise RuntimeError("%s: сжатие %s ≠ TC_MASKS — сэмплер Masks не совпадёт" % (NS_DEFAULT_RE, tex.get_editor_property("compression_settings")))
    return tex


def wire_normal_studio(m, tex, bc, rough, em=None, uv=None, ao_in=None, with_delight=True, with_emissive=True,
                       C=None, P=None, x0=-2400, y0=900):
    """Ветки рельефа NormalStudio ПОВЕРХ уже собранного прежнего графа (см. шапку блока).
      tex   — сэмплер альбедо (для Delight: его RGB сейчас идёт в bc.A);   bc — Multiply BaseColor (вход A перевязывается на lerp);
      rough — скаляр Roughness (MP_ROUGHNESS перевязывается на lerp);        em — прежний узел Emissive (сумма) или None;
      uv    — узел UV для сэмплеров карт (террейн: uv0 = uv·Scale0) или None (UV0 по умолчанию);
      ao_in — прежний узел AO (террейн) → AO = ao_in × lerp; None → AO = lerp.
      C(a,ao,b,bi) / P(m,e,eo,prop) — функции соединения (fix-скрипты передают ПРОВЕРЯЮЩИЕ; по умолчанию — C/P модуля).
    Возвращает dict узлов. Число созданных узлов: NS_EXPRESSIONS_FULL / NS_EXPRESSIONS_TERRAIN."""
    _C = C or globals()["C"]; _P = P or globals()["P"]
    EAL = unreal.EditorAssetLibrary
    mpc = EAL.load_asset(MPC_NORMALS)
    if not mpc:
        raise RuntimeError("нет коллекции %s" % MPC_NORMALS)
    def_n = EAL.load_asset(NS_DEFAULT_NORMAL); def_re = ensure_ns_default_re(); def_c = EAL.load_asset(NS_DEFAULT_C)
    if not def_n or not def_re or not def_c:
        raise RuntimeError("нет дефолт-текстур сэмплеров: N=%s RE=%s C=%s" % (def_n, def_re, def_c))
    N = {}
    def coll(name, x, y):
        c = E(m, unreal.MaterialExpressionCollectionParameter, x, y)
        c.set_editor_property("collection", mpc); c.set_editor_property("parameter_name", name)
        N["coll_" + name] = c; return c
    def stex(nm, x, y, stype, dtex):
        t = texp(m, nm, x, y); t.set_editor_property("texture", dtex); t.set_editor_property("sampler_type", stype)
        if uv is not None: _C(uv, "", t, "UVs")
        N[nm] = t; return t
    # ── общий множитель gate = Enable × HasMaps × ClassStrength
    enable = coll("Enable", x0, y0)
    has = N["HasMaps"] = scal(m, "HasMaps", NS_HASMAPS_DEFAULT, x0, y0 + 120)
    cls = N["ClassStrength"] = scal(m, "ClassStrength", NS_CLASS_STRENGTH_DEFAULT, x0, y0 + 240)
    g1 = N["gate1"] = E(m, unreal.MaterialExpressionMultiply, x0 + 220, y0 + 40); _C(enable, "", g1, "A"); _C(has, "", g1, "B")
    gate = N["gate"] = E(m, unreal.MaterialExpressionMultiply, x0 + 400, y0 + 100); _C(g1, "", gate, "A"); _C(cls, "", gate, "B")
    def amount(name, x, y):
        p = coll(name, x, y); mul = E(m, unreal.MaterialExpressionMultiply, x + 220, y + 20)
        _C(p, "", mul, "A"); _C(gate, "", mul, "B"); N["amt_" + name] = mul; return mul
    # ── сэмплеры карт (дефолты по типу сжатия!)
    texn = stex("TexN", x0, y0 + 420, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, def_n)
    texre = stex("TexRE", x0, y0 + 700, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, def_re)
    texc = stex("TexC", x0, y0 + 980, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, def_c) if (with_delight or with_emissive) else None
    # ── нормаль: плоская → карта
    na = amount("NormalStrength", x0 + 600, y0 + 380)
    flat = N["flat"] = E(m, unreal.MaterialExpressionConstant3Vector, x0 + 800, y0 + 300)
    flat.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 1.0, 1.0))
    nl = N["nlerp"] = E(m, unreal.MaterialExpressionLinearInterpolate, x0 + 1100, y0 + 360)
    _C(flat, "", nl, "A"); _C(texn, "RGB", nl, "B"); _C(na, "", nl, "Alpha")
    nn = N["nnorm"] = E(m, unreal.MaterialExpressionNormalize, x0 + 1300, y0 + 360); _C(nl, "", nn, "VectorInput")
    _P(m, nn, "", unreal.MaterialProperty.MP_NORMAL)
    # ── затенение щелей: 1 → TexRE.B (× прежний AO у террейна)
    aa = amount("AoAmount", x0 + 600, y0 + 640)
    one = N["one"] = E(m, unreal.MaterialExpressionConstant, x0 + 800, y0 + 600); one.set_editor_property("r", 1.0)
    al = N["aolerp"] = E(m, unreal.MaterialExpressionLinearInterpolate, x0 + 1100, y0 + 640)
    _C(one, "", al, "A"); _C(texre, "B", al, "B"); _C(aa, "", al, "Alpha")
    ao_out = al
    if ao_in is not None:
        ao_out = N["aomul"] = E(m, unreal.MaterialExpressionMultiply, x0 + 1300, y0 + 640); _C(ao_in, "", ao_out, "A"); _C(al, "", ao_out, "B")
    _P(m, ao_out, "", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    # ── шероховатость: скаляр → TexRE.R
    ra = amount("RoughFromMap", x0 + 600, y0 + 820)
    rl = N["roughlerp"] = E(m, unreal.MaterialExpressionLinearInterpolate, x0 + 1100, y0 + 840)
    _C(rough, "", rl, "A"); _C(texre, "R", rl, "B"); _C(ra, "", rl, "Alpha")
    _P(m, rl, "", unreal.MaterialProperty.MP_ROUGHNESS)
    # ── делайтинг: Tex.RGB → TexC.RGB на входе A прежнего Multiply BaseColor
    if with_delight:
        da = amount("Delight", x0 + 600, y0 + 1000)
        dl = N["delight"] = E(m, unreal.MaterialExpressionLinearInterpolate, x0 + 1100, y0 + 1020)
        _C(tex, "RGB", dl, "A"); _C(texc, "RGB", dl, "B"); _C(da, "", dl, "Alpha")
        _C(dl, "", bc, "A")
    # ── свечение: Emissive_прежний + TexRE.G·gain·TexC.RGB
    if with_emissive:
        ea = amount("EmissiveGain", x0 + 600, y0 + 1180)
        e1 = N["em1"] = E(m, unreal.MaterialExpressionMultiply, x0 + 1100, y0 + 1200); _C(texre, "G", e1, "A"); _C(ea, "", e1, "B")
        e2 = N["em2"] = E(m, unreal.MaterialExpressionMultiply, x0 + 1300, y0 + 1200); _C(e1, "", e2, "A"); _C(texc, "RGB", e2, "B")
        em_out = e2
        if em is not None:
            em_out = N["emadd"] = E(m, unreal.MaterialExpressionAdd, x0 + 1500, y0 + 1160); _C(em, "", em_out, "A"); _C(e2, "", em_out, "B")
        _P(m, em_out, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return N


# Ровно столько узлов строит wire_object() БЕЗ рельефа: Tex, VertexColor, Multiply(BaseColor), Roughness, Metallic,
#   AmbientFloor, Multiply(Emissive), FadeIn, Multiply(OpacityMask) = 9 — это живой M_AsdaObjectLit (генератор 5 узлов +
#   asda_add_ambient_floor.py 2 + asda_add_fadein_param.py 2), сверено дампом setC2_audit_before.json 05.09.2026.
#   ★ Сет D (05.09.2026): + FOLIAGE_EXPRESSIONS (7) узлов свитча листвы (wire_foliage) = 16; итого с рельефом 16 + 28 = 44.
WIRE_OBJECT_BASE_EXPRESSIONS = 9 + FOLIAGE_EXPRESSIONS
WIRE_OBJECT_EXPRESSIONS = WIRE_OBJECT_BASE_EXPRESSIONS + NS_EXPRESSIONS_FULL   # 44
FADE_IN_DEFAULT = 1.0


def wire_object(m, with_normal_studio=True):
    """Граф М-мастера ОБЪЕКТОВ карт (masked). Вынесен отдельно (как wire_char), чтобы чинить СУЩЕСТВУЮЩИЙ мастер на месте
    (asda_fix_objectlit_graph.py) — без delete_asset, не теряя parent у 6771 MIC и usage-флаги.
    Воспроизводит живой граф 05.09.2026 = build_object + AmbientFloor-пол (asda_add_ambient_floor.py) + FadeIn
    (asda_add_fadein_param.py: OpacityMask = Tex.A × FadeIn, дефолт 1 — фейд превью char-select через MID) + рельеф NormalStudio.
    ★ Тот же намеренный дефект, что у wire_char: C(vc,"RGB",…) у VertexColor молча не соединяется (выход безымянный),
      BaseColor = Tex.RGB; менять только с разрешения владельца (asda_fix_charlit_lighting.KNOWN_DANGLING)."""
    m.set_editor_property("blend_mode",unreal.BlendMode.BLEND_MASKED)
    m.set_editor_property("shading_model",unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    m.set_editor_property("opacity_mask_clip_value",0.5)
    m.set_editor_property("two_sided",True)
    m.set_editor_property("used_with_skeletal_mesh",True)   # ★ персонаж = скелетал
    m.set_editor_property("used_with_instanced_static_meshes",True)   # ★ HISM-объекты: без флага в -game чёрный фолбэк
    tex=texp(m,"Tex",-700,-100)
    vc=E(m,unreal.MaterialExpressionVertexColor,-700,150)
    bc=E(m,unreal.MaterialExpressionMultiply,-300,0); C(tex,"RGB",bc,"A"); C(vc,"RGB",bc,"B")   # BaseColor=tex·vtxColor
    rough=scal(m,"Roughness",ROUGH,-300,200); metal=scal(m,"Metallic",0.0,-300,320)
    floor=scal(m,"AmbientFloor",AMBIENT_FLOOR,550,400)
    em=E(m,unreal.MaterialExpressionMultiply,700,300); C(bc,"",em,"A"); C(floor,"",em,"B")       # Emissive = BaseColor·AmbientFloor (пол, дефолт 0)
    fade=scal(m,"FadeIn",FADE_IN_DEFAULT,550,600)
    om=E(m,unreal.MaterialExpressionMultiply,700,550); C(tex,"A",om,"A"); C(fade,"",om,"B")     # OpacityMask = Tex.A·FadeIn
    P(m,bc,"",unreal.MaterialProperty.MP_BASE_COLOR)
    P(m,rough,"",unreal.MaterialProperty.MP_ROUGHNESS)
    P(m,metal,"",unreal.MaterialProperty.MP_METALLIC)
    P(m,em,"",unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    P(m,om,"",unreal.MaterialProperty.MP_OPACITY_MASK)
    wire_foliage(m, bc=bc)          # ★ сет D: shading_model → FromMaterialExpression, свитч Foliage (дефолт False = DefaultLit, как раньше)
    if with_normal_studio:
        wire_normal_studio(m, tex=tex, bc=bc, rough=rough, em=em)
    mel.recompile_material(m)


def build_object():
    m=fresh("M_AsdaObjectLit")
    wire_object(m)
    unreal.EditorAssetLibrary.save_asset("%s/M_AsdaObjectLit"%PKG); unreal.log("M_AsdaObjectLit OK")


def build_object_legacy_5nodes():
    """Прежний build_object (5 узлов, до 31.07.2026) — оставлен для истории, НЕ вызывается (живой мастер = wire_object)."""
    m=fresh("M_AsdaObjectLit")
    m.set_editor_property("blend_mode",unreal.BlendMode.BLEND_MASKED)
    m.set_editor_property("shading_model",unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    m.set_editor_property("opacity_mask_clip_value",0.5)
    m.set_editor_property("two_sided",True)
    m.set_editor_property("used_with_skeletal_mesh",True)   # ★ персонаж = скелетал
    m.set_editor_property("used_with_instanced_static_meshes",True)   # ★ HISM-объекты: без флага в -game чёрный фолбэк
    tex=texp(m,"Tex",-700,-100)
    vc=E(m,unreal.MaterialExpressionVertexColor,-700,150)
    bc=E(m,unreal.MaterialExpressionMultiply,-300,0); C(tex,"RGB",bc,"A"); C(vc,"RGB",bc,"B")   # BaseColor=tex·vtxColor
    rough=scal(m,"Roughness",ROUGH,-300,200); metal=scal(m,"Metallic",0.0,-300,320)
    P(m,bc,"",unreal.MaterialProperty.MP_BASE_COLOR)
    P(m,rough,"",unreal.MaterialProperty.MP_ROUGHNESS)
    P(m,metal,"",unreal.MaterialProperty.MP_METALLIC)
    P(m,tex,"A",unreal.MaterialProperty.MP_OPACITY_MASK)
    mel.recompile_material(m); unreal.EditorAssetLibrary.save_asset("%s/M_AsdaObjectLit"%PKG); unreal.log("M_AsdaObjectLit OK")

def build_terrain():
    m=fresh("M_AsdaTerrainLit")
    m.set_editor_property("shading_model",unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    m.set_editor_property("two_sided",True)
    uv=E(m,unreal.MaterialExpressionTextureCoordinate,-1100,0)
    s0=scal(m,"Scale0",8.0,-1100,200); s1=scal(m,"Scale1",8.0,-1100,320); hasD1=scal(m,"HasD1",0.0,-1100,440)
    uv0=E(m,unreal.MaterialExpressionMultiply,-900,100); C(uv,"",uv0,"A"); C(s0,"",uv0,"B")
    uv1=E(m,unreal.MaterialExpressionMultiply,-900,250); C(uv,"",uv1,"A"); C(s1,"",uv1,"B")
    d0=texp(m,"Detail0",-650,-300); C(uv0,"",d0,"UVs")
    d1=texp(m,"Detail1",-650,-50);  C(uv1,"",d1,"UVs")
    mk=texp(m,"Mask",-650,220);     C(uv,"",mk,"UVs")
    cm=texp(m,"Colormap",-650,480); C(uv,"",cm,"UVs")
    w=E(m,unreal.MaterialExpressionMultiply,-350,250); C(mk,"A",w,"A"); C(hasD1,"",w,"B")
    g1=E(m,unreal.MaterialExpressionLinearInterpolate,-150,-100); C(d0,"RGB",g1,"A"); C(d1,"RGB",g1,"B"); C(w,"",g1,"Alpha")
    g2=E(m,unreal.MaterialExpressionLinearInterpolate,80,50); C(g1,"",g2,"A"); C(cm,"RGB",g2,"B"); C(cm,"A",g2,"Alpha")
    vc=E(m,unreal.MaterialExpressionVertexColor,80,300)
    bc=E(m,unreal.MaterialExpressionMultiply,320,100); C(g2,"",bc,"A"); C(vc,"RGB",bc,"B")   # BaseColor=detail·colormap·vtxColor
    rough=scal(m,"Roughness",ROUGH,320,300); metal=scal(m,"Metallic",0.0,320,420)
    P(m,bc,"",unreal.MaterialProperty.MP_BASE_COLOR)
    P(m,mk,"B",unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)   # ★ запечённый AO → канал AO (влияет на indirect, корректно)
    P(m,rough,"",unreal.MaterialProperty.MP_ROUGHNESS)
    P(m,metal,"",unreal.MaterialProperty.MP_METALLIC)
    mel.recompile_material(m); unreal.EditorAssetLibrary.save_asset("%s/M_AsdaTerrainLit"%PKG); unreal.log("M_AsdaTerrainLit OK")

def build_char():
    """★ М-мастер СЕМЕЙСТВА ПЕРСОНАЖА (tSkin/tRigid эталона): НЕПРОЗРАЧНЫЙ.
    Альфа текстуры в оригинале в прозрачность НЕ идёт (PShade: Output.RGBColor.w = 1*fog*fade),
    а маскирует блик/кубмап (vSpecular*Tex.w, texCUBE*Tex.w*0.3) → заводим её в Specular
    (Specular = SpecScale · Tex.A; SpecScale=0.5 — дефолт UE, т.е. на непрозрачных зонах вид не меняется)."""
    m=fresh("M_AsdaCharLit")
    wire_char(m)
    unreal.EditorAssetLibrary.save_asset("%s/M_AsdaCharLit"%PKG); unreal.log("M_AsdaCharLit OK")

def wire_char(m, with_normal_studio=True):
    """Граф М-мастера персонажа. Вынесен отдельно, чтобы им же чинить УЖЕ СУЩЕСТВУЮЩИЙ мастер
    (asda_fix_charlit_lighting.py: delete_all_material_expressions + wire_char) — без delete_asset,
    т.е. НЕ теряя parent у 2585 MIC.

    ★ РЕГРЕССИЯ 2026-07-31 и её фикс. M_AsdaObjectLit после генератора дорабатывался скриптами
      (asda_add_ambient_floor.py → Emissive=BaseColor·AmbientFloor(0.25); asda_add_fadein_param.py),
      а M_AsdaCharLit был собран с нуля ⇒ ambient-floor в него НЕ попал, и персонаж стал уходить
      в чёрное («слишком чувствителен к свету, легко затемняется»). Плюс Specular был сделан
      МНОЖИТЕЛЕМ SpecScale·Tex.A — а в эталоне (EmptyProject.fx, PShade) Tex.w АДДИТИВЕН:
          ... + ((vSpecular.xyz * Tex.w) * ShadowAlpha.z);   += texCUBE(...)*(Tex.w*0.3f)
      т.е. добавка блика ПОВЕРХ, а не «обнуление базового Specular» при малой альфе.
    ⇒ Светотень обязана совпадать с M_AsdaObjectLit; отличие ТОЛЬКО непрозрачность (нет OpacityMask).
    """
    m.set_editor_property("blend_mode",unreal.BlendMode.BLEND_OPAQUE)          # ★ единственное намеренное отличие от M_AsdaObjectLit
    m.set_editor_property("shading_model",unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    m.set_editor_property("two_sided",True)                                    # как у M_AsdaObjectLit (одностор. геометрия частей)
    m.set_editor_property("used_with_skeletal_mesh",True)                      # тело/броня = скелетал
    m.set_editor_property("used_with_instanced_static_meshes",True)            # на случай HISM-использования
    tex=texp(m,"Tex",-700,-100)
    vc=E(m,unreal.MaterialExpressionVertexColor,-700,150)
    bc=E(m,unreal.MaterialExpressionMultiply,-300,0); C(tex,"RGB",bc,"A"); C(vc,"RGB",bc,"B")   # BaseColor=tex·vtxColor
    rough=scal(m,"Roughness",ROUGH_CHAR,-300,200); metal=scal(m,"Metallic",0.0,-300,320)   # ★ сет C-1: ROUGH_CHAR (см. выше)
    # ★ Сет D (2): Roughness = lerp(Roughness, lerp(RoughHi, RoughLo, Tex.A), RoughAlphaMix); RoughAlphaMix=0 ⇒ ровно константа C-1.
    #   Tex.A — маска блика эталона (fx:818/823): A=1 (блик) → RoughLo 0.5, A=0 → RoughHi 0.85. A/B на солнце — рантайм-MID героя.
    rhi=scal(m,"RoughHi",ROUGH_HI,-1000,180); rlo=scal(m,"RoughLo",ROUGH_LO,-1000,300)
    rmask=E(m,unreal.MaterialExpressionLinearInterpolate,-780,240); C(rhi,"",rmask,"A"); C(rlo,"",rmask,"B"); C(tex,"A",rmask,"Alpha")
    rmix=scal(m,"RoughAlphaMix",0.0,-1000,420)
    rsel=E(m,unreal.MaterialExpressionLinearInterpolate,-120,220); C(rough,"",rsel,"A"); C(rmask,"",rsel,"B"); C(rmix,"",rsel,"Alpha")
    # Specular = SpecBase + SpecAlphaBoost·Tex.A. ДЕФОЛТ (0.5 + 0·A) = 0.5 = дефолт UE = ровно то,
    #   что давал M_AsdaObjectLit (у него вход Specular не подключён). Альфа заведена АДДИТИВНО и
    #   по умолчанию ВЫКЛЮЧЕНА — включать, только когда величина блика будет доказана эталоном.
    sbase=scal(m,"SpecBase",0.5,-700,430); sboost=scal(m,"SpecAlphaBoost",0.0,-700,550)
    sbm=E(m,unreal.MaterialExpressionMultiply,-400,520); C(sboost,"",sbm,"A"); C(tex,"A",sbm,"B")
    sadd=E(m,unreal.MaterialExpressionAdd,-180,470); C(sbase,"",sadd,"A"); C(sbm,"",sadd,"B")
    # Ambient-floor 1:1 как у M_AsdaObjectLit/M_AsdaTerrainLit (asda_add_ambient_floor.py): узел-рычаг,
    #   дефолт 0.0 с 2026-09-03 (Д-3: «двойной свет» снят; см. AMBIENT_FLOOR выше).
    floor=scal(m,"AmbientFloor",AMBIENT_FLOOR,550,400)
    em=E(m,unreal.MaterialExpressionMultiply,700,300); C(bc,"",em,"A"); C(floor,"",em,"B")
    P(m,bc,"",unreal.MaterialProperty.MP_BASE_COLOR)
    P(m,rsel,"",unreal.MaterialProperty.MP_ROUGHNESS)          # ★ сет D: выбор константа/маска (rsel), дальше рельеф берёт его как «скаляр»
    P(m,metal,"",unreal.MaterialProperty.MP_METALLIC)
    P(m,sadd,"",unreal.MaterialProperty.MP_SPECULAR)
    P(m,em,"",unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    # ★ сет C-2: рельеф NormalStudio (lerp от Enable×HasMaps×ClassStrength; Emissive — СУММА с полом). 11 + 5 + 28 = 44 узла.
    em_last = em
    if with_normal_studio:
        N = wire_normal_studio(m, tex=tex, bc=bc, rough=rsel, em=em)
        em_last = N.get("emadd", em)
    # ★ сет D (3): рим с теневой стороны ПОСЛЕДНИМ (Emissive = прежняя сумма + рим·RimEnable). Итого 44 + 20 = 64 узла.
    wire_rim(m, em_prev=em_last)
    mel.recompile_material(m)

# ВНИМАНИЕ: fresh() УДАЛЯЕТ существующий мастер → пересборка M_AsdaObjectLit/M_AsdaTerrainLit обнуляет
#   parent у всех ссылающихся MIC. Поэтому по умолчанию собираем ТОЛЬКО недостающее; полная пересборка —
#   явным аргументом. Запуск: run_headless.bat asda_build_materials_lit.py [char|all]
#   Модуль импортируем (asda_fix_charlit_lighting.py берёт wire_char) — тело под __main__.
import sys
if __name__ == "__main__":
    _mode = "char" if any(a.lower()=="char" for a in sys.argv) else ("all" if any(a.lower()=="all" for a in sys.argv) else "char")
    if _mode == "all":
        build_object(); build_terrain(); build_char()
    else:
        build_char()
    unreal.log("=== LIT-мастера готовы (режим %s) ===" % _mode)
