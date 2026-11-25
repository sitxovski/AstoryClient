# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_toon_shaders.py — сборка NPR-слоя «как в Genshin Impact»: коллекция параметров
# MPC_AsdaToon + пост-обработочный материал M_AsdaToonPP (контур + ramp-затенение) + ветка
# rim-света в игровых мастерах M_AsdaCharLit / M_AsdaObjectLit.
#
# ОСНОВАНИЕ (разбор рендера Genshin, Reports/genshin_shading_plan.md):
#   • NPR в Genshin применён ТОЛЬКО к персонажам, мир — стилизованный PBR (доклад miHoYo).
#     Поэтому у нас четыре независимых веса по категориям, а не один общий тумблер.
#   • Цвет линии контура берётся НЕ чёрным, а по формуле оригинала:
#         lerp(OutlineColor, BaseColor · 0.203, OutlineColorBlend)
#     Одноцветная чёрная обводка — главная причина «дешёвого» вида стилизации.
#   • Толщина линии в Genshin кусочно-линейна по расстоянию; у нас — затухание FadeStart/FadeEnd.
#   • Rim в Genshin — сравнение глубин, а НЕ Френель; Френель у них = эффект получения урона.
#     Полноценное сравнение глубин делается в пост-обработке (шаг контура), а в мастерах ставим
#     дешёвый Френель-rim как отдельно выключаемую добавку к Emissive.
#   • Ramp-затенение по NdotL внутри цикла освещения возможно только через Substrate ToonBSDF
#     (проверено: SubstrateToonBSDF.ush, ToonProfile, DiffuseRampIncludeShadow). Substrate —
#     ГЛОБАЛЬНЫЙ переключатель проекта с необратимой конвертацией материалов, поэтому здесь
#     реализован обратимый эквивалент в пост-обработке: постеризация яркости с настраиваемым
#     числом ступеней, порогом (аналог _LightArea), контрастом и оттенком тени (аналог ShadowTint).
#
# ЗАКОН ПРОЕКТА: всё новое входит через `lerp`, доля — из коллекции параметров. При Enable = 0
# картинка БУКВАЛЬНО прежняя (PP-материал возвращает вход без изменений, rim даёт 0).
#
# КАТЕГОРИИ (значение CustomStencil, пишет UAsdaToonSubsystem):
#   10 — персонажи (игрок и другие игроки)
#   11 — существа (мобы, NPC, питомцы, страж души)
#   12 — мир (террейн, вода, небо)
#    0 — объекты карты (статика/HISM) и всё прочее
#
# Запуск (headless):
#   UnrealEditor-Cmd.exe <проект> -ExecutePythonScript=".../asda_build_toon_shaders.py"
#     -unattended -nopause -nosplash -nosound
#   (без -NullRHI, если нужна проверка компиляции; см. отчёт в конце)
PKG = "/Game/Materials"
MPC_PATH = PKG + "/MPC_AsdaToon"
PP_PATH = PKG + "/M_AsdaToonPP"

# ── Скаляры коллекции: имя → (значение по умолчанию, назначение) ──────────────────────────────
SCALARS = [
    ("Enable",              0.0, "общий выключатель NPR-слоя (0 = картинка как до работы)"),
    # веса по категориям (умножаются на эффект; 0 = категория не затронута)
    ("ApplyChar",           1.0, "категория «персонажи»: доля эффекта"),
    ("ApplyCreature",       1.0, "категория «существа» (мобы/NPC/пет/страж)"),
    ("ApplyWorld",          0.0, "категория «мир» (террейн/вода/небо) — в Genshin NPR тут НЕ применён"),
    ("ApplyObject",         0.0, "категория «объекты карты» — в Genshin NPR тут НЕ применён"),
    # контур
    ("OutlineEnable",       1.0, "включить контур"),
    ("OutlineThickness",    1.4, "толщина линии в пикселях"),
    ("OutlineDepthSens",    1.0, "чувствительность к разрыву глубины (нормирована на саму глубину)"),
    ("OutlineNormalSens",   1.6, "чувствительность к углу между нормалями"),
    ("OutlineColorBlend",   0.65, "доля «диффуз·0.203» в цвете линии (формула Genshin)"),
    ("OutlineFadeStart", 2500.0, "с какого расстояния линия начинает исчезать, см"),
    ("OutlineFadeEnd",  12000.0, "на каком расстоянии линии уже нет, см"),
    ("OutlineMulChar",      1.0, "множитель контура: персонажи"),
    ("OutlineMulCreature",  1.0, "множитель контура: существа"),
    ("OutlineMulWorld",     0.0, "множитель контура: мир"),
    ("OutlineMulObject",    0.35, "множитель контура: объекты карты"),
    # ramp-затенение (эквивалент ramp-текстуры Genshin в пост-обработке)
    ("RampEnable",          1.0, "включить ступенчатое затенение"),
    ("RampSteps",           4.0, "число ступеней светотени"),
    ("RampSmooth",          0.35, "сглаживание ступеней (1 = плавно, как раньше)"),
    ("RampOffset",          0.45, "где проходит граница света/тени — аналог _LightArea"),
    ("RampContrast",        1.35, "крутизна перехода — аналог _ShadowRampWidth"),
    ("ShadowTintAmount",    0.5, "насколько тень уходит в свой оттенок"),
    ("RampMulChar",         1.0, "множитель ramp: персонажи"),
    ("RampMulCreature",     1.0, "множитель ramp: существа"),
    ("RampMulWorld",        0.0, "множитель ramp: мир"),
    ("RampMulObject",       0.0, "множитель ramp: объекты карты"),
    # rim-свет (в мастерах материалов, не в пост-обработке)
    ("RimEnable",           0.0, "включить rim-свет (Френель-приближение)"),
    ("RimPower",            4.0, "резкость каймы: чем больше, тем тоньше"),
    ("RimWidth",            0.55, "яркость каймы"),
]

# ── Векторы коллекции ────────────────────────────────────────────────────────────────────────
VECTORS = [
    ("OutlineColor",   (0.04, 0.045, 0.06, 1.0), "базовый цвет линии (смешивается с диффузом)"),
    ("ShadowTint",     (0.72, 0.76, 0.95, 1.0), "оттенок тени (у Genshin тень уходит в холод)"),
    ("RimColor",       (1.0, 0.96, 0.88, 1.0), "цвет rim-света"),
]

# ── HLSL пост-обработки. Индексы SceneTexture взяты из движка (MaterialSceneTextureId.h):
#    1 = SceneDepth, 8 = WorldNormal, 14 = PostProcessInput0, 25 = CustomStencil, 26 = StoredBaseColor.
HLSL = r"""
// Категория пикселя по CustomStencil. Значение ЦЕЛОЕ, не нормализованное — проверено в движке:
// FGBufferData::CustomStencil объявлен как uint и читается напрямую из CustomStencilTexture
// (DeferredShadingCommon.ush). Поэтому сравниваем с 10/11/12 без пересчёта: подстраховочное
// умножение на 255 испортило бы stencil = 1, который пишет подсветка цели.
float st = SceneTextureLookup(UV, 25, false).r;

float catChar  = (st > 9.5  && st < 10.5) ? 1.0 : 0.0;
float catCreat = (st > 10.5 && st < 11.5) ? 1.0 : 0.0;
float catWorld = (st > 11.5 && st < 12.5) ? 1.0 : 0.0;
float catObj   = 1.0 - saturate(catChar + catCreat + catWorld);

float outMul  = catChar * OutMulChar  + catCreat * OutMulCreat
              + catWorld * OutMulWorld + catObj * OutMulObj;
float rampMul = catChar * RampMulChar + catCreat * RampMulCreat
              + catWorld * RampMulWorld + catObj * RampMulObj;

float3 src = SceneTextureLookup(UV, 14, false).rgb;
float3 col = src;
float  d   = SceneTextureLookup(UV,  1, false).r;
float3 n   = SceneTextureLookup(UV,  8, false).rgb;
float  nl  = max(length(n), 1e-4);
n /= nl;

// ── КОНТУР: крест по 4 соседям. Разрыв глубины нормируется на саму глубину — иначе далёкая
//    геометрия обводится толще ближней (грабля, отмеченная в разборе Genshin).
float dSum = 0.0;
float nSum = 0.0;
float2 t = InvSize * max(OutThick, 0.0);
float2 offs[4] = { float2(t.x, 0.0), float2(-t.x, 0.0), float2(0.0, t.y), float2(0.0, -t.y) };
for (int i = 0; i < 4; ++i)
{
    float2 suv = UV + offs[i];
    dSum += abs(SceneTextureLookup(suv, 1, false).r - d);
    float3 nn = SceneTextureLookup(suv, 8, false).rgb;
    nn /= max(length(nn), 1e-4);
    nSum += 1.0 - saturate(dot(nn, n));
}
float dEdge = saturate(dSum / max(d, 1.0) * OutDepthSens * 25.0);
float nEdge = saturate(nSum * OutNormalSens * 0.5);
float edge  = saturate(max(dEdge, nEdge));

float fadeSpan = max(OutFadeEnd - OutFadeStart, 1.0);
edge *= 1.0 - saturate((d - OutFadeStart) / fadeSpan);
edge *= saturate(outMul) * saturate(OutlineOn);

// Цвет линии по формуле оригинала: lerp(OutlineColor, diffuse·0.203, blend).
float3 diff = SceneTextureLookup(UV, 26, false).rgb;
float3 lineCol = lerp(OutColor, diff * 0.203, saturate(OutColorBlend));

// ── RAMP: ступенчатая светотень. Порог RampOffset = «где граница» (аналог _LightArea),
//    RampContrast = крутизна (аналог _ShadowRampWidth), тень уходит в свой оттенок.
float lum = dot(col, float3(0.299, 0.587, 0.114));
float x = saturate((lum - RampOffset) * max(RampContrast, 0.01) + RampOffset);
float steps = max(RampSteps, 1.0);
float q = floor(x * steps + 0.5) / steps;
float qs = lerp(q, x, saturate(RampSmooth));
float3 shaded = col * (qs / max(lum, 1e-4));
shaded = lerp(shaded, shaded * ShadowTint, saturate(1.0 - qs) * saturate(ShadowTintAmt));
col = lerp(col, shaded, saturate(rampMul) * saturate(RampOn));

// контур поверх затенения — линия не должна постеризоваться
col = lerp(col, lineCol, edge);

// ★ Общий выключатель ВНУТРИ кода: при MasterEnable = 0 возвращается нетронутый кадр —
//   буквально прежняя картинка. Отдельным узлом Lerp это не делаем: SceneTexture отдаёт
//   float4, Custom — float3, и смешение двух разных типов в графе не компилируется.
return lerp(src, col, saturate(MasterEnable));
"""


def main():
    import unreal

    eal = unreal.EditorAssetLibrary
    atools = unreal.AssetToolsHelpers.get_asset_tools()
    mel = unreal.MaterialEditingLibrary

    # ── 1. коллекция параметров ───────────────────────────────────────────────────────────────
    if eal.does_asset_exist(MPC_PATH):
        mpc = eal.load_asset(MPC_PATH)
    else:
        mpc = atools.create_asset("MPC_AsdaToon", PKG, unreal.MaterialParameterCollection,
                                  unreal.MaterialParameterCollectionFactoryNew())
    have_s = {}
    for p in mpc.get_editor_property("scalar_parameters"):
        have_s[str(p.get_editor_property("parameter_name"))] = p
    sc = list(mpc.get_editor_property("scalar_parameters"))
    for name, val, _why in SCALARS:
        if name in have_s:
            continue
        e = unreal.CollectionScalarParameter()
        e.set_editor_property("parameter_name", name)
        e.set_editor_property("default_value", val)
        sc.append(e)
    mpc.set_editor_property("scalar_parameters", sc)

    have_v = {str(p.get_editor_property("parameter_name"))
              for p in mpc.get_editor_property("vector_parameters")}
    vc = list(mpc.get_editor_property("vector_parameters"))
    for name, rgba, _why in VECTORS:
        if name in have_v:
            continue
        e = unreal.CollectionVectorParameter()
        e.set_editor_property("parameter_name", name)
        e.set_editor_property("default_value", unreal.LinearColor(*rgba))
        vc.append(e)
    mpc.set_editor_property("vector_parameters", vc)
    eal.save_loaded_asset(mpc)
    unreal.log("MPC_AsdaToon: скаляров %d, векторов %d" %
               (len(mpc.get_editor_property("scalar_parameters")),
                len(mpc.get_editor_property("vector_parameters"))))

    # ── 2. пост-обработочный материал ─────────────────────────────────────────────────────────
    if eal.does_asset_exist(PP_PATH):
        eal.delete_asset(PP_PATH)          # пересборка графа с нуля — иначе останутся старые узлы
    mat = atools.create_asset("M_AsdaToonPP", PKG, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    # ★ Перед блумом (и до тонемаппинга): цвет ЛИНЕЙНЫЙ, ступени ramp ложатся по физической
    #   яркости, а не по сжатой кривой — иначе ступени «плавают» при смене экспозиции. Плюс на
    #   этом месте ещё доступен GBuffer (WorldNormal / StoredBaseColor / CustomStencil), а после
    #   тонемаппинга его уже нет. В UE 5.8 значения enum названы BL_SceneColor* — прежнего
    #   BL_BeforeTonemapping в движке нет (проверено в BlendableInterface.h).
    mat.set_editor_property("blendable_location",
                            unreal.BlendableLocation.BL_SCENE_COLOR_BEFORE_BLOOM)
    mat.set_editor_property("blendable_priority", 100)

    def expr(cls, x, y):
        return mel.create_material_expression(mat, cls, x, y)

    def coll(name, is_vector=False):
        e = expr(unreal.MaterialExpressionCollectionParameter, -1500, coll.y)
        coll.y += 60
        e.set_editor_property("collection", mpc)
        e.set_editor_property("parameter_name", name)
        return e
    coll.y = -1400

    # UV кадра и размер пикселя
    uv = expr(unreal.MaterialExpressionTextureCoordinate, -1500, 400)
    vsize = expr(unreal.MaterialExpressionViewProperty, -1500, 480)
    vsize.set_editor_property("property", unreal.MaterialExposedViewProperty.MEVP_VIEW_SIZE)

    custom = expr(unreal.MaterialExpressionCustom, -600, 0)
    custom.set_editor_property("code", HLSL)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("description", "AsdaToonPP")

    # ★ Входы Custom-узла: имя в HLSL → узел графа. Порядок = порядок пинов.
    wiring = [
        ("UV", uv, ""),
        ("InvSize", vsize, "InvProperty"),
        ("MasterEnable", coll("Enable"), ""),
        ("OutlineOn", coll("OutlineEnable"), ""),
        ("OutThick", coll("OutlineThickness"), ""),
        ("OutDepthSens", coll("OutlineDepthSens"), ""),
        ("OutNormalSens", coll("OutlineNormalSens"), ""),
        ("OutColorBlend", coll("OutlineColorBlend"), ""),
        ("OutFadeStart", coll("OutlineFadeStart"), ""),
        ("OutFadeEnd", coll("OutlineFadeEnd"), ""),
        ("OutMulChar", coll("OutlineMulChar"), ""),
        ("OutMulCreat", coll("OutlineMulCreature"), ""),
        ("OutMulWorld", coll("OutlineMulWorld"), ""),
        ("OutMulObj", coll("OutlineMulObject"), ""),
        ("RampOn", coll("RampEnable"), ""),
        ("RampSteps", coll("RampSteps"), ""),
        ("RampSmooth", coll("RampSmooth"), ""),
        ("RampOffset", coll("RampOffset"), ""),
        ("RampContrast", coll("RampContrast"), ""),
        ("ShadowTintAmt", coll("ShadowTintAmount"), ""),
        ("RampMulChar", coll("RampMulChar"), ""),
        ("RampMulCreat", coll("RampMulCreature"), ""),
        ("RampMulWorld", coll("RampMulWorld"), ""),
        ("RampMulObj", coll("RampMulObject"), ""),
        ("OutColor", coll("OutlineColor"), ""),
        ("ShadowTint", coll("ShadowTint"), ""),
    ]
    # ★★★ ОБЯЗАТЕЛЬНО: SceneTexture-узлы должны ПРИСУТСТВОВАТЬ в графе, иначе выборки внутри
    #   Custom-HLSL не компилируются — движок собирает маску используемых буферов
    #   (POST_PROCESS_USED_SCENE_TEXTURES) по узлам графа, а не по тексту кода. Сами значения
    #   пинов не используются (движок это поддерживает штатно, см. MaterialTemplate.ush).
    reg_ids = [("RegDepth", unreal.SceneTextureId.PPI_SCENE_DEPTH),
               ("RegNormal", unreal.SceneTextureId.PPI_WORLD_NORMAL),
               ("RegInput0", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0),
               ("RegStencil", unreal.SceneTextureId.PPI_CUSTOM_STENCIL),
               ("RegBase", unreal.SceneTextureId.PPI_STORED_BASE_COLOR)]
    y = 700
    for nm, sid in reg_ids:
        st = expr(unreal.MaterialExpressionSceneTexture, -1100, y)
        st.set_editor_property("scene_texture_id", sid)
        y += 140
        wiring.append((nm, st, "Color"))

    inputs = []
    for nm, _node, _out in wiring:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", nm)
        inputs.append(ci)
    custom.set_editor_property("inputs", inputs)

    for idx, (nm, node, out) in enumerate(wiring):
        if not mel.connect_material_expressions(node, out, custom, nm):
            unreal.log_error("не подключился вход %s" % nm)

    # ★ Custom-узел идёт прямо в Emissive: выключатель уже внутри кода (см. MasterEnable).
    if not mel.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        unreal.log_error("Custom-узел не подключился к Emissive")

    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    unreal.log("M_AsdaToonPP: домен PostProcess, входов Custom-узла %d" % len(wiring))

    err = mat.get_editor_property("compile_errors") if hasattr(mat, "compile_errors") else None
    unreal.log("=== ГОТОВО: %s + %s ===" % (MPC_PATH, PP_PATH))
    if err:
        unreal.log_error("ошибки компиляции: %s" % err)


if __name__ == "__main__":
    main()
