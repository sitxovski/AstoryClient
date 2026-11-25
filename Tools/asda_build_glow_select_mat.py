# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_glow_select_mat.py — ★★★ ПОСТ-МАТЕРИАЛ «ВЫБОРОЧНОЕ СВЕЧЕНИЕ» `M_AsdaGlowSelect`
#   (19.09.2026, фаза 2 окна настроек 69500, решение владельца №1: пункты «Свечения» — КАК В ОРИГИНАЛЕ).
#
# ═══ ЗАЧЕМ ═════════════════════════════════════════════════════════════════════════════════════════
#   Пункт LEVEL OF GLOW оригинала выбирает, НА ЧЁМ светится (разбор эталона — у объявления
#   `UAsdaVideoSettingsSubsystem::SetGlowLevel`): 1 «Персонаж» — первая группа (девять списков кадра эталона:
#   игрок и привязанное к нему — экипировка, питомец, хранитель, «душа», флаги клана, стрелы, гача; застройки,
#   рельефа, воды, неба, NPC и лута в ней НЕТ), 2 «Монстр» — только мобы (`sub_9256E3(g_pMobMgr)`), 3 — обе
#   группы, 4 — кадр. Штатный блум UE маски не имеет, поэтому пункты 1/2/3 собирает этот материал.
#   ★ 19.09.2026 (ревизия К1): маска ОБРАТНАЯ — группа = пиксели с трафаретом группы (CharStencil 10 / MobStencil
#   11), их ставит C++ только акторам групп; всё непомеченное в группу не входит. Режим 4 — весь кадр без маски:
#   сцена логина (второй путь кадра эталона `CRenderer_RenderFrame@0x975EF0`, пункты 1/3).
#
# ═══ ★★★ ФОРМУЛА — ЭТАЛОН ДОСЛОВНО (пересборка 19.09.2026, _backup/2026-09-19_glow_etalon) ═══════════
#   Прежняя версия (ореол только с пикселей ярче порога 2.5, добавка в линейном HDR) эталону НЕ отвечала:
#   у мобов нет ни одного пикселя ярче порога — пункт 2 не светился. Эталон (адреса — сборка IDB):
#   • `sceneRenderShadowLightPass@0xF5FCC0`: буфер свечения G (A8R8G8B8, во весь экран) очищается в
#     0x00FFFFFF (БЕЛЫЙ, альфа 0), и в него рисуется выбранная группа ЦЕЛИКОМ — теми же вызовами, что и
#     в обычном кадре, с глубиной основного кадра; ПОРОГА/ЯРКОСТНОЙ ВЫБОРКИ НЕТ. Обычный проход эту
#     группу на этих пунктах НЕ рисует (`mapDragRotateOnInput@0xF5FBA0` — мобы только при 0/1/4,
#     `sceneRenderShadowFogPasses@0xF5FA80` — первая группа только при 0/2/4).
#   • `effectSetupRenderStateAndDraw@0xF5EFF0`: два прохода размытия в буферы 256×256 (512×512 при ширине
#     экрана ≥ 1600, `d3dTargets_CreateScreenSurfaces@0xF5D280`): по 4 копии со сдвигом ±Offset текселя
#     по X и Y, точечная выборка, сложение ONE/ONE, каждая копия × TFACTOR = BlurFactor/255 (8 бит — насыщение 1).
#   • `effectQuadRender@0xF5F440`: ступень 0 = MODULATE2X(размытое, ComposeFactor/255), ступень 1 =
#     MODULATE2X(G, ступень 0), альфа = альфа G; смешение SRCALPHA/INVSRCALPHA ⇒ результат ложится ТОЛЬКО
#     на пиксели группы (снаружи альфа G = 0), ореола за контуром нет.
#   • Числа (данные .data, пишет их только чат-команда «+GLOW ALL|UNIT f d d» `effectQuadSetAnchorTag@0xF5E910`):
#     ALL (пункт 4) = UNIT (пункты 1..3) = 0.65 / 90 / 70; буфер 256 @0x15B5E40/44.
#   Итог на пиксель группы: out = min(1, 2·G·min(1, 2·(70/255)·B2)), B2/B1 — насыщаемые суммы 4 копий ×90/255.
#   Формула 8-битная, в гамма-пространстве кадра — поэтому материал стоит ПОСЛЕ тонмаппера (там кадр уже
#   в пространстве экрана 0..1, а предэкспозиция на выход не накладывается — PostProcessMaterialShaders.usf).
#   Приближение порта: B2 считается в самой точке пикселя, а не в узлах сетки 256² с билинейным растяжением.
#   ★ 19.09.2026 (glow_fix6): вес пикселя группы = fog (в проходе G смешения нет — С1; полупрозрачное тело в группу
#   не входит — К1, разбор в AsdaVideoSettingsSubsystem.cpp/.h). [ПОРТ] (С2): эталон сводит lerp(фон за группой, o,
#   fog) — группа на 1/3 обычным проходом не рисуется; порт — lerp(кадр с телом, o, fog). Поведение оставлено.
#
# ═══ ПАРАМЕТРЫ (ставит C++ из [AsdaGlow] DefaultScalability.ini) ═══════════════════════════════════
#   BlurOffset / BlurFactor / ComposeFactor — числа эталона (UNIT), BufSize / BufSizeWide / WideFromWidth —
#   сторона буфера размытия и порог ширины, MaskMode — 1 «первая группа» / 2 «мобы» / 3 «обе» / 4 «весь кадр»,
#   MobStencil — трафарет мобов (11), CharStencil — трафарет первой группы (10).
#
# Запуск (редактор ЗАКРЫТ): python Tools\run_headless_safe.py Tools\asda_build_glow_select_mat.py
#                             --marker "СВЕЧЕНИЕ-МАТЕРИАЛ: ГОТОВО"
#   Уже собранный материал НЕ пересобирается (его могли подстроить в редакторе); пересборка — set ASDA_GLOWSEL_REBUILD=1.
#   ★ 19.09.2026 (glow_fix4): без флага — ТОЧЕЧНОЕ обновление: текст Custom-узла «AsdaGlowSelect» и его входы
#   (недостающие параметры дописываются, существующие узлы и их значения не трогаются); актуальный — без изменений.
#   ★ run_headless_safe.py поднимает редактор на НАСТОЯЩЕМ профиле игрока; пересборку 19.09.2026 гоняли
#     коммандлетом -run=pythonscript -AllowCommandletRendering с -GameUserSettingsINI/-AsdaShortcutkeyIni на копиях.
import os
import traceback

PKG = "/Game/Materials"
NAME = "M_AsdaGlowSelect"
PATH = PKG + "/" + NAME

# ── HLSL: координаты — вьюпортные (GetViewportUV), в буферы переводит движок (ViewportUVToSceneTextureUV),
#    поэтому выборка верна и при масштабе рендера < 100 % (у входа и трафарета разные буферы).
#    Индексы SceneTexture — MaterialSceneTextureId.h: 1 SceneDepth, 13 CustomDepth, 14 PostProcessInput0,
#    25 CustomStencil. После тонмаппера предэкспозиции нет ни на входе, ни на выходе — деления нет.
#    ★ ПРИНАДЛЕЖНОСТЬ ГРУППЕ = трафарет + глубина: CustomStencil хранит ближайший ПОМЕЧЕННЫЙ меш, даже если
#    его заслоняет непомеченный (дерево перед мобом). Эталон рисует группу с глубиной основного кадра —
#    заслонённое в G не попадает; здесь то же даёт сравнение CustomDepth ≤ SceneDepth.
#    ★ 19.09.2026: небо, рельеф, застройка и прочее непомеченное в группу не входят сами (маска обратная) —
#    прежние признаки «трафарет мира 12» и «глубина неба ≥ 10 км» сняты.
#    ASDA_SEL — макрос, а не функция: Custom-узел вставляется в тело функции материала.
HLSL = r"""
// ★ 19.09.2026 (glow_fix6, К1): полос α больше нет — полупрозрачное (α < 1) тело в группу не входит (эталон не
//   рисует его в буфер свечения: бит 2 `подмеш+4`, `meshNodeRenderDispatch@0x111E110`), C++ его не помечает.
//   ASDA_SEL: 1 — пиксель группы (CharStencil/MobStencil по режиму, виден), 0 — вне группы. Режим 4 — весь кадр.
#define ASDA_SEL(UV, OUTA) { \
    float2 asdaSuv = ViewportUVToSceneTextureUV(UV, 1); \
    float asdaSd = SceneTextureLookup(asdaSuv, 1, false).r; \
    float asdaCd = SceneTextureLookup(asdaSuv, 13, false).r; \
    float asdaSt = SceneTextureLookup(asdaSuv, 25, false).r; \
    bool asdaVis = asdaCd <= asdaSd * 1.001 + 1.0; \
    float asdaCa = (abs(asdaSt - CharStencil) < 0.5) ? 1.0 : 0.0; \
    float asdaMa = (abs(asdaSt - MobStencil) < 0.5) ? 1.0 : 0.0; \
    float asdaA = (Mode < 1.5) ? asdaCa : ((Mode < 2.5) ? asdaMa : max(asdaCa, asdaMa)); \
    OUTA = (Mode > 3.5) ? 1.0 : (asdaVis ? asdaA : 0.0); }
float2 vuv = GetViewportUV(Parameters);
float3 src = SceneTextureLookup(ViewportUVToSceneTextureUV(vuv, 14), 14, false).rgb;
float aC = 0.0;
ASDA_SEL(vuv, aC)
// Пиксель вне группы: альфа G = 0, эталонное смешение оставляет кадр как есть.
if (aC <= 0.0) { return src; }
// ★ 19.09.2026 (glow_fix5, С3; glow_fix6): туман-альфа карты — альфа G = fog (PShade EmptyProject.fx:828 при α = 1), fog вершинного шейдера
//   .fx (EmptyProject.fx:634-645): saturate(Near + (Far − Near)·max(0, d² − Start²)/(End² − Start²)), d — расстояние
//   камера–точка тела (глубина вида пикселя группы × длина луча при z = 1). Числа — .SCE карты (C++ из реестра карт).
float2 asdaNdc = vuv * 2.0 - 1.0;
float3 asdaRay = float3(asdaNdc.x / View.ViewToClip[0][0], asdaNdc.y / View.ViewToClip[1][1], 1.0);
float asdaD = SceneTextureLookup(ViewportUVToSceneTextureUV(vuv, 13), 13, false).r * length(asdaRay);
float asdaS2 = FogStart * FogStart;
float asdaE2 = max(FogEnd * FogEnd, asdaS2 + 1.0);
float fogA = saturate(FogNear + (FogFar - FogNear) * max(0.0, asdaD * asdaD - asdaS2) / (asdaE2 - asdaS2));
// Сторона буфера размытия (эталон: 256, при ширине экрана ≥ 1600 — вдвое), шаг — один его тексель.
float bufN = ((1.0 / max(InvSize.x, 1e-8)) >= WideFromWidth) ? BufSizeWide : BufSize;
float2 t = float2(1.0, 1.0) / max(bufN, 1.0);
float k1 = BlurFactor / 255.0;
float k2 = ComposeFactor / 255.0;
float3 b2 = 0;
[unroll] for (int i = 0; i < 4; ++i)
{
    // Второй проход: 4 копии B1 со сдвигом ±Offset текселя; точечная выборка при Offset > 0.5 берёт соседний тексель.
    float2 di = float2((i == 0) ? 1.0 : ((i == 1) ? -1.0 : 0.0), (i == 2) ? 1.0 : ((i == 3) ? -1.0 : 0.0));
    float2 q = vuv + di * ((BlurOffset > 0.5) ? 1.0 : 0.0) * t;
    float3 b1 = 0;
    [unroll] for (int j = 0; j < 4; ++j)
    {
        // Первый проход: 4 копии G со сдвигом ±Offset текселя буфера; вне группы G — белый (очистка 0x00FFFFFF),
        //   тело группы — его цвет (★ glow_fix5/6: тело рисуется в G БЕЗ смешения — С1, белое не подмешивается).
        float2 dj = float2((j == 0) ? 1.0 : ((j == 1) ? -1.0 : 0.0), (j == 2) ? 1.0 : ((j == 3) ? -1.0 : 0.0));
        float2 x = q + dj * BlurOffset * t;
        float aX = 0.0;
        ASDA_SEL(x, aX)
        b1 += (aX > 0.0) ? SceneTextureLookup(ViewportUVToSceneTextureUV(x, 14), 14, false).rgb : float3(1.0, 1.0, 1.0);
    }
    b2 += min(b1 * k1, 1.0);   // 8-битный буфер B1 насыщается на 1
}
b2 = min(b2 * k1, 1.0);        // 8-битный буфер B2
float3 s0 = min(2.0 * k2 * b2, 1.0);   // ступень 0: MODULATE2X(B2, TFACTOR)
float3 g = src;                                     // G пикселя группы: цвет тела (без смешения — С1)
float3 o = min(2.0 * g * s0, 1.0);                  // ступень 1: MODULATE2X(G, ступень 0)
// Смешение SRCALPHA/INVSRCALPHA по альфе G = fog (★ glow_fix6: С1 — в G смешения нет, вес линейный, не fog²).
// [ПОРТ] (С2, поведение не меняется): эталон на пунктах 1/3 группу обычным проходом не рисует и сводит
//   lerp(ФОН за группой, o, fog); здесь src — кадр С ТЕЛОМ, т.е. lerp(кадр с телом, o, fog): при fog < 1 вместо
//   фона просвечивает само тело. Пост-материал видит один кадр — убрать тело только на пунктах 1/3 нечем.
return lerp(src, o, aC * fogA);
"""


def log(m):
    import unreal
    unreal.log("[свечение-материал] %s" % m)


# Скалярные входы Custom-узла: (имя входа, имя параметра, значение по умолчанию, Y узла в графе).
#   Значения по умолчанию — числа эталона (C++ всё равно ставит их из [AsdaGlow] и своих констант).
SCALARS = (
    ("BlurOffset", "BlurOffset", 0.65, 0),
    ("BlurFactor", "BlurFactor", 90.0, 80),
    ("ComposeFactor", "ComposeFactor", 70.0, 160),
    ("BufSize", "BufSize", 256.0, 240),
    ("BufSizeWide", "BufSizeWide", 512.0, 320),
    ("WideFromWidth", "WideFromWidth", 1600.0, 400),
    ("Mode", "MaskMode", 2.0, 480),
    ("MobStencil", "MobStencil", 11.0, 560),
    ("CharStencil", "CharStencil", 10.0, 640),        # ★ 19.09.2026: первая группа (маска обратная)
    # ★ 19.09.2026 (glow_fix5, С3): туман-альфа карты (.SCE, C++ из AsdaMapRegistry::kSceFogs); умолчания — без тумана.
    ("FogStart", "FogStart", 0.0, 960),
    ("FogEnd", "FogEnd", 1.0, 1040),
    ("FogNear", "FogNear", 1.0, 1120),
    ("FogFar", "FogFar", 1.0, 1200),
)
# ★ 19.09.2026 (glow_fix6): параметры полос α (glow_fix4), снятые с Custom-узла, — точечное обновление удаляет их
#   узлы из графа ПО ИМЕНИ, по одному (`delete_material_expression`; массовое удаление не зовётся — баг UE 5.8).
RETIRED = ("CharAlphaBase", "MobAlphaBase", "AlphaSteps")
# ★ SceneTexture-узлы ОБЯЗАНЫ присутствовать в графе: маску используемых буферов
#   (POST_PROCESS_USED_SCENE_TEXTURES) движок собирает по узлам, а не по тексту HLSL (как в M_AsdaToonPP).
SCENE_TEX = (("RegInput0", "PPI_POST_PROCESS_INPUT0"), ("RegStencil", "PPI_CUSTOM_STENCIL"),
             ("RegCustomDepth", "PPI_CUSTOM_DEPTH"), ("RegSceneDepth", "PPI_SCENE_DEPTH"))


def wire(mat, custom, reuse):
    """Входы Custom-узла и их источники. reuse=True — существующие узлы-параметры/буферы графа берутся как есть
    (их значения, в т.ч. подстроенные владельцем, не трогаются), недостающие создаются."""
    import unreal
    mel = unreal.MaterialEditingLibrary
    have_s, have_t, have_v = {}, {}, None
    if reuse:
        for e in mel.get_material_expressions(mat):
            if isinstance(e, unreal.MaterialExpressionScalarParameter):
                have_s[str(e.get_editor_property("parameter_name"))] = e
            elif isinstance(e, unreal.MaterialExpressionSceneTexture):
                have_t[str(e.get_editor_property("scene_texture_id"))] = e
            elif isinstance(e, unreal.MaterialExpressionViewProperty):
                have_v = e
    created = []
    if have_v is None:
        have_v = mel.create_material_expression(mat, unreal.MaterialExpressionViewProperty, -1300, -200)
        have_v.set_editor_property("property", unreal.MaterialExposedViewProperty.MEVP_VIEW_SIZE)
        created.append("ViewSize")
    wiring = [("InvSize", have_v, "InvProperty")]
    for inp, pname, val, y in SCALARS:
        e = have_s.get(pname)
        if e is None:
            e = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -1300, y)
            e.set_editor_property("parameter_name", pname)
            e.set_editor_property("default_value", val)
            created.append(pname)
        wiring.append((inp, e, ""))
    y = 980
    for nm, sid_name in SCENE_TEX:
        sid = getattr(unreal.SceneTextureId, sid_name)
        e = have_t.get(str(sid))
        if e is None:
            e = mel.create_material_expression(mat, unreal.MaterialExpressionSceneTexture, -1100, y)
            e.set_editor_property("scene_texture_id", sid)
            created.append(nm)
        y += 140
        wiring.append((nm, e, "Color"))
    inputs = []
    for nm, _n, _o in wiring:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", nm)
        inputs.append(ci)
    custom.set_editor_property("inputs", inputs)
    for nm, node, out in wiring:
        if not mel.connect_material_expressions(node, out, custom, nm):
            raise RuntimeError("не подключился вход %s" % nm)
    return wiring, created


def main():
    import unreal
    eal = unreal.EditorAssetLibrary
    atools = unreal.AssetToolsHelpers.get_asset_tools()
    mel = unreal.MaterialEditingLibrary

    if eal.does_asset_exist(PATH) and not os.environ.get("ASDA_GLOWSEL_REBUILD"):
        # ★ 19.09.2026 (glow_fix4): ТОЧЕЧНОЕ обновление вместо пересборки — меняется только текст Custom-узла
        #   «AsdaGlowSelect» и список его входов (недостающие параметры дописываются); прочие узлы, их значения и
        #   свойства материала (домен, место в цепочке, приоритет) не трогаются. Повторный запуск ничего не меняет.
        mat = eal.load_asset(PATH)
        custom = None
        for e in mel.get_material_expressions(mat):
            if isinstance(e, unreal.MaterialExpressionCustom) and str(e.get_editor_property("description")) == "AsdaGlowSelect":
                custom = e
        if custom is None:
            raise RuntimeError("в %s нет Custom-узла AsdaGlowSelect — точечно обновить нечего (пересборка: ASDA_GLOWSEL_REBUILD=1)" % PATH)
        names_now = [str(ci.get_editor_property("input_name")) for ci in custom.get_editor_property("inputs")]
        names_want = ["InvSize"] + [x[0] for x in SCALARS] + [x[0] for x in SCENE_TEX]
        retired = [e for e in mel.get_material_expressions(mat)
                   if isinstance(e, unreal.MaterialExpressionScalarParameter) and str(e.get_editor_property("parameter_name")) in RETIRED]
        if str(custom.get_editor_property("code")) == HLSL and names_now == names_want and not retired:
            log("%s: Custom-узел уже актуален (входов %d) — без изменений" % (PATH, len(names_now)))
            log("СВЕЧЕНИЕ-МАТЕРИАЛ: ГОТОВО — без изменений")
            return
        custom.set_editor_property("code", HLSL)
        wiring, created = wire(mat, custom, reuse=True)
        gone = []   # ★ glow_fix6: удалённые узлы снятых параметров (входы Custom-узла уже переподключены wire())
        for e in retired:
            gone.append(str(e.get_editor_property("parameter_name")))
            mel.delete_material_expression(mat, e)
        if gone:
            log("%s: удалены узлы снятых параметров: %s" % (PATH, ", ".join(gone)))
        mel.recompile_material(mat)
        if not eal.save_loaded_asset(mat, False):
            raise RuntimeError("не сохранён %s" % PATH)
        log("%s: точечно обновлён Custom-узел (входов было %d, стало %d; новых узлов: %s)" % (
            PATH, len(names_now), len(wiring), ", ".join(created) or "нет"))
        log("СВЕЧЕНИЕ-МАТЕРИАЛ: ГОТОВО — обновлён")
        return
    if eal.does_asset_exist(PATH):
        eal.delete_asset(PATH)   # явная пересборка по флагу — граф с нуля
    mat = atools.create_asset(NAME, PKG, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    # ★ 19.09.2026: ПОСЛЕ тонмаппера — формула эталона 8-битная, в пространстве экрана (разбор в шапке).
    mat.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
    mat.set_editor_property("blendable_priority", 50)
    custom = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -600, 0)
    custom.set_editor_property("code", HLSL)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("description", "AsdaGlowSelect")
    wiring, _created = wire(mat, custom, reuse=False)
    if not mel.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError("Custom-узел не подключился к Emissive")
    mel.recompile_material(mat)
    if not eal.save_loaded_asset(mat, False):
        raise RuntimeError("не сохранён %s" % PATH)
    log("%s: домен PostProcess, после тонмаппера, входов Custom-узла %d" % (PATH, len(wiring)))
    log("СВЕЧЕНИЕ-МАТЕРИАЛ: ГОТОВО — собран")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    try:
        main()
    except Exception:
        import unreal
        unreal.log_error("[свечение-материал] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
