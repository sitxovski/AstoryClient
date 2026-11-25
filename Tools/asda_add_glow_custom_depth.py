# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_add_glow_custom_depth.py — ★★★ 19.09.2026 (ревизия «Свечения» С1/С2): прозрачные материалы группы
#   свечения пишут трафарет (CustomDepth/CustomStencil) сами.
# ★★★ 19.09.2026 (glow_fix6): НЕ ИСПОЛЬЗУЕТСЯ — TARGETS пуст, правки откачены (причина — у TARGETS ниже).
#
# ═══ ЗАЧЕМ ═════════════════════════════════════════════════════════════════════════════════════════
#   Пункты «Свечения» 1/3 выбирают первую группу эталона по трафарету 10 (UAsdaVideoSettingsSubsystem,
#   M_AsdaGlowSelect). В эталоне в той же группе (список персонажа 0x29080FC) лежит полупрозрачный игрок — «душа»
#   и появление. ★ 19.09.2026 (glow_fix5, С2): эффекты предметов порта (свечение заточки, крылья, кольцо соулмейта)
#   в группу эталона НЕ входят — очередь 0x3537FA8 рисуется после сведения (разбор у `AAsdaEffectActor::bGlowWithCarrier`);
#   прежние ссылки `itemSetupSowelGlow@0x9ED64F` (полосы «совела», в порту их нет) и «загрузчик .jeb @0x9EDF51» (это
#   загрузчик модели груди .jbm, функция 0x9EDE30) были ошибкой. Правка мастеров эффектов ниже оставлена: она ничего не
#   стоит, пока эффекту не ставят трафарет (сейчас — никому).
#   В UE прозрачный материал в проход CustomDepth НЕ идёт вовсе, пока у него не взведён флаг
#   «Allow Custom Depth Writes» (`AllowTranslucentCustomDepthWrites`, Material.h:996; иначе
#   CustomDepthRendering.cpp:575-588 «ignore translucent materials without allowing custom depth writes»).
#   Флаг ничего не стоит тем, кому трафарет не ставят (bRenderCustomDepth выключен: эффекты навыков и карт),
#   — компилируются лишь доп. шейдеры прохода глубины (DepthRendering.h:88-91, :128-131).
#
# ═══ ОТСЕЧКА «ГДЕ ЭФФЕКТ ЕСТЬ» ══════════════════════════════════════════════════════════════════════
#   • Translucent (M_AsdaEffectAlpha, M_AsdaCharLitSoul, M_AsdaPreviewFade): штатно —
#     clip(Opacity − OpacityMaskClipValue) в проходе глубины (DepthOnlyPixelShader.usf:91-92).
#     У «души» (Opacity = Tex.A·0.3) и появления (Tex.A·FadeIn) порог по умолчанию 0.3333 отсёк бы тело
#     целиком — ставится CLIP_BODY (0.01): тело группы — всё видимое тело. Тень «души» от порога не зависит
#     (ShadowReplace = 1.0 выше любого порога, Tools/asda_build_charlit_soul_mat.py); у появления тени-маски нет
#     (печатается, и при cast_dynamic_shadow_as_masked = True порог НЕ меняется).
#   • Additive / Modulate (M_AsdaEffectAdd, M_AsdaEffectModAdd, M_AsdaEffectMod): штатной отсечки в проходе
#     глубины НЕТ (DepthOnlyPixelShader.usf:91-96: clip только при MATERIALBLENDING_TRANSLUCENT) — квад
#     целиком лёг бы в трафарет, и формула свечения тронула бы фон вокруг эффекта. Поэтому в конец цепи
#     Emissive встаёт узел Custom `AsdaGlowMaskClip`: возвращает вход без изменений, а ТОЛЬКО в пиксельном
#     шейдере прохода глубины (`SCENE_TEXTURES_DISABLED` = 1 — DepthRendering.h:154; в основном проходе 0 —
#     BasePassRendering.cpp:872) отбрасывает пиксели со вкладом ниже порога материала OpacityMaskClipValue
#     (тот же, что у Translucent-мастера): Additive — вклад = max(Emissive.rgb), Modulate — вклад =
#     max(1 − Emissive.rgb) (множитель кадра). Кадр эффекта от узла не меняется ни на бит.
#
# ═══ ★ 19.09.2026 (glow_fix4): GM-hide — ПЛОЩАДЬ В ТРАФАРЕТЕ НЕ ЗАВИСИТ ОТ ПРИТУШЕНИЯ ══════════════════════════
#   Под GM-«скрыться» эффекты носителя притушены множителем вершинной альфы `FxAlphaScale` (asda.Gm.HideFxAlpha 0.3),
#   а вклад у M_AsdaEffectAdd и непрозрачность у M_AsdaEffectAlpha квадратичны по вершинной альфе (VertexColor.a
#   входит в цепь дважды) ⇒ при 0.3 вклад ×0.09, и порог 0.3333 выкидывал эффект из трафарета почти целиком.
#   Слабее притушенный эффект должен делать ВЕС полосы трафарета (C++ GlowStencilFor по FxAlphaScale), а не
#   отсечка. Поэтому C++ пишет эффекту в Custom Primitive Data[0] силу притушения Hide = 1 − FxAlphaScale
#   (UAsdaVideoSettingsSubsystem::kGlowFxHideCpdIndex; не задано = 0 = не притушен), а мастера в ПРОХОДЕ ГЛУБИНЫ
#   сравнивают с порогом вклад, приведённый к непритушенному: Add — порог × max(s², 1e-4) (узел `AsdaGlowHideClip`
#   на входе Clip), Alpha — Opacity / max(s², 1e-4) (узел `AsdaGlowMaskClip` на входе Opacity), s = 1 − Hide.
#   ModAdd (вклад ∝ 1 − альфа) и Mod (альфу не использует) от притушения так не слабеют — не трогаются.
#   В основном проходе оба узла мертвы (`#if SCENE_TEXTURES_DISABLED` / вход Clip используется только под ним).
#
# ★ ГРАФ НЕ ПЕРЕСОБИРАЕТСЯ: правка только ДОБАВЛЯЕТ узлы поверх существующей цепи (как asda_add_fx_glow_params.py);
#   `delete_all_material_expressions` не вызывается (баг UE 5.8, asda_mat_graph.py). Ручные правки мастеров
#   сохраняются — ассет грузится и дописывается, не создаётся заново.
# Идемпотентность: узел с описанием `AsdaGlowMaskClip` уже есть — не добавляется; флаг/порог уже стоят — не трогаются;
#   ★ glow_fix4: узел `AsdaGlowHideClip` (Add) уже есть — не добавляется.
# Запуск (редактор ЗАКРЫТ): коммандлет -run=pythonscript с -saveddirsuffix и копиями профиля (обёртка агента) либо
#   python Tools/run_headless_safe.py Tools/asda_add_glow_custom_depth.py --marker "GLOW-CUSTOM-DEPTH: ГОТОВО"
import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

TAG = "AsdaGlowMaskClip"        # описание узла Custom — признак уже применённой правки
HIDE_TAG = "AsdaGlowHideClip"   # ★ glow_fix4: описание узла деления порога Add на притушение
HIDE_PARAM = "AsdaGlowHide"     # ★ glow_fix4: параметр-CPD силы притушения (C++-константа снята в glow_fix6)
HIDE_CPD_INDEX = 0              # ★ glow_fix4: индекс Custom Primitive Data (C++ kGlowFxHideCpdIndex снят в glow_fix6)
CLIP_BODY = 0.01                # порог «души»/появления: всё видимое тело (Opacity = Tex.A·α)
# ★ 19.09.2026 (glow_fix5, С4): порог действует только там, где мастер пишет глубину, — в проходе CustomDepth (и в
#   тени-маске «души», где Opacity заменена на 1). Глубину/скорость кадра «душа» и появление НЕ пишут: флаг
#   «Output Depth and Velocity» (bOutputTranslucentVelocity) у обоих не взведён (в .uasset его нет, умолчание false —
#   UMaterial::SetInitialValues), а проход TranslucentVelocity берёт только такие материалы (VelocityRendering.cpp:1092);
#   в игре `IsTranslucencyWritingVelocity()` = 0 (прогон glow_fix5 D1). Тело группы за «душой» светится (прогон V2).
# (путь, режим): add — Additive (вклад = Emissive), modulate — Modulate (вклад = 1 − Emissive),
#   translucent — штатная отсечка по Opacity, body — Translucent тела игрока (порог CLIP_BODY).
# ★★★ 19.09.2026 (glow_fix6): ЦЕЛЕЙ НЕТ — скрипт НЕ ИСПОЛЬЗУЕТСЯ. Реверс К1 (разбор у объявления
#   UAsdaVideoSettingsSubsystem::GlowStencilGroup): полупрозрачное тело («душа», появление) в буфер свечения эталона
#   не рисуется вовсе — трафарет ему не нужен; эффекты предметов в группу не входят (С2 glow_fix5). Все шесть
#   мастеров (M_AsdaEffect{Add,Alpha,Mod,ModAdd}, M_AsdaCharLitSoul, M_AsdaPreviewFade) возвращены к состоянию до
#   волн из _backup/2026-09-19_glow_fix3 (бэкап состояния после волн — _backup/2026-09-19_glow_fix6). Код ниже оставлен
#   как образец на будущее (полосы «совела» — реестр отложенного); прежний список целей — в бэкапе glow_fix6.
#   Запуск с пустым списком ничего не меняет (печатает «изменено ассетов: 0 из 0»).
TARGETS = ()

# Тело узла Custom. `PIXELSHADER` — страховка: clip() допустим только в пиксельном шейдере
#   (ShaderCompiler.cpp:3470 ставит его по частоте шейдера).
# ★ glow_fix4: порог Add, приведённый к непритушенному вкладу (вклад Add ∝ s², s = 1 − Hide).
HIDE_CLIP_CODE = ("float asdaS = saturate(1.0 - Hide);\n"
                  "return Clip * max(asdaS * asdaS, 1e-4);")
# ★ glow_fix4: Opacity Alpha-мастера в проходе глубины, приведённая к непритушенной (∝ s²); в основном — как есть.
OPACITY_HIDE_CODE = ("#if SCENE_TEXTURES_DISABLED && PIXELSHADER\n"
                     "float asdaS = saturate(1.0 - Hide);\n"
                     "return In / max(asdaS * asdaS, 1e-4);\n"
                     "#endif\n"
                     "return In;")
CODE = {
    "add_hide": "#if SCENE_TEXTURES_DISABLED && PIXELSHADER\n"
                "clip(max(max(In.r, In.g), In.b) - Clip);\n"
                "#endif\n"
                "return In;",
    "add": "#if SCENE_TEXTURES_DISABLED && PIXELSHADER\n"
           "clip(max(max(In.r, In.g), In.b) - Clip);\n"
           "#endif\n"
           "return In;",
    "modulate": "#if SCENE_TEXTURES_DISABLED && PIXELSHADER\n"
                "float3 asdaK = 1.0 - In;\n"
                "clip(max(max(asdaK.r, asdaK.g), asdaK.b) - Clip);\n"
                "#endif\n"
                "return In;",
}


def log(m):
    unreal.log("[GLOW-CD] " + m)


def has_tag(mat):
    """Есть ли уже узел Custom с описанием TAG."""
    for e in MEL.get_material_expressions(mat):
        if isinstance(e, unreal.MaterialExpressionCustom):
            try:
                if str(e.get_editor_property("description")) == TAG:
                    return True
            except Exception:
                pass
    return False


def prop_source(mat, prop):
    """(узел, имя выхода) текущего входа материала — поверх него встраиваемся."""
    node = MEL.get_material_property_input_node(mat, prop)
    if not node:
        return None, ""
    try:
        out = str(MEL.get_material_property_input_node_output_name(mat, prop))
    except Exception:
        out = ""
    return node, out


def state(mat):
    """Строка состояния для лога: режим бленда, флаг, порог, тень-маска, узел отсечки, корень Emissive."""
    em, _ = prop_source(mat, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return "blend=%s allow_cd=%s clip=%.4f shadow_as_masked=%s custom=%s emissive<-%s узлов=%d" % (
        mat.get_editor_property("blend_mode"), mat.get_editor_property("allow_translucent_custom_depth_writes"),
        mat.get_editor_property("opacity_mask_clip_value"), mat.get_editor_property("cast_dynamic_shadow_as_masked"),
        has_tag(mat), type(em).__name__ if em else "НЕТ", len(MEL.get_material_expressions(mat)))


def add_clip_node(mat, mode):
    """Узел Custom в конец цепи Emissive: вход без изменений + отсечка только в проходе глубины."""
    src, out = prop_source(mat, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if not src:
        raise RuntimeError("вход Emissive пуст — отсечку ставить некуда")
    clip_val = float(mat.get_editor_property("opacity_mask_clip_value"))   # порог материала (как у Translucent)
    k = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, 2300, 1700)
    k.set_editor_property("r", clip_val)
    c = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, 2520, 1500)
    c.set_editor_property("description", TAG)
    c.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    ci_in = unreal.CustomInput(); ci_in.set_editor_property("input_name", "In")
    ci_clip = unreal.CustomInput(); ci_clip.set_editor_property("input_name", "Clip")
    c.set_editor_property("inputs", [ci_in, ci_clip])
    c.set_editor_property("code", CODE[mode])
    if not MEL.connect_material_expressions(src, out, c, "In"):
        raise RuntimeError("не подключился вход In узла отсечки")
    if not MEL.connect_material_expressions(k, "", c, "Clip"):
        raise RuntimeError("не подключился вход Clip узла отсечки")
    if not MEL.connect_material_property(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError("узел отсечки не подключился к Emissive")
    return clip_val


def has_desc(mat, desc):
    """Есть ли узел Custom с описанием desc."""
    for e in MEL.get_material_expressions(mat):
        if isinstance(e, unreal.MaterialExpressionCustom):
            try:
                if str(e.get_editor_property("description")) == desc:
                    return e
            except Exception:
                pass
    return None


def hide_param(mat, x, y):
    """Параметр-CPD силы притушения (glow_fix4): существующий по имени или новый."""
    for e in MEL.get_material_expressions(mat):
        if isinstance(e, unreal.MaterialExpressionScalarParameter) and str(e.get_editor_property("parameter_name")) == HIDE_PARAM:
            return e
    p = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    p.set_editor_property("parameter_name", HIDE_PARAM)
    p.set_editor_property("default_value", 0.0)
    p.set_editor_property("use_custom_primitive_data", True)
    p.set_editor_property("primitive_data_index", HIDE_CPD_INDEX)
    return p


def add_hide_clip(mat):
    """Add (glow_fix4): вход Clip узла AsdaGlowMaskClip ← AsdaGlowHideClip(Clip, Hide). Прежний источник Clip
    (константа порога материала) переподключается на вход нового узла — граф не пересоздаётся."""
    mask = has_desc(mat, TAG)
    if not mask:
        raise RuntimeError("нет узла %s — сначала отсечка" % TAG)
    srcs = MEL.get_inputs_for_material_expression(mat, mask)   # источники входов In, Clip (порядок входов)
    if len(srcs) < 2 or not srcs[1]:
        raise RuntimeError("у %s не найден источник входа Clip" % TAG)
    clip_src = srcs[1]
    r = MEL.get_input_node_output_name_for_material_expression(mask, clip_src)
    # Привязка UE Python: (bool, имя) либо только имя/None — разбираем оба вида.
    ok, clip_out = (bool(r[0]), str(r[1] or "")) if isinstance(r, tuple) else (r is not None, str(r or ""))
    hp = hide_param(mat, 2080, 1860)
    c = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, 2300, 1860)
    c.set_editor_property("description", HIDE_TAG)
    c.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    ci_c = unreal.CustomInput(); ci_c.set_editor_property("input_name", "Clip")
    ci_h = unreal.CustomInput(); ci_h.set_editor_property("input_name", "Hide")
    c.set_editor_property("inputs", [ci_c, ci_h])
    c.set_editor_property("code", HIDE_CLIP_CODE)
    if not MEL.connect_material_expressions(clip_src, clip_out if ok else "", c, "Clip"):
        raise RuntimeError("не подключился вход Clip узла %s" % HIDE_TAG)
    if not MEL.connect_material_expressions(hp, "", c, "Hide"):
        raise RuntimeError("не подключился вход Hide узла %s" % HIDE_TAG)
    if not MEL.connect_material_expressions(c, "", mask, "Clip"):
        raise RuntimeError("узел %s не подключился к Clip узла %s" % (HIDE_TAG, TAG))
    return float(clip_src.get_editor_property("r")) if isinstance(clip_src, unreal.MaterialExpressionConstant) else -1.0


def add_opacity_hide(mat):
    """Alpha (glow_fix4): Opacity ← AsdaGlowMaskClip(In = прежняя Opacity, Hide) — деление только в проходе глубины."""
    src, out = prop_source(mat, unreal.MaterialProperty.MP_OPACITY)
    if not src:
        raise RuntimeError("вход Opacity пуст")
    hp = hide_param(mat, 2080, 2060)
    c = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, 2300, 2060)
    c.set_editor_property("description", TAG)
    c.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    ci_in = unreal.CustomInput(); ci_in.set_editor_property("input_name", "In")
    ci_h = unreal.CustomInput(); ci_h.set_editor_property("input_name", "Hide")
    c.set_editor_property("inputs", [ci_in, ci_h])
    c.set_editor_property("code", OPACITY_HIDE_CODE)
    if not MEL.connect_material_expressions(src, out, c, "In"):
        raise RuntimeError("не подключился вход In узла Opacity")
    if not MEL.connect_material_expressions(hp, "", c, "Hide"):
        raise RuntimeError("не подключился вход Hide узла Opacity")
    if not MEL.connect_material_property(c, "", unreal.MaterialProperty.MP_OPACITY):
        raise RuntimeError("узел Opacity не подключился")


def patch(path, mode):
    mat = EAL.load_asset(path)
    if not mat:
        log("НЕТ АССЕТА %s" % path)
        return False
    log("%s ДО: %s" % (path, state(mat)))
    changed = False
    if not mat.get_editor_property("allow_translucent_custom_depth_writes"):
        mat.set_editor_property("allow_translucent_custom_depth_writes", True)
        changed = True
    if mode in CODE and not has_tag(mat):
        v = add_clip_node(mat, mode)
        log("%s: узел %s (%s), порог вклада %.4f" % (path, TAG, mode, v))
        changed = True
    if mode == "add_hide" and not has_desc(mat, HIDE_TAG):
        v = add_hide_clip(mat)
        log("%s: узел %s (порог × max(s², 1e-4), s = 1 − CPD[%d]), исходный порог %.4f" % (path, HIDE_TAG, HIDE_CPD_INDEX, v))
        changed = True
    if mode == "translucent_hide" and not has_tag(mat):
        add_opacity_hide(mat)
        log("%s: узел %s на Opacity (÷ max(s², 1e-4) в проходе глубины, s = 1 − CPD[%d])" % (path, TAG, HIDE_CPD_INDEX))
        changed = True
    if mode == "body":
        if mat.get_editor_property("cast_dynamic_shadow_as_masked") and "PreviewFade" in path:
            log("%s: cast_dynamic_shadow_as_masked=True — порог НЕ меняю (он задаёт тень-маску)" % path)
        elif abs(float(mat.get_editor_property("opacity_mask_clip_value")) - CLIP_BODY) > 1e-6:
            mat.set_editor_property("opacity_mask_clip_value", CLIP_BODY)
            changed = True
    if not changed:
        log("%s: уже применено — пропуск" % path)
        return False
    MEL.recompile_material(mat)
    mat.modify()
    ok = EAL.save_loaded_asset(mat, only_if_is_dirty=False)
    log("%s ПОСЛЕ: %s; сохранён=%s" % (path, state(mat), ok))
    if not ok:
        raise RuntimeError("не сохранился %s" % path)
    return True


def main():
    n = 0
    for path, mode in TARGETS:
        if patch(path, mode):
            n += 1
    log("изменено ассетов: %d из %d" % (n, len(TARGETS)))
    unreal.log("GLOW-CUSTOM-DEPTH: ГОТОВО")


if __name__ == "__main__":
    try:
        main()
    except Exception as ex:   # в лог — и маркер НЕ печатается (обёртка увидит сбой)
        import traceback
        unreal.log_error("[GLOW-CD] СБОЙ: %s\n%s" % (ex, traceback.format_exc()))
