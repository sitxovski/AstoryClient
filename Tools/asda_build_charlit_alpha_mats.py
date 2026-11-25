# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_charlit_alpha_mats.py — 31.10.2026, волна acce_alpha. ДВА ВАРИАНТА мастера персонажа для частей,
#   которые оригинал рисует ПРОЗРАЧНЫМИ: M_AsdaCharLitBlend (translucent) и M_AsdaCharLitMask (masked).
#   Общий мастер M_AsdaCharLit НЕ МЕНЯЕТСЯ (30.10 правка общего мастера сняла текстуры со всей игры).
#
# ЭТАЛОН (разбор — Reports/acce_alpha/REPORT.md §1):
#   • подсетка .jbm с битом mr+0x004 & 0x1 в опак-проходе не рисуется (meshNodeRenderDispatch@0x111E110),
#     а уходит в отсортированную очередь g_RenderQueue (entityCollectNameplates@0x9D8050 для персонажа/частей);
#   • очередь рисуется в CRenderer_RenderWorldFrame@0x9773B0 (render_core.c:1418-1446: renderShadowMeshQueue /
#     clanShadowRenderQueue — имена врут, это обход g_RenderQueue: база 0x3537FA8, счётчик +0x13880 = 0x354B828,
#     проверено по машинному коду) ПРИ g_bUseEffectPath = 0 — мимо шейдера PShade, фиксированным конвейером:
#     стадия 0 ALPHAOP = MODULATE(TEXTURE, CURRENT) ⇒ альфа пикселя = Tex.A;
#   • состояние: ALPHABLENDENABLE=1, SRCALPHA/INVSRCALPHA (d3dApplyMaterialRenderState@0x111DA90 →
#     CRenderer_ApplyBlendRenderState@0x9764F0), ALPHATESTENABLE=1, ALPHAREF=8, ALPHAFUNC=GREATEREQUAL
#     (render_core.c:1425-1430), Z пишется, если нет бита 0x10 (rendApplyZWriteOnState@0x979910).
#   ⇒ «смешивание по Tex.A + отсечка a < 8/255 + запись глубины».
#
# ПОЧЕМУ ДВА МАСТЕРА (потолок UE, обход — память no-feature-loss-on-engine-limits): у UE нет режима
#   «смешивание + запись Z». Translucent даёт мягкий край, но глубину не пишет (крупная деталь сама себя
#   перекрывает в неверном порядке); Masked пишет глубину и режет по тому же порогу 8/255, но полутон
#   рисует непрозрачно. Выбор на пару «меш+текстура» — по данным (aa_uv_alpha.py → AsdaCharAlphaParts.inl):
#   мягкая альфа (вуали, наклейки) → Blend, жёсткая вырезка (листья, кружево, прорези) → Mask.
#
# ЧТО ДОБАВЛЯЕТСЯ ПОВЕРХ КОПИИ (граф M_AsdaCharLit копируется целиком: свет, блик Tex.A, рим, ambient-floor):
#   Blend: blend_mode=TRANSLUCENT, TLM_SURFACE_PER_PIXEL_LIGHTING (освещение как у Lit-меша, как «душа»),
#          two_sided=True (как у мастера: в эталоне типы 0x101/0x81 = CULLMODE NONE), Opacity =
#          ShadowReplace(Default = If(Tex.A >= 8/255, Tex.A, 0), Shadow = Tex.A), cast_dynamic_shadow_as_masked,
#          opacity_mask_clip_value = 8/255 (тень режется тем же альфа-тестом). +4 узла (Constant, If, Constant 0, ShadowReplace).
#   Mask:  blend_mode=MASKED, opacity_mask_clip_value = 8/255, OpacityMask = Tex.A. +0 узлов.
#
# Запуск (СКРЫТО, редактор закрыт):
#   python Tools/run_headless_safe.py Tools/asda_build_charlit_alpha_mats.py --marker ALPHA-MATS-FINISHED
import os, sys
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_mat_graph as MG                                        # контроль числа узлов/дублей
from asda_fix_charlit_lighting import WIRE_CHAR_EXPRESSIONS       # узлов мастера по генератору (нижняя граница)

mel = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
PKG = "/Game/Materials"
SRC = "%s/M_AsdaCharLit" % PKG                 # общий мастер персонажа — ТОЛЬКО читается
BLEND = "%s/M_AsdaCharLitBlend" % PKG          # translucent-вариант
MASK = "%s/M_AsdaCharLitMask" % PKG            # masked-вариант
ALPHA_REF = 8.0 / 255.0                        # эталон D3DRS_ALPHAREF = 8, ALPHAFUNC = GREATEREQUAL
BLEND_EXTRA_EXPRESSIONS = 4                    # Constant(ref), If, Constant(0), ShadowReplace
MASK_EXTRA_EXPRESSIONS = 0                     # только провод Tex.A → OpacityMask


def log(msg): unreal.log_warning("[alpha-mats] %s" % msg)


def E(m, cls, x, y):
    """Новый узел графа."""
    return mel.create_material_expression(m, cls, x, y)


def C(a, ao, b, bi):
    """Соединение с ПРОВЕРКОЙ (connect_material_expressions при неверном имени входа молча даёт False)."""
    if not mel.connect_material_expressions(a, ao, b, bi):
        raise RuntimeError("не соединилось: %s.%s -> %s.%s" % (a.get_name(), ao, b.get_name(), bi))


def duplicate(dst):
    """Свежая копия мастера в dst (старая копия удаляется) и её единственный сэмплер Tex."""
    if EAL.does_asset_exist(dst):
        if not EAL.delete_asset(dst):
            raise RuntimeError("не удалось удалить старый %s" % dst)
    if not EAL.duplicate_asset(SRC, dst):
        raise RuntimeError("duplicate_asset %s -> %s вернул None" % (SRC, dst))
    m = EAL.load_asset(dst)
    if not isinstance(m, unreal.Material):
        raise RuntimeError("%s не Material: %r" % (dst, m))
    texs = [e for e in mel.get_material_expressions(m)
            if isinstance(e, unreal.MaterialExpressionTextureSampleParameter2D)
            and str(e.get_editor_property("parameter_name")) == "Tex"]
    if len(texs) != 1:
        raise RuntimeError("в копии %s сэмплеров Tex: %d, ожидался 1" % (dst, len(texs)))
    return m, texs[0]


def finish(m, dst, expected, prop):
    """Контроль графа, компиляция, сохранение и печать итога ИЗ АССЕТА."""
    MG.check_graph(m, expected=expected, log=log, tag="[alpha-mats] " + dst)
    mel.recompile_material(m)
    m.modify()
    if not EAL.save_loaded_asset(m, False):
        raise RuntimeError("save_loaded_asset %s вернул False" % dst)
    node = mel.get_material_property_input_node(m, prop)
    log("%s: узлов %d, дубли %s; blend=%s two_sided=%s clip=%.5f shadow_as_masked=%s tlm=%s; вход %s <- %s" % (
        dst, MG.expression_count(m), MG.parameter_duplicates(m) or "нет", m.get_editor_property("blend_mode"),
        m.get_editor_property("two_sided"), m.get_editor_property("opacity_mask_clip_value"),
        m.get_editor_property("cast_dynamic_shadow_as_masked"), m.get_editor_property("translucency_lighting_mode"),
        prop, node.get_name() if node else "<не подключён>"))


def build_blend(base):
    """M_AsdaCharLitBlend: translucent, Opacity = Tex.A с эталонной отсечкой < 8/255."""
    m, tex = duplicate(BLEND)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    m.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("cast_dynamic_shadow_as_masked", True)
    m.set_editor_property("opacity_mask_clip_value", ALPHA_REF)
    m.set_editor_property("used_with_skeletal_mesh", True)
    ref = E(m, unreal.MaterialExpressionConstant, -900, 1150)
    ref.set_editor_property("r", ALPHA_REF)
    zero = E(m, unreal.MaterialExpressionConstant, -900, 1250)
    zero.set_editor_property("r", 0.0)
    cut = E(m, unreal.MaterialExpressionIf, -600, 1050)     # A>B → Tex.A, A==B → Tex.A, A<B → 0
    C(tex, "A", cut, "A")
    C(ref, "", cut, "B")
    C(tex, "A", cut, "A > B")
    C(tex, "A", cut, "A == B")
    C(zero, "", cut, "A < B")
    sw = E(m, unreal.MaterialExpressionShadowReplace, -350, 1050)
    C(cut, "", sw, "Default")
    C(tex, "A", sw, "Shadow")
    if not mel.connect_material_property(sw, "", unreal.MaterialProperty.MP_OPACITY):
        raise RuntimeError("не подключился вход Opacity")
    finish(m, BLEND, base + BLEND_EXTRA_EXPRESSIONS, unreal.MaterialProperty.MP_OPACITY)


def build_mask(base):
    """M_AsdaCharLitMask: masked, OpacityMask = Tex.A, порог 8/255."""
    m, tex = duplicate(MASK)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    m.set_editor_property("opacity_mask_clip_value", ALPHA_REF)
    m.set_editor_property("used_with_skeletal_mesh", True)
    if not mel.connect_material_property(tex, "A", unreal.MaterialProperty.MP_OPACITY_MASK):
        raise RuntimeError("не подключился вход OpacityMask")
    finish(m, MASK, base + MASK_EXTRA_EXPRESSIONS, unreal.MaterialProperty.MP_OPACITY_MASK)


def main():
    """Работа скрипта (импорт модуля ничего не делает — закон tools-no-import-side-effects)."""
    if not EAL.does_asset_exist(SRC):
        raise RuntimeError("нет мастера %s" % SRC)
    src = EAL.load_asset(SRC)
    log("мастер %s: узлов %d (ожидается %d), дубли %s" % (SRC, MG.expression_count(src), WIRE_CHAR_EXPRESSIONS,
                                                          MG.parameter_duplicates(src) or "нет"))
    # ★ База — ЖИВОЙ мастер, а не генератор: после asda_fix_charlit_lighting в него законно добавлялись узлы
    #   (31.10: +7 узлов HitFlinchTint/CPD[1] — подкраска реакции на удар), копия обязана их унаследовать.
    #   Поэтому число узлов берётся из ассета; проверяются только дубли имён параметров и нижняя граница.
    base = MG.expression_count(src)
    if base < WIRE_CHAR_EXPRESSIONS:
        raise RuntimeError("мастер беднее генератора: %d < %d узлов" % (base, WIRE_CHAR_EXPRESSIONS))
    MG.check_graph(src, expected=base, log=log, tag="[alpha-mats] мастер")
    build_blend(base)
    build_mask(base)
    log("ЗАВЕРШЕНО (ALPHA-MATS-FINISHED)")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
