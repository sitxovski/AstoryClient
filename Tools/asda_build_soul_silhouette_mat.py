# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# M_AsdaSoulSilhouette — мастер-материал ПРИЗРАКА («душа», объект `+946128`) на экране выбора
#   персонажа. Меш `cre_cresoulman-a_00.jbm` (муж., 3 подмеша) / `cre_cresoulwoman-a_00.jbm`
#   (жен., 4 подмеша) со СВОЕЙ текстурой `cre_cresoulman/woman-a_00.TEX` (сине-белые облака, DXT2).
#
# ★★★ ПЕРЕСОБРАН 03.09.2026 ПО ЭТАЛОНУ (`Reports/soul_silhouette_visual_re.md` §2.3, §7, §8;
#   бриф `audit_2026-09-02/_БРИФ_ПРАВОК.md`, отчёт `fix_soul_ghost_charselect.md`).
#   ЧТО БЫЛО (наш дизайн, ВЫДУМАНО): Translucent + Unlit + TwoSided, Emissive = tex·TintColor·
#   EmissiveBoost(2.0), серый тинт. Реверс показал: Unlit/буст/двусторонность/тинт в движке
#   оригинала для ЭТОГО меша ниоткуда не следуют — у него ЛЮБОЙ подмеш персонажа рисуется
#   ОСВЕЩЁННЫМ, ОДНОСТОРОННИМ (CULLMODE=CW, флаг `&1` у обычных подмешей не ставится —
#   `d3dApplyMaterialRenderState@0x111DA90 :846418`), БЕЗ тинта (diffuse.rgb = 1 — VS `mov oD0.xyz,
#   c95.w`), стадия 0 = MODULATE(TEXTURE, DIFFUSE) (:19317-19323) — то есть цвет пикселя = цвет
#   текстуры, освещение как у всех.
#   ЧТО СТАЛО (взято из эталона):
#     BaseColor = BaseTex.rgb                         — своя сине-облачная текстура меша, без тинта
#     Shading   = DefaultLit                          — освещённый, как любой меш этого движка
#     TwoSided  = False                               — односторонний CW
#     Blend     = Translucent (SRCALPHA/INVSRCALPHA — `CRenderer_ApplyBlendRenderState :18518`)
#     Z-тест    = вкл (bDisableDepthTest=False)
#     Освещение translucent — Surface ForwardShading (TLM_SurfacePerPixelLighting): у оригинала
#       прозрачный проход освещается так же, как непрозрачный (тот же VS/стадии), в UE это ближайший
#       эквивалент «лит-полупрозрачного».
#   ★ [ПОРТ] ДОПУЩЕНИЕ — ПРОЗРАЧНОСТЬ. Живого прохода именно этого меша в оригинале НЕТ (загрузчик
#     `@0xF05420` мёртв, материал меша не доказан). Единственное ДОКАЗАННОЕ «полупрозрачное» в
#     системе «душа» — α = 0.3 в МИРЕ (`charEntityUpdateTick :80703`, константа 0x3E99999A):
#     `Opacity = tex.a × 0.3`, ALPHAOP = MODULATE(TEXTURE.a, DIFFUSE.a). Берём его как ближайший
#     эталон: Opacity = BaseTex.a × OpacityScale, OpacityScale = 0.3. Крутится в MIC без пересборки.
#   ★ [ПОРТ] Z-ЗАПИСЬ: у оригинала translucent-проход ПИШЕТ глубину (ZWRITEENABLE=1 из
#     `CRenderer_ApplyAlphaFogRenderState :18578`); в UE translucent глубину не пишет — эквивалента
#     нет, ближайшее: `SetTranslucentSortPriority` на компоненте (AsdaLoginGameMode::SpawnSoulInSlot).
#   ★ [ПОРТ] Roughness/Metallic/SpecBase/AmbientFloor — ТЕ ЖЕ значения и та же схема, что у мастера
#     персонажа `M_AsdaCharLit` (`asda_build_materials_lit.py::wire_char`): «рендер как у любого
#     меша этого движка» в порту означает «как у персонажа». AmbientFloor 0.25 — проектное
#     аниме-правило «тень никогда не чёрная» (asda_add_ambient_floor.py), не из оригинала.
#   ★ Текстура: PNG порта (`RawAssets/Textures/Data/Character/Texture/cre_cresoul*-a_00.png`) —
#     ПРЯМОЙ декод DXT2 (premultiplied: corr(R,A)=0.90 у муж.), D3D9 сэмплирует DXT2 как есть, поэтому
#     BaseColor = tex.rgb БЕЗ деления на альфу воспроизводит пиксель оригинала 1:1.
#   Дымка `silhoutte_tex_blue` — ОТДЕЛЬНЫЙ pbb-проход (`M_AsdaSoulAura`, overlay), здесь её нет.
#
# ★ БЕЗ delete_asset: пересоздание мастера обнулило бы parent у MIC четырёх наборов силуэта
#   (`cre_cresoul{man,woman,mwwoman,wmman}_preview/Materials/*`). Чистим граф СУЩЕСТВУЮЩЕГО ассета
#   и собираем заново, как `asda_fix_charlit_lighting.py`.
#   Если мастера ещё нет — создаём. В конце ПРОВЕРЯЕМ все MIC силуэтов: parent = этот мастер,
#   BaseTex = нативная текстура по полу меша (как ставит `import_char_possess.py`); чиним, если нет.
#
# ★★★ ГРАБЛЯ UE 5.8 `DeleteAllMaterialExpressions` (ревизия `review_soul_ghost_charselect.md` §1,
#   03.09.2026): один вызов удаляет только КАЖДЫЙ ВТОРОЙ узел (удаление из массива по ходу итерации
#   по нему же, `MaterialEditingLibrary.cpp:559-568`). Первый прогон этого скрипта (03.09 11:35)
#   оставил в ассете фантомы старого графа — `TintColor`, второй `OpacityScale` с дефолтом 1.0 и
#   лишний `Multiply`; кэш параметров взял первый попавшийся `OpacityScale` = 1.0, и все 14 MIC отдавали
#   1.0 вместо 0.3 (лог печатал константу скрипта, а не ассет). Промежуточный recompile этого НЕ чинит.
#   ЛЕЧЕНИЕ (`asda_mat_graph.py`): чистка В ЦИКЛЕ до `get_material_expressions == 0` (11→5→2→0) и
#   КОНТРОЛЬ графа после сборки — ровно EXPECTED_EXPRESSIONS узлов и ни одного дубля имени параметра;
#   иначе ошибка и ассет НЕ сохраняется. Итог печатается ИЗ АССЕТА (число узлов, дефолт OpacityScale
#   из кэша, эффективный OpacityScale каждого MIC), а не из констант скрипта.
#
# ★ ТЕНЬ (правка 03.09.2026 по ревизии §3.3). У «души» в мире тень ПОЛНАЯ, непрозрачная: shadow-pass
#   оригинала гасит ALPHABLENDENABLE (`:422630`; уверенность разбора — СРЕДНЯЯ, для ЭТОГО меша
#   загрузчик мёртв и тень не доказана, берём поведение «души» как ближайший эталон). В UE
#   Translucent-материал в shadow-depth НЕ ПИШЕТСЯ ВОВСЕ, пока `cast_dynamic_shadow_as_masked=False`,
#   — `SetCastShadow(true)` на компоненте ничего не даёт. С флагом тень режется как Masked по
#   `opacity_mask_clip_value` (0.333), а наш Opacity = tex.a×0.3 ≤ 0.3 ⇒ тень была бы пустой. Поэтому
#   ТА ЖЕ схема, что у мирового `M_AsdaCharLitSoul` (`asda_build_charlit_soul_mat.py`):
#     cast_dynamic_shadow_as_masked = True
#     Opacity = ShadowReplace(Default = BaseTex.a × OpacityScale, Shadow = 1.0)
#   — в основном проходе прежние 0.3, в shadow-pass 1.0 (заведомо выше порога) ⇒ тень полная по
#   контуру меша, как «ALPHABLENDENABLE=0 на shadow-pass». Откат: снять флаг и ветку Shadow (тени не
#   будет вовсе).
#
# ПАРАМЕТРЫ (имя `BaseTex` совпадает с тем, что назначает Tools/import_char_possess.py):
#   BaseTex (Texture2D) · OpacityScale (Scalar, 0.3) · Roughness (0.9) · Metallic (0) ·
#   SpecBase (0.5) · AmbientFloor (0.25)
#   Удалены (были выдуманы): TintColor, EmissiveBoost.
#
# Запуск (headless, СКРЫТО — Start-Process -WindowStyle Hidden; успех ТОЛЬКО по логу):
#   python Tools/run_headless_safe.py Tools/asda_build_soul_silhouette_mat.py --marker "[SOUL] FINISHED"
#   ★ 15.09.2026: только с явного разрешения владельца — ASDA_ALLOW_LOGIN_WRITE=1 (пакеты сцены логина, Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 15).
import os
import sys
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_mat_graph as MG    # чистка графа до пустого + контроль после сборки (обход бага UE 5.8)

mel = unreal.MaterialEditingLibrary
atools = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()

PKG, NAME = "/Game/Materials", "M_AsdaSoulSilhouette"
path = "%s/%s" % (PKG, NAME)

# ── Значения. Помечено: что из эталона, что [ПОРТ].
DEF_OPACITY_SCALE = 0.3     # [ПОРТ] ближайший эталон — α 0.3 «души» в мире (charEntityUpdateTick :80703)
DEF_ROUGH         = 0.9     # [ПОРТ] как у M_AsdaCharLit (ROUGH)
DEF_METAL         = 0.0     # [ПОРТ] как у M_AsdaCharLit
DEF_SPEC_BASE     = 0.5     # [ПОРТ] как у M_AsdaCharLit (= дефолт UE)
DEF_AMBIENT_FLOOR = 0.0     # [ПОРТ] как у M_AsdaCharLit: с 2026-09-03 (Д-3) пол 0 — узел-рычаг остаётся, «двойной свет» снят
DEF_SHADOW_OPACITY = 1.0    # ветка Shadow у ShadowReplace: «ALPHABLENDENABLE=0 на shadow-pass» (:422630) — тень полная

# Ровно столько узлов строит wire(): BaseTex, Roughness, Metallic, SpecBase, AmbientFloor,
#   OpacityScale, Multiply(Emissive), Multiply(Opacity), Constant(1.0), ShadowReplace = 10.
#   При изменении wire() — обновить. Расхождение после сборки = фантомы бага UE 5.8 → НЕ сохранять.
EXPECTED_EXPRESSIONS = 10

# Наборы силуэта, чьи MIC должны сидеть на этом мастере (`import_char_possess.py::SOUL`).
SOUL_SETS = ["cre_cresoulman_preview", "cre_cresoulwoman_preview",
             "cre_cresoulmwwoman_preview", "cre_cresoulwmman_preview"]
SOUL_DIR = "/Game/AsdaAssets/Models/Skeletal/Characters"
NATIVE_SOUL_TEX = {
    "man":   "/Game/AsdaAssets/Textures/Characters/Texture/cre_cresoulman-a_00",
    "woman": "/Game/AsdaAssets/Textures/Characters/Texture/cre_cresoulwoman-a_00",
}

BAD = []
def E(cls, x, y):
    return mel.create_material_expression(m, cls, x, y)
def C(a, ao, b, bi):
    ok = mel.connect_material_expressions(a, ao, b, bi)
    unreal.log("[SOUL] connect %s[%s] -> %s.%s : %s" % (a.get_name(), ao or "<default>", b.get_name(), bi, ok))
    if not ok: BAD.append("%s[%s]->%s.%s" % (a.get_name(), ao, b.get_name(), bi))
def P(e, eo, prop):
    ok = mel.connect_material_property(e, eo, prop)
    unreal.log("[SOUL] property %s[%s] -> %s : %s" % (e.get_name(), eo or "<default>", prop, ok))
    if not ok: BAD.append("%s->%s" % (e.get_name(), prop))
def scal(nm, v, x, y):
    s = E(unreal.MaterialExpressionScalarParameter, x, y)
    s.set_editor_property("parameter_name", nm)
    s.set_editor_property("default_value", v)
    return s


def wire(mat):
    """Граф призрака по эталону §2.3/§8 (см. шапку). Идемпотентно: вызывающий чистит граф до нас."""
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)          # SRCALPHA/INVSRCALPHA
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)  # освещённый, как любой меш
    mat.set_editor_property("two_sided", False)                                         # CULLMODE=CW — односторонний
    mat.set_editor_property("disable_depth_test", False)                                # ZENABLE=1
    # Освещение полупрозрачного по поверхности (forward): ближайший эквивалент «лит + translucent».
    mat.set_editor_property("translucency_lighting_mode",
                            unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    mat.set_editor_property("used_with_skeletal_mesh", True)      # призрак — скелетал; без флага движок отбраковывает
    mat.set_editor_property("used_with_morph_targets", True)      # у призрака 5 морф-кадров (Init(5,1) @0xF05485)
    mat.set_editor_property("used_with_static_lighting", True)
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    # ТЕНЬ ПОЛНАЯ (см. шапку): без флага translucent в shadow-depth не пишется вовсе; с флагом —
    #   как masked по opacity_mask_clip_value, порог перекрывает ветка Shadow=1.0 ниже.
    mat.set_editor_property("cast_dynamic_shadow_as_masked", True)

    tex = E(unreal.MaterialExpressionTextureSampleParameter2D, -700, -120)
    tex.set_editor_property("parameter_name", "BaseTex")          # своя сине-облачная текстура меша
    rough = scal("Roughness", DEF_ROUGH, -700, 140)
    metal = scal("Metallic", DEF_METAL, -700, 260)
    sbase = scal("SpecBase", DEF_SPEC_BASE, -700, 380)
    floor = scal("AmbientFloor", DEF_AMBIENT_FLOOR, -700, 500)
    opsc  = scal("OpacityScale", DEF_OPACITY_SCALE, -700, 620)

    # BaseColor = BaseTex.rgb — без тинта (diffuse.rgb = 1 в VS оригинала), без vertex color
    #   (oD0.xyz = c95.w — константа, цвет вершин в этот проход не идёт).
    P(tex, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    P(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    P(metal, "", unreal.MaterialProperty.MP_METALLIC)
    P(sbase, "", unreal.MaterialProperty.MP_SPECULAR)
    # Emissive = BaseColor × AmbientFloor — [ПОРТ] проектный «пол» освещения, как у M_AsdaCharLit.
    em = E(unreal.MaterialExpressionMultiply, -380, -40)
    C(tex, "RGB", em, "A")
    C(floor, "", em, "B")
    P(em, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    # Opacity = BaseTex.a × OpacityScale — ALPHAOP=MODULATE(TEXTURE.a, DIFFUSE.a=0.3) [ПОРТ, см. шапку].
    mulO = E(unreal.MaterialExpressionMultiply, -380, 560)
    C(tex, "A", mulO, "A")
    C(opsc, "", mulO, "B")
    # В shadow-pass — 1.0: тень полная, непрозрачная (shadow-pass оригинала гасит ALPHABLENDENABLE,
    #   :422630; для ЭТОГО меша не доказано — ближайший эталон «душа» в мире). Та же схема, что у
    #   `M_AsdaCharLitSoul` (asda_build_charlit_soul_mat.py).
    one = E(unreal.MaterialExpressionConstant, -380, 700)
    one.set_editor_property("r", DEF_SHADOW_OPACITY)
    sw = E(unreal.MaterialExpressionShadowReplace, -150, 600)
    C(mulO, "", sw, "Default")
    C(one, "", sw, "Shadow")
    P(sw, "", unreal.MaterialProperty.MP_OPACITY)


def verify_instances(master):
    """MIC четырёх наборов силуэта: parent = мастер, BaseTex = нативная текстура по полу меша."""
    fixed = seen = bad_op = 0
    for nm in SOUL_SETS:
        sub = "%s/%s/Materials" % (SOUL_DIR, nm)
        if not EAL.does_directory_exist(sub):
            unreal.log_warning("[SOUL] набора %s нет (%s) — MIC не проверяю" % (nm, sub))
            continue
        want_tex = NATIVE_SOUL_TEX["woman"] if "woman" in nm else NATIVE_SOUL_TEX["man"]
        tex = unreal.load_asset(want_tex)
        for ad in AR.get_assets_by_path(unreal.Name(sub), recursive=True):
            if str(ad.asset_class_path.asset_name) != "MaterialInstanceConstant":
                continue
            mic = unreal.load_asset(str(ad.package_name))
            if not mic: continue
            seen += 1
            parent = mic.get_editor_property("parent")
            cur_tex = mel.get_material_instance_texture_parameter_value(mic, "BaseTex")
            dirty = False
            if parent is None or parent.get_path_name().split(".")[0] != path:
                mel.set_material_instance_parent(mic, master); dirty = True
            if tex and (cur_tex is None or cur_tex.get_path_name().split(".")[0] != want_tex):
                mel.set_material_instance_texture_parameter_value(mic, "BaseTex", tex); dirty = True
            if dirty:
                EAL.save_loaded_asset(mic, False); fixed += 1
            # Эффективный OpacityScale ИЗ MIC (наследуется от мастера) — именно он врал при фантомах.
            eff = mel.get_material_instance_scalar_parameter_value(mic, "OpacityScale")
            if abs(eff - DEF_OPACITY_SCALE) > 1e-4:
                bad_op += 1
            unreal.log("[SOUL] MIC %s: parent=%s BaseTex=%s OpacityScale(eff)=%.3f%s" % (
                str(ad.asset_name),
                (parent.get_name() if parent else "<НЕТ>"),
                (cur_tex.get_name() if cur_tex else "<НЕТ>"),
                eff,
                " → ПОЧИНЕНО" if dirty else ""))
    unreal.log("[SOUL] MIC силуэтов: проверено %d, починено %d, с чужим OpacityScale %d"
               % (seen, fixed, bad_op))
    if bad_op:
        unreal.log_error("[SOUL] у %d MIC эффективный OpacityScale ≠ %.2f — фантом параметра в мастере?"
                         % (bad_op, DEF_OPACITY_SCALE))
    return seen


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global m
    # ★★★ ЗАЩИТА СЦЕНЫ ЛОГИНА (15.09.2026, Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 15): инструмент пишет ТОЛЬКО пакеты сцены логина —
    #   мастер /Game/Materials/M_AsdaSoulSilhouette (его берут лишь 14 MIC силуэтов) и MIC наборов cre_cresoul*_preview.
    #   Без ASDA_ALLOW_LOGIN_WRITE=1 — стоп до загрузки мастера. Пропуск наборов из раздела 14 стал лишним и снят.
    from asda_login_guard import forbid_login_write   # Tools уже в sys.path (строка перед импортом asda_mat_graph)
    forbid_login_write("asda_build_soul_silhouette_mat.py",
                       "мастер /Game/Materials/M_AsdaSoulSilhouette и MIC силуэтов души cre_cresoul*_preview")
    created = False
    if EAL.does_asset_exist(path):
        m = EAL.load_asset(path)
        # ★ Чистка В ЦИКЛЕ до пустого графа — один delete_all оставляет каждый второй узел (см. шапку).
        MG.clear_expressions(m, log=unreal.log, tag="[SOUL]")
        # ГРАБЛЯ (asda_fix_charlit_lighting.py): кэш параметров обновляется с задержкой — промежуточный
        #   recompile на ПУСТОМ (теперь действительно пустом) графе сбрасывает кэш старых имён.
        mel.recompile_material(m)
    else:
        m = atools.create_asset(NAME, PKG, unreal.Material, unreal.MaterialFactoryNew())
        created = True
    wire(m)
    if BAD:
        unreal.log_error("[SOUL] НЕ ПОДКЛЮЧЕНО: %s — ассет НЕ сохранён" % BAD)
        raise RuntimeError("[SOUL] битые соединения: %s" % BAD)
    # ★ КОНТРОЛЬ ГРАФА ДО СОХРАНЕНИЯ: ровно EXPECTED_EXPRESSIONS узлов и ни одного дубля имени
    #   параметра; иначе RuntimeError и ассет остаётся как был (на диске).
    n_expr = MG.check_graph(m, expected=EXPECTED_EXPRESSIONS, log=unreal.log, tag="[SOUL]")
    m.modify()
    mel.recompile_material(m)
    ok = EAL.save_loaded_asset(m, False)   # правки через MEL не помечают пакет dirty → only_if_is_dirty=False
    # Итог печатаем ИЗ АССЕТА, а не из констант скрипта (прошлый лог «OpacityScale=0.30» врал).
    scalars = [str(n) for n in mel.get_scalar_parameter_names(m)]
    vectors = [str(n) for n in mel.get_vector_parameter_names(m)]
    op_cache = mel.get_material_default_scalar_parameter_value(m, "OpacityScale")
    shadow_masked = bool(m.get_editor_property("cast_dynamic_shadow_as_masked"))
    op_node = mel.get_material_property_input_node(m, unreal.MaterialProperty.MP_OPACITY)
    unreal.log("[SOUL] после сборки: узлов=%d, скаляры=%s, векторы=%s, кэш OpacityScale=%.3f, "
               "cast_dynamic_shadow_as_masked=%s, Opacity<-%s"
               % (n_expr, scalars, vectors, op_cache, shadow_masked,
                  op_node.get_class().get_name() if op_node else "<ОБРЫВ>"))
    seen = verify_instances(m)
    unreal.log("[SOUL] ИТОГ %s %s, save=%s, узлов=%d, битых соединений=%d, MIC=%d "
               "(blend=Translucent, shading=DefaultLit, twoSided=0, lighting=SurfacePerPixel, "
               "OpacityScale(кэш ассета)=%.3f, shadowAsMasked=%s, tint/emissive-boost=%s)"
               % (path, "создан" if created else "пересобран на месте", ok, n_expr, len(BAD), seen,
                  op_cache, shadow_masked, "ЕСТЬ!" if vectors else "НЕТ"))
    if not ok:
        raise RuntimeError("[SOUL] save_loaded_asset вернул False — на диске осталась прежняя версия")
    unreal.log("[SOUL] FINISHED")   # ASCII-маркер для run_headless_safe (кириллица в аргументах ненадёжна)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
