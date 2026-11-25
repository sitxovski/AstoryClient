# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_sky_dome.py — НАСТОЯЩЕЕ небо AsdaStory (реверс Reports/sky_system_reverse.md):
#   пер-карточный SKY_DOME.JBM (полусфера R=1000/H=285, FVF 0x252: pos + uv0 планарная + uv1 цилиндрическая)
#   + 3 слоя: cloude1 база (uv0) → cloude2 по альфе (uv0, скролл) → sky_dome.TEX горы горизонта по альфе (uv1).
#   Скорости скролла из заголовка .TER (T[2912..2915], file offset 12+idx*4): слой1 U=V=T[2912] (баг оригинала:
#   2913 игнорируется), слой2 U/V=T[2914/2915]. Единицы dt≈мс (ГИПОТЕЗА отчёта §4) → UV/с = spd×1000.
#   Актор AAsdaSkyDomeActor следует за камерой (оригинал: world-трансляция купола = позиция камеры).
#   Звёздный слой (g_bFXStarSky, опция графики) НЕ реализован — формула VS-генерации UV не подтверждена (§10).
# ★ 05.09.2026 (доводка сета A, review_setA_rollout.md §4 вариант (д)): купол — TRANSLUCENT unlit с «Apply Fogging»=off
#   (`use_translucency_vertex_fog=False`), как лента горизонта. Причина: в UE 5.8 opaque-материал с IsSky НЕ защищён от
#   ExponentialHeightFog (Material.h:1088 «Height and Volumetric fog effects will still be applied») — сценовый туман сета A
#   заливал купол и облака; cutoff тумана по куполу (27 105 юн) красил чёрным пустоту за краем карты ниже уровня глаз.
#   Транслюцент без fogging сценовый туман не получает вовсе (MaterialShared.cpp:2224 ShouldApplyFogging), а фон за краем
#   карты (far plane) снова красится туманом, как в оригинале (фон = цвет тумана). Порядок среди транслюцентов —
#   TranslucencySortPriority DOME_SORT_PRIORITY (−1000, рисуется ПЕРВЫМ: иначе купол с центром bounds cam+14 250 перекрыл бы
#   3D-облака CloudField на z=35 000 и любые дальние эффекты); is_sky снят (флаг «Unlit and Opaque only», для транслюцента
#   не определён; SkyAtmosphere в картах нет). Секции меша: Clouds (0) → Horizon (1) — порядок стабилен
#   (FMeshDrawCommandSortKey::Translucent.MeshIdInPrimitive), лента ×0.995 остаётся.
# env: ASDA_MAP (имя папки карты). Заменяет старую сферу SM_sky/MI_sky.
import os, struct, unreal
import sys as _sys
_sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_maps as _MAPS          # ★ единый санитайзер имени карты
import asda_mapraw as _MR          # ★ единый ридер .mapraw (был свой parse_mapraw_fog по офсетам)
import asda_ter_height as _TER     # ★ единый разбор .TER (был свой parse_ter_sky)
import asda_paths as _P
import asda_jbm as _JBM            # ★ дедуп №2 В1 Т5: единый разбор шапки .JBM (был свой parse_jbm по офсетам)
import asda_model_import_ue as _MIU  # ★ дедуп №2 В1 Т13: общий поиск текстуры в <root>/Texture
import asda_mat_graph as _MG       # ★ чистка графа до пустого + контроль после сборки (баг UE 5.8 DeleteAllMaterialExpressions)

MAP_NAME  = os.environ.get("ASDA_MAP", "alpen")
# ★ 06.09.2026 fog_srgb (review_rename_and_fog.md §4, решение владельца «делаем»): байты цвета тумана .mapraw — sRGB
#   (оригинал CRenderer_SetFogAndLightConstants@0x1124D30: D3DRS_FOGCOLOR = ARGB как есть, D3DRS_SRGBWRITEENABLE в эталоне нет →
#   кадр смешивается в гамма-пространстве, байты = то, что видит глаз), а FogInscatteringLuminance MapFog и FogColor купола —
#   ЛИНЕЙНАЯ яркость (на экран через тонмап → sRGB). Запись raw/255 без конверсии давала туман ~2× светлее оригинала
#   (chaos #401E40 → на кадре (144,100,170) вместо ≈(64,30,64)). ASDA_FOG_SRGB=1 (умолч.) — sRGB→linear по формуле UE
#   FLinearColor::FromSRGBColor; 0 — прежнее raw (только для A/B). ЕДИНСТВЕННАЯ функция цвета тумана — fog_color_ue(): ею же
#   пользуются asda_indoor_fog.py (4 закрытые карты) и asda_fog_srgb.py (правка 22 карт на месте без перегенерации).
FOG_SRGB = os.environ.get("ASDA_FOG_SRGB", "1").strip() != "0"


def fog_color_ue(fog_rgb):
    """(r, g, b) 0..1 для UE из fog ARGB .mapraw (asda_mapraw.argb_to_rgb = байт/255): при FOG_SRGB — sRGB→linear
    (asda_mapraw.rgb_srgb_to_linear = формула UE Color.h:68), иначе как есть. Одна точка правды для MapFog и FogColor купола."""
    c = tuple(float(v) for v in fog_rgb[:3])
    return _MR.rgb_srgb_to_linear(c) if FOG_SRGB else c

# ★ Корень карт: раньше тут был ORIG-клиент, а у asda_ter_height — MINE. Файлы .TER/SKY_DOME.JBM
#   в обеих сборках побайтово совпадают (сверено 2026-08-02 на всех 22 картах), корень теперь один.
CLIENT_MAP= _P.MAP_DIR
_asset = _MAPS.map_asset_name
MASTER    = "/Game/Materials/M_AsdaSkyDome"
# ★ Ровно столько узлов строит build_master(): uv0, uv1, tm, sb, tmB (5) + scroll_uv×2 (s_u, s_v, ap, mul,
#   add = 5 каждый → 10) + tBase, tClouds, l1 (3) + tStar, sInt, sMul, sAdd (4) + sb2, bMul (2)
#   + _append_fog_chain (wp, op, sub, mz, pScale, dv, pOff, s1, pInv, m1f, pMul, m2f, pAdd, ad, cl, fc, lp = 17)
#   + opq (Constant 1.0 → Opacity, транслюцент 05.09) (1)
#   + append_range_fade (14 + range_fade_only_sky 8 + range_fade_radial 6 = 28, 07.11.2026)
#   = 70. (uv1 создаётся, но не подключён — горизонт живёт второй секцией меша.) При изменении графа — обновить.
#   Сверено с живым ассетом 03.09.2026: подключённых узлов было ровно 41 (до Opacity), остальные 39 — фантомы бага
#   DeleteAllMaterialExpressions (дубли SkyClouds×4, FogInvRange×4, DomeScale×3, …).
MASTER_EXPRESSIONS = 70
# TranslucencySortPriority компонента Dome: меньший приоритет рисуется РАНЬШЕ (BasePassRendering.cpp:317) — купол
#   должен лечь под все прочие транслюценты (CloudField 3D-облака, эффекты 5, LayeredClouds 4, Login 1).
#   То же значение — умолчание конструктора AAsdaSkyDomeActor (AsdaSkyDomeActor.cpp), здесь ставится явно.
DOME_SORT_PRIORITY = -1000
# Ровно столько узлов строит build_master_horizon(): t (1) + _append_fog_chain (17) + hb, ob, cl (3) + append_range_fade (28) = 49.
MASTER_HORIZON_EXPRESSIONS = 49

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
AT  = unreal.AssetToolsHelpers.get_asset_tools()

# ---------- 1. Данные оригинала ----------
def parse_jbm(path):
    """SKY_DOME.JBM → (индексы u16, вершины по 11 float как есть, число вершин, число треугольников) меша 0.
    Шапка и смещения Pass1 — общий `asda_jbm` (parse_header/pass1_offsets). У купола всегда 1 меш
    (проверено: 18/18 карт клиента), прежний разбор считал так же."""
    b = open(path, "rb").read()
    if not _JBM.is_jbm(b): raise RuntimeError("не JBM: %s" % path)
    hdr = _JBM.parse_header(b)
    if hdr is None: raise RuntimeError("битый JBM (число мешей 0 или > 4096): %s" % path)
    r0 = hdr["recs"][0]                                   # сабсет-запись (у скайдома всегда 1)
    vtx = r0["V"]; tri = r0["tri"]
    geo, vbase = _JBM.pass1_offsets(hdr)[0][0][:2]       # индексы u16×3 → вершины 44Б=11 float
    idx = struct.unpack_from("<%dH" % (tri*3), b, geo)
    verts = [struct.unpack_from("<11f", b, vbase + v*44) for v in range(vtx)]
    return idx, verts, vtx, tri

def parse_ter_sky(path):
    """(spd, c5) из заголовка .TER — скорости скролла слоёв купола и c5 fog-градиента."""
    return _TER.TerFile(path).sky_scroll()   # было своё чтение по индексам 2912/2916

def parse_mapraw_fog(path):
    # scene-заголовок .mapraw: AMRW, ver u32, name (len+bytes), fog ARGB u32, fogStart f32, fogEnd f32
    sc = _MR.read_scene(path, strict=False)  # не-AMRW → None, дальше прежний фолбэк
    if sc is None: return (1.0, 1.0, 1.0), 10000.0, 12500.0
    return sc["fog_rgb"], sc["fogStart"], sc["fogEnd"]

def ensure_star_texture():
    # звёздная текстура ОДНА на все карты: system/space11_0.nds (переименованный BMP 2048²) → /Game/Sky/T_StarSky
    if EAL.does_asset_exist("/Game/Sky/T_StarSky"): return
    src = r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/star_sky.png"
    if not os.path.isfile(src):
        unreal.log_warning("[SKY] нет star_sky.png — звёзды пропущены"); return
    import shutil
    tmp = src.replace("star_sky.png", "T_StarSky.png")
    shutil.copyfile(src, tmp)
    for a in _MIU.import_batch([tmp], "/Game/Sky"):   # ★ дедуп №2 Т17: общий импорт пачкой
        try: EAL.save_loaded_asset(a)
        except Exception: pass
    _fix_star_tex()
    unreal.log("[SKY] T_StarSky импортирована: %s" % EAL.does_asset_exist("/Game/Sky/T_StarSky"))

def _fix_star_tex():
    # звёзды = точки 1-2px: DXT+мипы их стирают (класс бага fx-текстур) → RGBA8+NoMips+NeverStream
    t = EAL.load_asset("/Game/Sky/T_StarSky")
    if t and isinstance(t, unreal.Texture2D):
        t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        t.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        t.set_editor_property("never_stream", True)
        EAL.save_loaded_asset(t)

# ---------- 2. Мастер-материал ----------
# Вертикальный fog-градиент неба (дизасм VS @EXE 0xCA5368): oFog = c5.w·((v0.y − c5.x)·c5.z) + c5.y,
# fog=0 у земли → чистый цвет тумана сцены, fog=1 выше → чистое небо; c5=(T[2916],T[2917],1/T[2918],T[2919]).
# В UE: localY = (WorldPos.z − ObjectPos.z)/DomeScale; final = lerp(FogColor, color, clamp(fog,0,1)).
def _append_fog_chain(m, expr, color_node, color_out):
    wp  = expr(unreal.MaterialExpressionWorldPosition, -1400, 900)
    # ВАЖНО: ObjectPositionWS = центр BOUNDS (у купола на +14250 выше пивота!) → фог сдвигался на −142
    # игровых единицы и заливал весь купол. ActorPositionWS = позиция актора (пивот) — корректный ноль высоты.
    op  = expr(unreal.MaterialExpressionActorPositionWS, -1400, 1050)
    sub = expr(unreal.MaterialExpressionSubtract, -1250, 950)
    MEL.connect_material_expressions(wp, "", sub, "A"); MEL.connect_material_expressions(op, "", sub, "B")
    mz  = expr(unreal.MaterialExpressionComponentMask, -1100, 950)
    mz.set_editor_property("r", False); mz.set_editor_property("g", False)
    mz.set_editor_property("b", True);  mz.set_editor_property("a", False)
    MEL.connect_material_expressions(sub, "", mz, "")
    def sparam(nm, dv, x, y):
        s = expr(unreal.MaterialExpressionScalarParameter, x, y)
        s.set_editor_property("parameter_name", nm); s.set_editor_property("default_value", dv)
        return s
    pScale = sparam("DomeScale", 100.0, -1100, 1100)
    dv  = expr(unreal.MaterialExpressionDivide, -950, 950)
    MEL.connect_material_expressions(mz, "", dv, "A"); MEL.connect_material_expressions(pScale, "", dv, "B")
    pOff = sparam("FogHeightOff", 0.0, -950, 1100)
    s1  = expr(unreal.MaterialExpressionSubtract, -800, 950)
    MEL.connect_material_expressions(dv, "", s1, "A"); MEL.connect_material_expressions(pOff, "", s1, "B")
    pInv = sparam("FogInvRange", 0.005, -800, 1100)
    m1f = expr(unreal.MaterialExpressionMultiply, -650, 950)
    MEL.connect_material_expressions(s1, "", m1f, "A"); MEL.connect_material_expressions(pInv, "", m1f, "B")
    pMul = sparam("FogMul", 1.0, -650, 1100)
    m2f = expr(unreal.MaterialExpressionMultiply, -500, 950)
    MEL.connect_material_expressions(m1f, "", m2f, "A"); MEL.connect_material_expressions(pMul, "", m2f, "B")
    pAdd = sparam("FogAdd", 0.0, -500, 1100)
    ad  = expr(unreal.MaterialExpressionAdd, -350, 950)
    MEL.connect_material_expressions(m2f, "", ad, "A"); MEL.connect_material_expressions(pAdd, "", ad, "B")
    cl  = expr(unreal.MaterialExpressionClamp, -200, 950)
    MEL.connect_material_expressions(ad, "", cl, "")
    fc  = expr(unreal.MaterialExpressionVectorParameter, -350, 1150)
    fc.set_editor_property("parameter_name", "FogColor")
    fc.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
    lp  = expr(unreal.MaterialExpressionLinearInterpolate, -50, 950)
    MEL.connect_material_expressions(fc, "", lp, "A")               # fog=0 → чистый цвет тумана
    MEL.connect_material_expressions(color_node, color_out, lp, "B")
    MEL.connect_material_expressions(cl, "", lp, "Alpha")
    return lp
# ★ 07.11.2026 (пачка 14, баг 2): РАСТВОРЕНИЕ ДАЛЬНИХ СИЛУЭТОВ. Объект в полном тумане — плоский силуэт цвета тумана, а небо другого
#   цвета: на отсечке силуэт исчезал разом. Купол рисуется поверх всего (disable_depth_test) с непрозрачностью по глубине пикселя:
#   небо (глубины нет) = как было, ближе FadeStart = 0 (объект виден), между FadeStart и FadeEnd силуэт растворяется в небе.
#   Числа даёт UAsdaViewRangeSubsystem через MPC_AsdaViewRange; значения по умолчанию (1e9) = «ничего не растворять»: логин, редактор.
#   Растворяются только силуэты на фоне неба: рельеф и вода пишутся в CustomDepth (UAsdaViewRangeSubsystem), и там, где они есть за объектом
#   или вместо него, купол не рисуется: иначе дальний рельеф растворялся бы и открывал нижнюю часть купола (кадр castle_of_chess).
RANGE_FADE_MPC = "/Game/Materials/MPC_AsdaViewRange"
RANGE_FADE_SKY_DEPTH = 500000.0   # глубже этого пикселя геометрии нет: карта меньше 200 000 юн
def range_fade_radial(expr, op, sd):
    """Расстояние до пикселя вместо глубины по оси взгляда: туман и отсечка меряют расстояние, иначе у края кадра объект отсекался недорастворённым.
    Глубина × (расстояние до точки купола / глубина точки купола). Возвращает выходной узел."""
    wp = expr(unreal.MaterialExpressionWorldPosition, -1250, 1400)
    cp = expr(unreal.MaterialExpressionCameraPositionWS, -1250, 1500)
    ds = op(unreal.MaterialExpressionDistance, wp, cp, -1100, 1450)
    pd = expr(unreal.MaterialExpressionPixelDepth, -1100, 1550)
    rt = op(unreal.MaterialExpressionDivide, ds, pd, -950, 1500)
    return op(unreal.MaterialExpressionMultiply, sd, rt, -800, 1500)

def range_fade_only_sky(expr, op, const, fade):
    """fade × «в пикселе нет рельефа»: CustomDepth глубже RANGE_FADE_SKY_DEPTH. Возвращает выходной узел."""
    sc = expr(unreal.MaterialExpressionSceneTexture, -700, 2050)
    sc.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_CUSTOM_DEPTH)
    mk = expr(unreal.MaterialExpressionComponentMask, -550, 2050)
    for c, on in (("r", True), ("g", False), ("b", False), ("a", False)): mk.set_editor_property(c, on)
    MEL.connect_material_expressions(sc, "Color", mk, "")
    kd = const(RANGE_FADE_SKY_DEPTH, -550, 2150)
    s4 = op(unreal.MaterialExpressionSubtract, mk, kd, -400, 2050)
    kk = const(1.0e-5, -400, 2150)
    m2 = op(unreal.MaterialExpressionMultiply, s4, kk, -250, 2050)
    st3 = expr(unreal.MaterialExpressionSaturate, -120, 2050)
    MEL.connect_material_expressions(m2, "", st3, "")
    return op(unreal.MaterialExpressionMultiply, fade, st3, -20, 1950)

def append_range_fade(m, expr, base_node, base_out):
    """Цепочка непрозрачности купола: base × max(saturate((d − FadeStart)/(FadeEnd − FadeStart)), пиксель неба). Возвращает выходной узел."""
    mpc = EAL.load_asset(RANGE_FADE_MPC)
    if not mpc: raise RuntimeError("[SKY] нет %s" % RANGE_FADE_MPC)
    def coll(nm, x, y):
        e = expr(unreal.MaterialExpressionCollectionParameter, x, y)
        e.set_editor_property("collection", mpc); e.set_editor_property("parameter_name", nm)
        return e
    def const(v, x, y):
        e = expr(unreal.MaterialExpressionConstant, x, y); e.set_editor_property("r", v)
        return e
    def op(cls, a, b, x, y):
        e = expr(cls, x, y)
        MEL.connect_material_expressions(a, "", e, "A"); MEL.connect_material_expressions(b, "", e, "B")
        return e
    sd = expr(unreal.MaterialExpressionSceneDepth, -700, 1500)
    ps = coll("FadeStart", -700, 1650); pe = coll("FadeEnd", -700, 1750)
    s1 = op(unreal.MaterialExpressionSubtract, range_fade_radial(expr, op, sd), ps, -500, 1550)
    s2 = op(unreal.MaterialExpressionSubtract, pe, ps, -500, 1700)
    dv = op(unreal.MaterialExpressionDivide, s1, s2, -350, 1600)
    st = expr(unreal.MaterialExpressionSaturate, -200, 1600)
    MEL.connect_material_expressions(dv, "", st, "")
    kd = const(RANGE_FADE_SKY_DEPTH, -500, 1850)
    s3 = op(unreal.MaterialExpressionSubtract, sd, kd, -350, 1800)
    kk = const(1.0e-5, -350, 1950)
    m1 = op(unreal.MaterialExpressionMultiply, s3, kk, -200, 1800)
    st2 = expr(unreal.MaterialExpressionSaturate, -80, 1800)
    MEL.connect_material_expressions(m1, "", st2, "")
    mx = op(unreal.MaterialExpressionMax, range_fade_only_sky(expr, op, const, st), st2, 40, 1700)
    ob = expr(unreal.MaterialExpressionMultiply, 160, 1600)
    MEL.connect_material_expressions(base_node, base_out, ob, "A"); MEL.connect_material_expressions(mx, "", ob, "B")
    m.set_editor_property("disable_depth_test", True)
    return ob

def build_master():
    name = MASTER.rsplit("/",1)[-1]
    if EAL.does_asset_exist(MASTER):
        m = EAL.load_asset(MASTER)
        # in-place (delete_asset рвёт parent у MIC). ★ В ЦИКЛЕ до пустого: один delete_all оставляет
        #   каждый второй узел (UE 5.8, MaterialEditingLibrary.cpp:559-568) → фантомы/дубли параметров.
        _MG.clear_expressions(m, log=unreal.log, tag="[SKY]")
    else:
        m = AT.create_asset(name, MASTER.rsplit("/",1)[0], unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)             # оригинал: CULLMODE=NONE
    # ★ 05.09.2026: TRANSLUCENT + Apply Fogging=off — единственный в UE 5.8 способ вывести меш из-под ExponentialHeightFog
    #   (is_sky от HeightFog НЕ спасает, Material.h:1088; раньше тут стояло is_sky=True с ложным комментарием «иммунитет»).
    #   Непрозрачность = 1 явно (узел opq ниже), глубина транслюцентом не пишется — для неба безвредно.
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("is_sky", False)               # флаг «Unlit and Opaque only»; SkyPass для транслюцента не нужен
    m.set_editor_property("use_translucency_vertex_fog", False)   # «Apply Fogging» = off: сценовый туман купол не трогает
    def expr(cls, x, y): return MEL.create_material_expression(m, cls, x, y)
    uv0 = expr(unreal.MaterialExpressionTextureCoordinate, -1400, 0)
    uv1 = expr(unreal.MaterialExpressionTextureCoordinate, -1400, 600)
    uv1.set_editor_property("coordinate_index", 1)
    tm  = expr(unreal.MaterialExpressionTime, -1400, 300)
    # ScrollBoost — множитель скорости обоих слоёв (пользовательский тюнинг: оригинальные скорости
    # многих карт почти статичны, alpen 0.001 UV/с; boost делает дрейф видимым, 1.0 = как оригинал)
    sb = expr(unreal.MaterialExpressionScalarParameter, -1400, 450)
    sb.set_editor_property("parameter_name", "ScrollBoost"); sb.set_editor_property("default_value", 1.0)
    tmB = expr(unreal.MaterialExpressionMultiply, -1320, 300)
    MEL.connect_material_expressions(tm, "", tmB, "A"); MEL.connect_material_expressions(sb, "", tmB, "B")
    def scroll_uv(su, sv, ypos):
        s_u = expr(unreal.MaterialExpressionScalarParameter, -1250, ypos);   s_u.set_editor_property("parameter_name", su); s_u.set_editor_property("default_value", 0.0)
        s_v = expr(unreal.MaterialExpressionScalarParameter, -1250, ypos+80); s_v.set_editor_property("parameter_name", sv); s_v.set_editor_property("default_value", 0.0)
        ap  = expr(unreal.MaterialExpressionAppendVector, -1100, ypos)
        MEL.connect_material_expressions(s_u, "", ap, "A"); MEL.connect_material_expressions(s_v, "", ap, "B")
        mul = expr(unreal.MaterialExpressionMultiply, -950, ypos)
        MEL.connect_material_expressions(ap, "", mul, "A"); MEL.connect_material_expressions(tmB, "", mul, "B")
        add = expr(unreal.MaterialExpressionAdd, -800, ypos)
        MEL.connect_material_expressions(uv0, "", add, "A"); MEL.connect_material_expressions(mul, "", add, "B")
        return add
    uvA = scroll_uv("ScrollU1", "ScrollV1", -100)        # слой cloude1 (в оригинале скроллится тоже: rec+624/628)
    uvB = scroll_uv("ScrollU2", "ScrollV2", 250)         # слой cloude2 (rec+632/636)
    def tex_param(pname, uv_expr, x, y):
        t = expr(unreal.MaterialExpressionTextureSampleParameter2D, x, y)
        t.set_editor_property("parameter_name", pname)
        if uv_expr is not None: MEL.connect_material_expressions(uv_expr, "", t, "UVs")
        return t
    tBase   = tex_param("SkyBase",    uvA, -600, -100)   # stage0 SELECTARG1
    tClouds = tex_param("SkyClouds",  uvB, -600, 250)    # stage1 BLENDTEXTUREALPHA
    l1 = expr(unreal.MaterialExpressionLinearInterpolate, -350, 60)
    MEL.connect_material_expressions(tBase, "RGB", l1, "A")
    MEL.connect_material_expressions(tClouds, "RGB", l1, "B")
    MEL.connect_material_expressions(tClouds, "A", l1, "Alpha")
    # ЗВЁЗДЫ (stage3 оригинала, g_bFXStarSky): space11_0.nds (BMP без альфы) АДДИТИВНО, oT3=uv0 БЕЗ скролла
    tStar = expr(unreal.MaterialExpressionTextureSampleParameter2D, -350, 450)
    tStar.set_editor_property("parameter_name", "StarTex")
    _st = EAL.load_asset("/Game/Sky/T_StarSky")
    if _st: tStar.set_editor_property("texture", _st)
    MEL.connect_material_expressions(uv0, "", tStar, "UVs")
    sInt = expr(unreal.MaterialExpressionScalarParameter, -350, 650)
    sInt.set_editor_property("parameter_name", "StarIntensity"); sInt.set_editor_property("default_value", 1.0)
    sMul = expr(unreal.MaterialExpressionMultiply, -180, 480)
    MEL.connect_material_expressions(tStar, "RGB", sMul, "A"); MEL.connect_material_expressions(sInt, "", sMul, "B")
    sAdd = expr(unreal.MaterialExpressionAdd, -60, 200)
    MEL.connect_material_expressions(l1, "", sAdd, "A"); MEL.connect_material_expressions(sMul, "", sAdd, "B")
    # SkyBoost — множитель сочности неба (тонемап UE глушит бирюзу оригинала; 1.0 = как есть)
    sb2 = expr(unreal.MaterialExpressionScalarParameter, -60, 400)
    sb2.set_editor_property("parameter_name", "SkyBoost"); sb2.set_editor_property("default_value", 1.0)
    bMul = expr(unreal.MaterialExpressionMultiply, 60, 250)
    MEL.connect_material_expressions(sAdd, "", bMul, "A"); MEL.connect_material_expressions(sb2, "", bMul, "B")
    fogged = _append_fog_chain(m, expr, bMul, "")
    MEL.connect_material_property(fogged, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    opq = expr(unreal.MaterialExpressionConstant, 60, 700)          # Opacity = 1: купол непрозрачен, транслюцент — только ради фога
    opq.set_editor_property("r", 1.0)
    if not MEL.connect_material_property(append_range_fade(m, expr, opq, ""), "", unreal.MaterialProperty.MP_OPACITY):
        raise RuntimeError("[SKY] Opacity не подключился (connect_material_property=False)")
    # ★ Контроль ДО сохранения: ровно MASTER_EXPRESSIONS узлов и без дублей имён параметров;
    #   при расхождении RuntimeError, ассет не сохраняется (фантомы старого графа).
    _MG.check_graph(m, expected=MASTER_EXPRESSIONS, log=unreal.log, tag="[SKY]")
    MEL.recompile_material(m); m.modify(); EAL.save_loaded_asset(m)
    node = MEL.get_material_property_input_node(m, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.log("[SKY] мастер %s: Emissive=%s" % (name, node.get_class().get_name() if node else "ОБРЫВ!"))
    return m

# Горизонт (stage2 BLENDTEXTUREALPHA) — python-API не даёт 2-й UV-канал StaticMeshDescription →
# та же геометрия ВТОРОЙ секцией с uv1 в канале 0 + translucent-мастер поверх opaque-секции облаков.
MASTER_HORIZON = "/Game/Materials/M_AsdaSkyHorizon"
def build_master_horizon():
    name = MASTER_HORIZON.rsplit("/",1)[-1]
    if EAL.does_asset_exist(MASTER_HORIZON):
        m = EAL.load_asset(MASTER_HORIZON)
        _MG.clear_expressions(m, log=unreal.log, tag="[SKY]")   # ★ в цикле до пустого (баг UE 5.8, см. build_master)
    else:
        m = AT.create_asset(name, MASTER_HORIZON.rsplit("/",1)[0], unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)
    try: m.set_editor_property("use_translucency_vertex_fog", False)   # лента на 10 км — сценовый туман её растворил бы
    except Exception:
        try: m.set_editor_property("apply_fogging", False)
        except Exception: pass
    def expr(cls, x, y): return MEL.create_material_expression(m, cls, x, y)
    t = expr(unreal.MaterialExpressionTextureSampleParameter2D, -400, 0)
    t.set_editor_property("parameter_name", "SkyHorizon")
    fogged = _append_fog_chain(m, expr, t, "RGB")   # D3D: fog на src ДО альфа-бленда
    MEL.connect_material_property(fogged, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    # HorizonBoost — множитель непрозрачности ленты горизонта (пользовательский тюнинг; 1.0 = оригинал)
    hb = expr(unreal.MaterialExpressionScalarParameter, -400, 350)
    hb.set_editor_property("parameter_name", "HorizonBoost"); hb.set_editor_property("default_value", 1.0)
    ob = expr(unreal.MaterialExpressionMultiply, -200, 300)
    MEL.connect_material_expressions(t, "A", ob, "A"); MEL.connect_material_expressions(hb, "", ob, "B")
    cl = expr(unreal.MaterialExpressionClamp, -50, 300)
    MEL.connect_material_expressions(ob, "", cl, "")
    MEL.connect_material_property(append_range_fade(m, expr, cl, ""), "", unreal.MaterialProperty.MP_OPACITY)
    _MG.check_graph(m, expected=MASTER_HORIZON_EXPRESSIONS, log=unreal.log, tag="[SKY]")   # ★ ровно 21 узел, без дублей, иначе не сохранять
    MEL.recompile_material(m); m.modify(); EAL.save_loaded_asset(m)
    node = MEL.get_material_property_input_node(m, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.log("[SKY] мастер %s: Emissive=%s" % (name, node.get_class().get_name() if node else "ОБРЫВ!"))
    return m

# ---------- 3. Статик-меш купола ----------
def find_texture(stem):
    """Текстура купола в DEST_ROOT/Texture (DEST_ROOT читается при вызове: его переставляет asda_build_map_level) —
    общая реализация `asda_model_import_ue.find_texture_in_dir` (дедуп №2 Т13)."""
    return _MIU.find_texture_in_dir(DEST_ROOT, stem)

def build_dome_mesh(idx, verts):
    name = "SM_skydome"; path = DEST_ROOT + "/Models/" + name
    if EAL.does_asset_exist(path): EAL.delete_asset(path)      # всегда свежий (bounds-закон)
    sm = AT.create_asset(name, DEST_ROOT + "/Models", unreal.StaticMesh, None)
    desc = sm.create_static_mesh_description()
    gClouds = desc.create_polygon_group(); desc.set_polygon_group_material_slot_name(gClouds, "Clouds")
    gHoriz  = desc.create_polygon_group(); desc.set_polygon_group_material_slot_name(gHoriz, "Horizon")
    # секция 1: облака (uv0 планарная); секция 2: горизонт (uv1) — СВОИ вершины ×0.995:
    # co-planar с облачной секцией транслюцентный горизонт резался Z-тестом на равной глубине.
    def make_verts(scale):
        out = []
        for v in verts:
            # game LH Y-up → UE: C(x,y,z)=(-z,x,y) (как статика карт)
            vid = desc.create_vertex()
            desc.set_vertex_position(vid, unreal.Vector(-v[2]*scale, v[0]*scale, v[1]*scale))
            out.append(vid)
        return out
    vids  = make_verts(1.0)
    vidsH = make_verts(0.995)
    for grp, vv, (ua, va) in ((gClouds, vids, (7, 8)), (gHoriz, vidsH, (9, 10))):
        for t in range(len(idx)//3):
            i0, i1, i2 = idx[t*3], idx[t*3+1], idx[t*3+2]
            if i0 == i1 or i1 == i2 or i0 == i2: continue
            tri = []
            for k in (i0, i1, i2):
                vi = desc.create_vertex_instance(vv[k])
                desc.set_vertex_instance_uv(vi, unreal.Vector2D(verts[k][ua], verts[k][va]), 0)
                tri.append(vi)
            desc.create_triangle(grp, tri)
    sm.build_from_static_mesh_descriptions([desc], False, False)   # полный билд: чистые bounds (закон неба)
    return sm

# ---------- 4. MIC + расстановка ----------
def place_map_fog(eas, fog_rgb, fog_start, dens):
    """Актор `MapFog` (ExponentialHeightFog) с параметрами сценового тумана оригинала: density (дистанционная
    экспонента, 1/1000 юн), start_distance = fogStart, без вертикального градиента (falloff 1e-4), кап 0.9,
    цвет = fog_color_ue(fog ARGB .mapraw): sRGB→linear при ASDA_FOG_SRGB=1 (умолч., ★ 06.09.2026 fog_srgb), raw при 0;
    cutoff 0 = выкл (ВАЖНО №1 ревизии сета A).
    ★ 06.09.2026: вынесено из main() — той же функцией ставится туман на 4 закрытые карты без купола
    (Tools/asda_indoor_fog.py). Возвращает актор. Существующие ExponentialHeightFog НЕ трогает — это дело вызывающего."""
    fog_a = eas.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0))
    fog_a.set_actor_label("MapFog")
    fc = fog_a.get_component_by_class(unreal.ExponentialHeightFogComponent.static_class())
    fc.set_editor_property("fog_density", float(dens))
    fc.set_editor_property("fog_height_falloff", 0.0001)   # без вертикального градиента (фог оригинала дистанционный)
    fc.set_editor_property("start_distance", max(float(fog_start), 0.0))
    fc.set_editor_property("fog_max_opacity", 0.9)
    c = fog_color_ue(fog_rgb)     # ★ 06.09 fog_srgb: одна функция цвета (sRGB→linear), не raw
    try: fc.set_editor_property("fog_inscattering_luminance", unreal.LinearColor(c[0], c[1], c[2], 1.0))
    except Exception:
        fc.set_editor_property("fog_inscattering_color", unreal.LinearColor(c[0], c[1], c[2], 1.0))
    return fog_a


def main():
    # ★ 16.09.2026 (ревизия раздела 22, СРЕДН-2): имя карты приходит из ASDA_MAP и НИ С ЧЕМ не сверяется
    #   (asda_maps.map_asset_name только чистит символы) — ASDA_MAP=Login дал бы пути сцены логина. Уровень
    #   логина открывает только ASDA_ALLOW_LOGIN_MAP_WRITE=1 (единое правило, asda_login_guard).
    # ★★★ Здесь дыра была РЕАЛЬНОЙ, а не предупредительной: проверка уровня (does_asset_exist(DEST_LEVEL)) стоит
    #   ниже, уже ПОСЛЕ создания ассетов — build_dome_mesh и make_mic пишут /Game/Map/<карта>/Models/SM_skydome и
    #   /Game/Map/<карта>/Materials/MI_skydome|MI_skyhorizon с сохранением. При ASDA_MAP=Login это создало бы и
    #   сохранило пакеты ПОД деревом сцены логина, хотя сам уровень так и не нашёлся бы и работа бы прервалась.
    import asda_login_guard as _LG
    # ★ 16.09.2026 (ревизия §7, п. 5): плюс сверка имени карты с реестром (Tools/data/asda_map_registry.json) —
    #   опечатка в ASDA_MAP иначе молча создаёт /Game/Map/<мусор>/Models и /Game/Map/<мусор>/Materials.
    if _LG.login_map_write_blocked(DEST_ROOT, "asda_build_sky_dome.py") or \
       _LG.map_name_blocked(DEST_ROOT, "asda_build_sky_dome.py"): return
    mdir = os.path.join(CLIENT_MAP, MAP_NAME)
    jbm = os.path.join(mdir, "SKY_DOME.JBM")
    ter = _TER.ter_path(MAP_NAME)              # ★ единый резолв пути (учитывает регистр имени файла)
    if not os.path.isfile(jbm):
        unreal.log_warning("[SKY] %s: нет SKY_DOME.JBM — карта без неба, пропуск" % MAP_NAME); return
    idx, verts, vtx, tri = parse_jbm(jbm)
    spd, c5 = parse_ter_sky(ter) if ter else ((0, 0, 0, 0), (0, 0, 1, 1))
    mr = _P.mapraw(MAP_NAME)
    (fog_rgb, fog_start, fog_end) = parse_mapraw_fog(mr) if os.path.isfile(mr) else ((1, 1, 1), 10000.0, 12500.0)
    unreal.log("[SKY] %s: JBM vtx=%d tri=%d, spd=%s c5=%s fog raw=%s → UE=%s (ASDA_FOG_SRGB=%d) start/end=%.0f/%.0f" % (
        MAP_NAME, vtx, tri, list(spd), list(c5), [round(v, 4) for v in fog_rgb], [round(v, 4) for v in fog_color_ue(fog_rgb)],
        int(FOG_SRGB), fog_start, fog_end))

    ensure_star_texture()
    build_master()
    build_master_horizon()
    sm = build_dome_mesh(idx, verts)

    def make_mic(nm, master_path):
        p = DEST_ROOT + "/Materials/" + nm
        mi = EAL.load_asset(p) if EAL.does_asset_exist(p) else \
             AT.create_asset(nm, DEST_ROOT + "/Materials", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(mi, EAL.load_asset(master_path))
        return mi
    mic  = make_mic("MI_skydome", MASTER)          # облака (opaque)
    micH = make_mic("MI_skyhorizon", MASTER_HORIZON)  # горизонт (translucent поверх)
    for mi, pname, stem in ((mic,"SkyBase","SKY_DOME_CLOUDE1"), (mic,"SkyClouds","SKY_DOME_CLOUDE2"),
                            (micH,"SkyHorizon","SKY_DOME")):
        t = find_texture(stem)
        if t: MEL.set_material_instance_texture_parameter_value(mi, pname, t)
        else: unreal.log_warning("[SKY] нет текстуры %s" % stem)
    # скорости: слой1 U=V=spd[0] (баг оригинала: T[2913] игнорируется), слой2 = spd[2]/spd[3].
    # dt = мс/кадр (CONFIRMED timerUpdateHighRes @0x965D60: (qpcΔ)*1000/freq) → UV/с = spd×1000.
    K = 1000.0
    for pname, val in (("ScrollU1", spd[0]*K), ("ScrollV1", spd[0]*K), ("ScrollU2", spd[2]*K), ("ScrollV2", spd[3]*K)):
        MEL.set_material_instance_scalar_parameter_value(mic, pname, val)
    # fog-градиент (VS: oFog = c5.w·((y−c5.x)/T[2918]) + c5.y; c5 из .TER, fogColor из .mapraw)
    inv = (1.0 / c5[2]) if abs(c5[2]) > 1e-9 else 0.0
    for mi in (mic, micH):
        MEL.set_material_instance_scalar_parameter_value(mi, "DomeScale", 100.0)
        MEL.set_material_instance_scalar_parameter_value(mi, "FogHeightOff", c5[0])
        MEL.set_material_instance_scalar_parameter_value(mi, "FogInvRange", inv)
        MEL.set_material_instance_scalar_parameter_value(mi, "FogMul", c5[3])
        MEL.set_material_instance_scalar_parameter_value(mi, "FogAdd", c5[1])
        fcol = fog_color_ue(fog_rgb)   # ★ 06.09 fog_srgb: тот же цвет, что у MapFog (иначе горизонт купола и туман разойдутся по яркости)
        MEL.set_material_instance_vector_parameter_value(mi, "FogColor", unreal.LinearColor(fcol[0], fcol[1], fcol[2], 1.0))
    # пользовательский тюнинг (подтверждён юзером 2026-07-28): горизонт заметнее ×1.5, дрейф облаков ×9
    # ★★★ 2026-08-27: дрейф был 10.0 в MIC, но актор `AAsdaSkyDomeActor` перебивал его в рантайме
    #   на 9.0 (`CloudsSpeedBoost`), то есть В ИГРЕ РАБОТАЛО 9.0. Рантайм-присвоение убрано
    #   (§6.2 аудита `Reports/audit_runtime_style.md`), поэтому здесь ставим ровно 9.0 —
    #   иначе пересборка неба вернула бы 10.0 и облака поехали бы быстрее, чем было.
    MEL.set_material_instance_scalar_parameter_value(micH, "HorizonBoost", float(os.environ.get("ASDA_SKY_HORIZON_BOOST", "1.725")))
    MEL.set_material_instance_scalar_parameter_value(mic, "ScrollBoost", float(os.environ.get("ASDA_SKY_SCROLL_BOOST", "9.0")))
    # ослабление фога на ленте горизонта (юзер: «горизонт еле виден» — его смывал именно фог):
    # f' = w·f + (1−w): при w=0.5 у земли лента видна наполовину, а не тонет в fogColor целиком
    w = float(os.environ.get("ASDA_SKY_HORIZON_FOG", "0.425"))
    MEL.set_material_instance_scalar_parameter_value(micH, "FogMul", c5[3] * w)
    MEL.set_material_instance_scalar_parameter_value(micH, "FogAdd", c5[1] * w + (1.0 - w))
    EAL.save_loaded_asset(mic); EAL.save_loaded_asset(micH)
    sm.set_editor_property("static_materials", [
        unreal.StaticMaterial(material_interface=mic,  material_slot_name="Clouds"),
        unreal.StaticMaterial(material_interface=micH, material_slot_name="Horizon")])
    EAL.save_loaded_asset(sm)

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not EAL.does_asset_exist(DEST_LEVEL):
        unreal.log_warning("[SKY] нет уровня %s" % DEST_LEVEL); return
    les.load_level(DEST_LEVEL)
    # снести старую сферу SM_sky и прежние SkyDome-акторы
    for a in list(eas.get_all_level_actors()):
        cls = a.get_class().get_name()
        if cls in ("AsdaSkyDomeActor", "AsdaLayeredCloudsActor"):   # LayeredClouds сняты с вооружения (юзер 2026-07-28)
            eas.destroy_actor(a); continue
        if cls == "StaticMeshActor":
            c = a.static_mesh_component
            msh = c.static_mesh if c else None
            if msh and msh.get_name() == "SM_sky":
                eas.destroy_actor(a)
    for old in (DEST_ROOT + "/Models/SM_sky", DEST_ROOT + "/Materials/MI_sky"):
        if EAL.does_asset_exist(old): EAL.delete_asset(old)
    # СЦЕНОВЫЙ ТУМАН (линейный фог оригинала fogStart/fogEnd из .mapraw ≈ ExponentialHeightFog):
    # растворяет дальние холмы/объекты в fogColor у горизонта (стык земли и неба как в игре).
    # Купол и лента горизонта — translucent с Apply Fogging=off (05.09.2026) — туману НЕ подвержены: у них свой VS-градиент.
    #   (До 05.09 купол был opaque is_sky, и комментарий «иммунитет» был ЛОЖНЫМ — HeightFog его заливал.) fog_cutoff_distance
    #   не ставится (0 = выкл): cutoff по куполу красил чёрным пустоту за краем карты (review_setA_rollout.md ВАЖНО №1).
    for old in [x for x in eas.get_all_level_actors() if x.get_class().get_name() == "ExponentialHeightFog"]:
        eas.destroy_actor(old)
    # линейный фог оригинала: 0 на fogStart → 1 на fogEnd. Экспонента с плотностью 3/fogEnd давала 74%
    # УЖЕ на fogStart («всё белое»). Калибровка: старт = fogStart, ~86% на fogEnd.
    # ★ 06.09.2026: умолчание = формула сета A (review_setA_fix.md): FOGK/(fogEnd−fogStart), FOGK 2737 → 73 % на fogEnd.
    #   Старое 1e-6 = «тумана нет» (UE делит на 1000 и берёт 2^-x) и откатывало доводку A при перегенерации неба.
    #   ASDA_SKY_FOG_DENSITY (абсолютная плотность) по-прежнему перекрывает, если задана явно.
    _abs = os.environ.get("ASDA_SKY_FOG_DENSITY")
    _span = max(1.0, float(fog_end) - float(fog_start))
    dens = float(_abs) if _abs else float(os.environ.get("ASDA_FOGK", "2737")) / _span
    place_map_fog(eas, fog_rgb, fog_start, dens)   # ★ 06.09: общая функция — ею же ставится туман на 4 закрытые карты (asda_indoor_fog.py)
    a = eas.spawn_actor_from_class(unreal.AsdaSkyDomeActor, unreal.Vector(0, 0, 0))
    a.set_actor_label("SkyDome")
    a.set_actor_scale3d(unreal.Vector(100, 100, 100))    # R=1000→100000: углы сохраняются, глубина не режет мир
    dome = a.get_editor_property("dome")
    dome.set_static_mesh(sm)
    dome.set_material(0, mic)
    dome.set_material(1, micH)
    dome.set_editor_property("translucency_sort_priority", DOME_SORT_PRIORITY)   # под всеми транслюцентами (см. константу)
    les.save_current_level()
    unreal.log("[SKY] %s: купол размещён (scale 100, follow camera, sort_priority=%d)" % (MAP_NAME, int(dome.get_editor_property("translucency_sort_priority"))))


def _script_main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global DEST_LEVEL, DEST_ROOT
    DEST_ROOT = "/Game/Map/%s" % _asset(MAP_NAME)
    DEST_LEVEL= "%s/%s" % (DEST_ROOT, _asset(MAP_NAME))
    main()


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    _script_main()
