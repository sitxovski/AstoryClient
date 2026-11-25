# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_blend_material.py — МАСТЕР ПОЛУПРОЗРАЧНЫХ ПОДСЕТОК `M_AsdaObjectBlendLit`.
#
# ★★★ ЧТО ВОСПРОИЗВОДИМ (эталон, `Reports/jbm_blend_flag_study.md` §1.2-1.3).
#   Меш с `mr+0x004 & (0x1|0x2)` НЕ рисуется в непрозрачном проходе
#   (`meshNodeRenderDispatch@0x111E110`, model_anim.c:25098), а кладётся в очередь `g_RenderQueue`,
#   отсортированную по d² до камеры (`CRenderQueue_AddObject@0x1143120`). Очередь сливается ОДИН
#   раз в конце кадра — `CRenderer_RenderFrame@0x975EF0` (render_core.c:820-827):
#       SetRenderState(ALPHABLENDENABLE, 1);
#       SetRenderState(SRCBLEND, 5);     // D3DBLEND_SRCALPHA
#       SetRenderState(DESTBLEND, 6);    // D3DBLEND_INVSRCALPHA
#       SetRenderState(ALPHATESTENABLE, 1);
#       SetRenderState(ALPHAREF, 8);     // ← порог 8/255 = 0.0314, а НЕ наш обычный clip 0.5
#       SetRenderState(ALPHAFUNC, 7);    // D3DCMP_GREATEREQUAL
#       CRenderQueue_Flush(&g_RenderQueue, ...);
#   ГЛУБИНА ПРИ ЭТОМ ПИШЕТСЯ: `CRenderer_ApplyAlphaFogRenderState@0x9766A0` оставляет
#   ZENABLE=1/ZWRITEENABLE=1; выключает запись только бит 0x10 через `rendApplyZWriteOnState@0x979910`
#   (имя врёт — функция ставит ZWRITEENABLE=0). Из 109+687+48 мешей наших групп бит 0x10 не стоит НИ У
#   ОДНОГО. Тень помеченные меши отбрасывают как обычные: теневой проход обходит отсев по флагу
#   (`g_bRenderShadowPass || g_bForceShadowRender` в том же условии).
#   Сама альфа: `EmptyProject.fx`, PShadeNoSkin() → `w = Tex.w * (fog*2) * (NoSkinTexScroll.w*0.5)`,
#   где `NoSkinTexScroll.w` = глобальный фейд объекта (c92.w, model_anim.c:24608) ⇒
#       ★ АЛЬФА = АЛЬФА ТЕКСТУРЫ × ФЕЙД ОБЪЕКТА. Отдельного числа на меш в данных НЕТ.
#
# ★★★ ПОЧЕМУ Masked+Dither, А НЕ Translucent (выбор сделан ЗАМЕРОМ, отчёт
#   `Reports/blend_rollout_maps_items.md` §3).
#   В UE deferred-рендере транслюцентная геометрия В ПРИНЦИПЕ не пишет глубину сцены: в
#   `Material.h` (UE 5.8) для BLEND_Translucent есть только «Output Depth and Velocity»
#   (`bOutputTranslucentVelocity`, пишет ВЕЛОСИТИ) и `bAllowTranslucentCustomDepthWrites`
#   (это буфер CustomDepth, не сцена). То есть требование «запись глубины» Translucent-режимом
#   недостижимо, а вместе с глубиной теряются тени и корректная сортировка 667 мешей задника.
#   Чистый Masked с порогом 8/255 даёт глубину/тени/сортировку, но рисует НЕПРОЗРАЧНО пиксели с
#   частичной альфой; замер по нашим текстурам: таких пикселей 15.8 % (декор), 20.2 % (сцена
#   создания), 27.0 % (предметы), а на отдельных мешах — до 100 % (`cre_creation01-a_03` = альфа 51
#   РОВНО, `itm1_n_common_00` = 62…206, `mdel_buryd-c_00` = 91 %). Терять их нельзя.
#   UE закрывает ровно этот разрыв флагом материала `DitherOpacityMask` (Material.h:571):
#       «Dither opacity mask. When combined with Temporal AA this can be used as a form of
#        limited translucency which supports all lighting features.»
#   Реализация — `MaterialTemplate.ush:4570`: `clip(GetDitheredCoverage(SvPosition, FrameIndex,
#   GetMaterialMask(...)))`, где `GetMaterialMask = OpacityMask − OpacityMaskClipValue` (стр. 3909).
#   Пиксель выживает с вероятностью, равной покрытию ⇒ после TAA получается настоящее смешивание
#   SRCALPHA/INVSRCALPHA, при этом остаются глубина, тени и непрозрачный порядок рисования.
#   Проект уже на TAA: `Config/DefaultEngine.ini` → `r.AntiAliasingMethod=2`.
#
# ★ ТОЧНОСТЬ ПОРОГА. Порог оригинала ALPHAREF=8 GREATEREQUAL воспроизводим НЕ через
#   OpacityMaskClipValue (тогда покрытие уехало бы на −8/255 у всех пикселей), а явной ступенькой:
#       OpacityMask = Tex.A × FadeIn × ceil(saturate((Tex.A − AlphaRef) × 255)),  AlphaRef = 7.5/255
#   при OpacityMaskClipValue = 0. Итог: Tex.A < 8/255 → маска 0 → `clip` отбрасывает пиксель (это и
#   есть alpha-test оригинала); Tex.A ≥ 8/255 → покрытие РОВНО Tex.A×FadeIn (= альфа оригинала).
#   Про 7.5 вместо 8 — см. комментарий у константы ALPHA_REF (точное «≥8» для 8-битной альфы).
#
# ★ ОСТАЛЬНОЕ — 1:1 с `M_AsdaObjectLit` (asda_build_materials_lit.build_object + asda_add_ambient_floor
#   + asda_add_fadein_param), чтобы светотень помеченной подсетки не отличалась от соседней:
#   Default Lit, two-sided, BaseColor = Tex.RGB × VertexColor, Roughness/Metallic-скаляры,
#   Emissive = BaseColor × AmbientFloor(0.25), параметр текстуры зовётся `Tex` (это имя пишет рантайм —
#   `AsdaLootActor.cpp:203` подменяет текстуру дропа через MID по имени `Tex`).
#
# Запуск: UnrealEditor-Cmd.exe <proj> -ExecutePythonScript=asda_build_blend_material.py -NullRHI ...
import unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_material_policy as POL

MASTER_PATH = POL.MASTER_BLEND
PKG, NAME = MASTER_PATH.rsplit("/", 1)

# ★ Эталон: `ALPHAREF = 8`, `ALPHAFUNC = D3DCMP_GREATEREQUAL` — проходит альфа ≥ 8 из 255.
#   Альфа текстур 8-битная, поэтому «≥ 8» точно эквивалентно «> 7.5», а строгое сравнение выражается
#   в графе одной ступенькой без краевого случая на самом значении 8 (при пороге ровно 8/255
#   `ceil(saturate(0))` дал бы 0 и уровень 8 ошибочно вырезался бы).
ALPHA_REF = 7.5 / 255.0
ROUGH = 0.9               # как у M_AsdaObjectLit (asda_build_materials_lit.ROUGH)
AMBIENT_FLOOR = 0.25      # как у M_AsdaObjectLit (asda_add_ambient_floor.FLOOR)

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary


def log(m):
    unreal.log("[blend-мастер] " + str(m))


def C(a, ao, b, bi):
    """★ Закон ue-material-script-gotchas: connect_material_expressions МОЛЧА возвращает False
    при неверном имени входа — исключения нет, часть графа тихо не собирается. Поэтому проверяем
    возврат и, для одновходовых узлов (Saturate/Ceil), перебираем известные написания имени входа.
    """
    ins = [bi] if bi else ["", "Input"]
    # ★ У `MaterialExpressionVertexColor` выхода «RGB» НЕТ (замер 2026-08-18: connect вернул False);
    #   его выходы — «» (полный RGBA) и покомпонентные R/G/B/A. Поэтому имя выхода тоже перебираем.
    outs = [ao] if ao != "RGB" else ["RGB", ""]
    for o in outs:
        for n in ins:
            if MEL.connect_material_expressions(a, o, b, n):
                return
    raise RuntimeError("не соединилось: %s[%s] -> %s[%s]" % (a, ao, b, bi))


def E(m, cls, x, y):
    return MEL.create_material_expression(m, cls, x, y)


import asda_mat_graph as _MG   # noqa: E402  ★ дедуп №2 Т19: общий узел-скаляр
scal = _MG.scalar_param        # ScalarParameter nm=v в (x, y) графа m


def build():
    if EAL.does_asset_exist(MASTER_PATH):
        # Пересоздание обнуляет parent у MIC-ов, ссылающихся на мастер, поэтому пересобираем ВСЕГДА
        # вместе с прогоном переподвязки (asda_reimport_blend_groups.py вызывает его следом).
        EAL.delete_asset(MASTER_PATH)
    m = ATOOLS.create_asset(NAME, PKG, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    # ★ Порог держит СТУПЕНЬКА в графе (см. шапку), поэтому сам clip-value = 0.
    m.set_editor_property("opacity_mask_clip_value", 0.0)
    m.set_editor_property("dither_opacity_mask", True)      # UPROPERTY DitherOpacityMask (Material.h:571)
    m.set_editor_property("two_sided", True)                # meshType&1 = CULLMODE_NONE у 96 % помеченных
    m.set_editor_property("used_with_skeletal_mesh", True)  # декор карт и шкатулка — скелеталки
    m.set_editor_property("used_with_instanced_static_meshes", True)  # карты кладут пропы в ISM

    tex = E(m, unreal.MaterialExpressionTextureSampleParameter2D, -900, -100)
    tex.set_editor_property("parameter_name", "Tex")
    tex.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)

    vc = E(m, unreal.MaterialExpressionVertexColor, -900, 150)
    bc = E(m, unreal.MaterialExpressionMultiply, -560, 0)
    C(tex, "RGB", bc, "A"); C(vc, "RGB", bc, "B")           # BaseColor = tex · vtxColor (как M_AsdaObjectLit)

    rough = scal(m, "Roughness", ROUGH, -560, 220)
    metal = scal(m, "Metallic", 0.0, -560, 340)
    floor = scal(m, "AmbientFloor", AMBIENT_FLOOR, -560, 460)
    em = E(m, unreal.MaterialExpressionMultiply, -300, 400)
    C(bc, "", em, "A"); C(floor, "", em, "B")               # Emissive = BaseColor · AmbientFloor

    # --- альфа: Tex.A × FadeIn × ступенька alpha-test(8/255) ---
    fade = scal(m, "FadeIn", 1.0, -900, 600)                # тот же параметр, что у M_AsdaObjectLit
    a_fade = E(m, unreal.MaterialExpressionMultiply, -640, 640)
    C(tex, "A", a_fade, "A"); C(fade, "", a_fade, "B")      # = альфа текстуры × фейд объекта (PShadeNoSkin)

    ref = scal(m, "AlphaRef", ALPHA_REF, -900, 800)         # эталонный ALPHAREF=8/255, редактируем
    sub = E(m, unreal.MaterialExpressionSubtract, -640, 820)
    C(tex, "A", sub, "A"); C(ref, "", sub, "B")             # Tex.A − 8/255
    big = E(m, unreal.MaterialExpressionConstant, -640, 960)
    big.set_editor_property("r", 255.0)
    amp = E(m, unreal.MaterialExpressionMultiply, -440, 880)
    C(sub, "", amp, "A"); C(big, "", amp, "B")
    sat = E(m, unreal.MaterialExpressionSaturate, -280, 880)
    C(amp, "", sat, "")
    step = E(m, unreal.MaterialExpressionCeil, -140, 880)   # 0 при Tex.A < 8/255, иначе 1
    C(sat, "", step, "")
    mask = E(m, unreal.MaterialExpressionMultiply, 40, 700)
    C(a_fade, "", mask, "A"); C(step, "", mask, "B")

    MEL.connect_material_property(bc, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
    MEL.connect_material_property(em, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(mask, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    log("готов %s (dither=%s, clip=%.4f, AlphaRef=%.5f)"
        % (MASTER_PATH, m.get_editor_property("dither_opacity_mask"),
           m.get_editor_property("opacity_mask_clip_value"), ALPHA_REF))
    return m


def main():
    build()


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
