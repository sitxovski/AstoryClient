# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# M_AsdaSoulAura — материал «АУРЫ» (glow-shell, свечение-оболочка) СИЛУЭТА «души» на экране
#   выбора персонажа. Накладывается ОВЕРЛЕЙ-ПРОХОДОМ на ТОТ ЖЕ SkeletalMesh силуэта
#   (USkeletalMeshComponent::SetOverlayMaterial), а НЕ отдельным billboard/квадом.
#
# ЗАЧЕМ / ОТКУДА. Дореверс `Reports/soulmate_soul_aura_render.md` (§0-§7) доказал, что
#   «аура» силуэта в оригинале — это ВТОРОЙ ПРОХОД по тому же скиннутому мешу тела (эталонный
#   объект `+0xE6FE4`, система glow-shell `pbbBuildStripBatch@0x11C15F0`, та же, что у заточки
#   оружия и квестовой метки «!»), с выдавливанием вершин по нормали и плоским аддитивным
#   свечением сквозь маску-облако `silhoutte_tex_blue`. Загрузчик в оригинале мёртв (доказано на
#   4 образах) ⇒ «эталонного» пикселя на экране нет; параметры — это ЗАМЫСЕЛ по коду (ВЫСОКАЯ
#   уверенность), а числовые допущения помечены ниже и вынесены в ключи владельцу
#   (`asda.Login.SoulAura*` в AsdaLoginGameMode.cpp).
#
# СПЕЦИФИКАЦИЯ (§2.2, §7 разбора) — дизасм перепроверен своими глазами (fix_soul_aura_impl.md §1):
#   Blend    = Additive   (SRCBLEND=SRCALPHA, DESTBLEND=ONE — дизасм d3dSetAlphaBlendState@0x111DCE0:
#                          push 5/push 0x13 и push 2/push 0x14)
#   Shading  = Unlit      (плоский oD0 = c91, освещения в вершинном шейдере нет)
#   TwoSided = true       (CULLMODE=D3DCULL_NONE(1) — дизасм 0x976D20: push 1/push 0x16)
#   ZWrite   = off        (ZWRITEENABLE=0 — дизасм 0x979910: push 0/push 0xe; для Additive в UE и
#                          так выкл)
#   WorldPositionOffset = normalize(VertexNormalWS) * Thickness      (mad r0.xyz, normal, c92.w)
#   Emissive = AuraTex.rgb * TintColor * (AuraTex.a * AuraAlpha)     (см. ★ФОЛД ниже)
#   Opacity  = 1                                                     (см. ★ФОЛД ниже)
#   UV       = TexCoord[0] + (0, Time * ScrollV)                     (Panner V = +0.5 UV/с, U=0)
#
# ★ФОЛД альфы в Emissive (наша реализация — идентична оригиналу по математике). Оригинал даёт
#   пиксель `dst += (tex.rgb*tint) * (tex.a*alpha(t))`. В UE аддитивный blend складывает вклад
#   `Emissive` c кадром. Чтобы «дыхание» альфы (AuraAlpha) работало НАДЁЖНО, независимо от того,
#   умножает ли конкретная версия UE Emissive на Opacity в аддитиве, всё сведено в Emissive, а
#   Opacity=1 — ровно как в рабочем `asda_build_target_highlight_mat.py`. Результат тот же самый.
#
# ПАРАМЕТРЫ (имена совпадают с тем, что гонит AsdaLoginGameMode.cpp::TickSoulAura):
#   AuraTex (Texture2D) · TintColor (Vector) · Thickness (Scalar) · AuraAlpha (Scalar) · ScrollV (Scalar)
#   Thickness/AuraAlpha покадрово гонит C++ (тween эталона, период 2000 мс). TintColor/ScrollV —
#   статические крутилки владельца (правятся в MID/мастере без пересборки).
#
# Запуск (headless, скрыто):
#   python Tools/run_headless_safe.py Tools/asda_build_soul_aura_mat.py --marker "[AURA] "
import unreal

mel = unreal.MaterialEditingLibrary
atools = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary

PKG, NAME = "/Game/Materials", "M_AsdaSoulAura"
path = "%s/%s" % (PKG, NAME)
AURA_TEX = "/Game/AsdaAssets/Textures/Characters/Texture/silhoutte_tex_blue"

# ── НАШИ дефолты (не из эталона — оригинальный пиксель на экране отсутствует, восстанавливаем
#    замысел; помечено законом fix-original-defects). Владелец меняет их в MID/мастере.
DEF_TINT     = unreal.LinearColor(0.5, 1.0, 1.0, 1.0)   # §2.2: c91.rgb = (0.5,1,1) — голубой/циан
DEF_SCROLL_V = 0.5     # §3: скролл V = +0.5 UV/с (U не едет); знак — ДОПУЩЕНИЕ (СРЕДНЯЯ, §8)
DEF_THICK    = 1.0     # ед. модели; амплитуду гонит C++ (0.5↔1.0), в см переводит SoulAuraHeightScale
DEF_ALPHA    = 1.0     # гонится C++ (кламп [0,1])

BAD = []
def E(cls, x, y):
    return mel.create_material_expression(m, cls, x, y)
def C(a, ao, b, bi):
    ok = mel.connect_material_expressions(a, ao, b, bi)
    unreal.log("[AURA] connect %s[%s] -> %s.%s : %s" % (a.get_name(), ao or "<default>", b.get_name(), bi, ok))
    if not ok: BAD.append("%s[%s]->%s.%s" % (a.get_name(), ao, b.get_name(), bi))
def P(e, eo, prop):
    ok = mel.connect_material_property(e, eo, prop)
    unreal.log("[AURA] property %s[%s] -> %s : %s" % (e.get_name(), eo or "<default>", prop, ok))
    if not ok: BAD.append("%s->%s" % (e.get_name(), prop))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global m, ok
    # ★★★ ЗАЩИТА СЦЕНЫ ЛОГИНА (15.09.2026, Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 13): удаляет, пересоздаёт и сохраняет ТОЛЬКО /Game/Materials/M_AsdaSoulAura —
    #   мастер ауры души на выборе персонажа, его грузит лишь AsdaLoginGameMode.cpp (текстуру silhoutte_tex_blue только читает).
    #   Запуск только с явного разрешения владельца и ASDA_ALLOW_LOGIN_WRITE=1 (иначе стоп, код 3).
    import os as _os_lg, sys as _sys_lg
    _sys_lg.path.insert(0, _os_lg.path.dirname(_os_lg.path.abspath(__file__)))   # Tools — для asda_login_guard
    from asda_login_guard import forbid_login_write
    forbid_login_write("asda_build_soul_aura_mat.py", "Materials/M_AsdaSoulAura (удаление, пересоздание, сохранение)")
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    m = atools.create_asset(NAME, PKG, unreal.Material, unreal.MaterialFactoryNew())
    # Аддитивный unlit-оверлей, двусторонний, не пишет глубину.
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)                       # CULLMODE=NONE эталона
    m.set_editor_property("used_with_skeletal_mesh", True)         # силуэт — скелетал; без флага движок отбраковывает оверлей
    m.set_editor_property("used_with_morph_targets", True)         # у силуэта морф-кадры (Init(5,1) @0xF05485)
    m.set_editor_property("used_with_static_lighting", True)
    m.set_editor_property("used_with_instanced_static_meshes", True)

    # ── Параметры ──
    tex = E(unreal.MaterialExpressionTextureSampleParameter2D, -900, -160)
    tex.set_editor_property("parameter_name", "AuraTex")
    if EAL.does_asset_exist(AURA_TEX):
        tex.set_editor_property("texture", unreal.load_asset(AURA_TEX))   # маска-облако silhoutte_tex_blue
    else:
        unreal.log_warning("[AURA] текстуры %s нет — AuraTex останется дефолтной" % AURA_TEX)
    tint = E(unreal.MaterialExpressionVectorParameter, -900, 120)
    tint.set_editor_property("parameter_name", "TintColor")
    tint.set_editor_property("default_value", DEF_TINT)
    alpha = E(unreal.MaterialExpressionScalarParameter, -900, 300)
    alpha.set_editor_property("parameter_name", "AuraAlpha")
    alpha.set_editor_property("default_value", DEF_ALPHA)
    thick = E(unreal.MaterialExpressionScalarParameter, -900, 470)
    thick.set_editor_property("parameter_name", "Thickness")
    thick.set_editor_property("default_value", DEF_THICK)
    scrollv = E(unreal.MaterialExpressionScalarParameter, -900, 640)
    scrollv.set_editor_property("parameter_name", "ScrollV")
    scrollv.set_editor_property("default_value", DEF_SCROLL_V)

    # ── UV = TexCoord[0] + (0, Time * ScrollV) — «дымка течёт» по V ──
    tc = E(unreal.MaterialExpressionTextureCoordinate, -1300, -220)
    tc.set_editor_property("coordinate_index", 0)
    tnode = E(unreal.MaterialExpressionTime, -1300, -60)
    mulV = E(unreal.MaterialExpressionMultiply, -1120, -80)
    C(tnode, "", mulV, "A")
    C(scrollv, "", mulV, "B")
    zero = E(unreal.MaterialExpressionConstant, -1120, 40)
    zero.set_editor_property("r", 0.0)
    appUV = E(unreal.MaterialExpressionAppendVector, -960, -120)   # float2(U=0, V=Time*ScrollV)
    C(zero, "", appUV, "A")
    C(mulV, "", appUV, "B")
    addUV = E(unreal.MaterialExpressionAdd, -1080, -200)
    C(tc, "", addUV, "A")
    C(appUV, "", addUV, "B")
    C(addUV, "", tex, "UVs")

    # ── Emissive = AuraTex.rgb * TintColor * (AuraTex.a * AuraAlpha) — ★ФОЛД (см. шапку) ──
    aA = E(unreal.MaterialExpressionMultiply, -560, 200)
    C(tex, "A", aA, "A")            # tex.a
    C(alpha, "", aA, "B")          # * AuraAlpha
    rgbTint = E(unreal.MaterialExpressionMultiply, -560, -80)
    C(tex, "RGB", rgbTint, "A")
    C(tint, "", rgbTint, "B")      # у VectorParameter выход по умолчанию = RGBA (RGB-выхода нет)
    emis = E(unreal.MaterialExpressionMultiply, -320, 20)
    C(rgbTint, "", emis, "A")
    C(aA, "", emis, "B")
    P(emis, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    one = E(unreal.MaterialExpressionConstant, -320, 260)
    one.set_editor_property("r", 1.0)
    P(one, "", unreal.MaterialProperty.MP_OPACITY)

    # ── WorldPositionOffset = normalize(VertexNormalWS) * Thickness — выдавливание по нормали ──
    vn = E(unreal.MaterialExpressionVertexNormalWS, -700, 500)
    norm = E(unreal.MaterialExpressionNormalize, -520, 500)
    C(vn, "", norm, "")
    wpo = E(unreal.MaterialExpressionMultiply, -320, 500)
    C(norm, "", wpo, "A")
    C(thick, "", wpo, "B")
    P(wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

    m.modify()
    mel.recompile_material(m)
    ok = EAL.save_loaded_asset(m)
    if BAD:
        unreal.log_error("[AURA] НЕ ПОДКЛЮЧЕНО: %s" % BAD)
    unreal.log("[AURA] %s собран, save=%s, битых соединений=%d "
               "(blend=Additive, shading=Unlit, twoSided=1, tint=(%.2f,%.2f,%.2f), scrollV=%.2f)"
               % (path, ok, len(BAD), DEF_TINT.r, DEF_TINT.g, DEF_TINT.b, DEF_SCROLL_V))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
