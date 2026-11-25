# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_gacha_glass_material.py — СТЕКЛЯННЫЙ КУПОЛ автомата премиум-гачи.
#
# ★★★ ЧТО ДОКАЗАНО ДАННЫМИ (никаких догадок).
#   1) В `.jbm` у КАЖДОГО меша есть поле рендер-флагов `mr+0x004` (см. `asda_jbm.py`, MR_FLAGS).
#      У `md_gachya_{a..e}_00.jbm` бит 0x1 стоит РОВНО у трёх подсеток: `Bowl` (купол) и двух
#      квадов тени `G_shadow`/`Ball_shadow`. У остальных 17 — ноль.
#   2) Что бит 0x1 = «рисовать с альфа-блендингом», подтверждено ВТОРЫМ методом (закон
#      false-negatives-method): по трём архивам клиента (Character/Mob/NPC) среди мешей С битом
#      текстуру с частичной альфой носят 87.8 %, среди мешей БЕЗ бита — 12.4 %.
#   3) Что купол ОБЯЗАН быть сквозным, доказано геометрией: девять шаров `Object01…Object10`
#      (по 151 вершине) целиком лежат внутри габарита `Bowl` — X[−38..44] Y[70..101] Z[−255..−172]
#      против купола X[−46..45] Y[70..148] Z[−256..−164]. При непрозрачном куполе эти 1359 вершин
#      не видны НИКОГДА.
#   4) Уравнение смешивания — из эталона `CRenderer_ApplyBlendRenderState@0x9764F0`:
#      `ALPHABLENDENABLE=1, SRCBLEND=5 (SRCALPHA), DESTBLEND=6 (INVSRCALPHA)` — обычный alpha-blend.
#
#   5) ГДЕ В ОРИГИНАЛЕ БЕРЁТСЯ САМА АЛЬФА — реверс доведён до конца (2026-08-17):
#      · меш с флагом кладётся в ОТСОРТИРОВАННУЮ ПО ГЛУБИНЕ очередь: `entity_char.c:8980`
#        `if ((meshRec[668*j+4] & 1) || (meshRec[668*j+4] & 2)) CRenderQueue_AddObject(&g_RenderQueue,
#        meshRec+668*j+384, meshRec+668*j+8, …)`; очередь сортируется `CRenderQueue_QSortByDepth`
#        и сливается `CRenderQueue_Flush` ПОСЛЕ непрозрачного прохода. Непрозрачной геометрии
#        сортировка не нужна ⇒ бит = «полупрозрачный». То же в `effects_particles.c:66725`;
#      · `CMeshNode` встроен в запись меша со смещением **+0x180** (доказано побайтово:
#        node+0x0C = faceCount = triCount записи, node+0x10 = vertexCount, node+0x14 → SetFVF =
#        поле fvf @+0x194, node+0x18 → stride @+0x198, node+0x44 → D3D VB/IB @+0x1C4/0x1C8);
#        `flags_ptr` (node+0x00) указывает на **meshType** (+0x000), `pStateFlags` (node+0x30) —
#        на **наше поле** (+0x004);
#      · `d3dApplyMaterialRenderState@0x111DA90`: `meshType&1` → `CULLMODE=NONE`,
#        `meshType&8` → `d3dSetAlphaBlendState` (SRCALPHA/**ONE**, аддитив — у моделей бита 8 нет),
#        иначе `CRenderer_ApplyBlendRenderState` (SRCALPHA/INVSRCALPHA);
#        `meshType&0x2000` → альфа-аргумент = TFACTOR, иначе `gfxRestoreRenderState2000` возвращает
#        `D3DTSS_ALPHAARG2 = 1` (для стадии 0 это DIFFUSE);
#      · диффузная альфа приходит из вершинного шейдера: во ВСЕХ модельных блоках vs.1.1 из
#        `system/AsdaStory.exe` это `mov oD0.w, c92.w` (или `mad oD0.w, r5.x, r3.w, c92.w` —
#        дальняя доводка), а `c92.w` = `CMeshNode.pTexScroll[0]` (структура: «w из field56»),
#        то есть ГЛОБАЛЬНЫЙ ФЕЙД ОБЪЕКТА; тот же фейд идёт в `D3DRS_TEXTUREFACTOR`
#        (`render_core.c:40284`) и в `NoSkinTexScroll.w` (`model_anim.c:24608`,
#        `EmptyProject.fx` PShade).
#      ⇒ **альфа помеченного меша = альфа текстуры × фейд объекта.** Отдельного числа на меш НЕТ.
#
# ⚠ СЛЕДСТВИЕ ДЛЯ КУПОЛА (проверено исчерпывающе, ничего не «не найдено»):
#   · текстура `md_Gachya_*_00.TEX` = A8R8G8B8, альфа 255 у 100 % пикселей (прочитано прямо из
#     `Character/Texture/Texture.btz` и вторым способом из наших PNG);
#   · фейд полностью проявленного автомата = 1.0;
#   · в записи меша 0x29C числа-непрозрачности нет (сдиффены ВСЕ dword записи у пяти подсеток);
#   · в записи `matTable` (0x20 байт) хвост после имени — мусор переиспользованного буфера
#     (гистограмма по 2094 записям трёх архивов: значения вида 1.7e22, −1.07e8 — не поле 0..1);
#   · вершинный DIFFUSE (смещение 24 в 44-байтной вершине) у всех подсеток автомата = 0x00000000
#     и в шейдерном пути не участвует.
#   ⇒ В ОРИГИНАЛЕ КУПОЛ РИСУЕТСЯ НЕПРОЗРАЧНЫМ: **альфа = 1 × 1 = 1.0**.
#
# ★★★ ИСПРАВЛЕНО 2026-08-18. Прошлый заход из этого же вывода сделал НЕВЕРНЫЙ шаг: решил, что
#   непрозрачный купол — «дефект оригинала» (иначе девять шаров `Object01…Object10`, лежащих
#   ВНУТРИ купола, не видны никогда), и поставил `GlassOpacity = 0.5`. Шары в оригинале видны —
#   но НЕ из-за прозрачности, а из-за ПОРЯДКА ОТРИСОВКИ: прозрачные подсетки идут первым проходом
#   БЕЗ записи Z, непрозрачные — вторым, по пустому Z-буферу, то есть ПОВЕРХ купола
#   (`PetWindow_RenderToTarget@0xB6D5B0` + `meshNodeRenderDispatch@0x111E110`; полный вывод —
#   `asda_gacha_light_node.apply_behind_opaque`). Как только порядок воспроизведён, купол обязан
#   быть непрозрачным — и `GlassOpacity` возвращается к эталонной **1.0**.
#   Именно та половинчатая плёнка и была «СВЕТЛЫМИ лутбоксами»: купол — самый крупный элемент
#   автомата, и смешанный 50/50 со светлой подложкой окна он делал бледной всю верхушку
#   (замер — `Reports/gacha_shadow_black.md` §4).
#
# Мастер: Unlit + Translucent, two-sided, цвет = `Tex.rgb`,
#   **непрозрачность = `Tex.A × GlassOpacity`** — ровно формула оригинала (альфа текстуры × фейд),
#   где `GlassOpacity` играет роль ФЕЙДА ОБЪЕКТА (`c92.w`). У полностью проявленного автомата фейд
#   равен 1.0, поэтому и дефолт параметра — **1.0**: оригинал 1:1, без единого подобранного числа.
#   Unlit — потому что в оригинале купол рисуется тем же forward-проходом без PBR, а его текстура
#   уже нарисована с бликами (вертикальные засветки стекла в нижней полосе атласа).
#
# Запуск: run_headless.bat asda_build_gacha_glass_material.py
import os as _os
import sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))

import unreal
import asda_gacha_light_node as LN
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)

MASTER_PKG = _U.G_MATERIALS
MASTER_NAME = "M_AsdaGachaGlass"
GACHYA_ROOT = _U.G_MODELS_GACHYA
# ★★★ 0.5 — РЕШЕНИЕ ВЛАДЕЛЬЦА (подтверждено дважды: «0.5 — шары видны»). Значение 1.0 было
#   поставлено вместе с затвором `apply_behind_opaque`, который воспроизводил «непрозрачное
#   поверх прозрачного»; затвор снят как неверный (см. `asda_gacha_light_node.BEHIND_OPAQUE_GATE`),
#   и без него купол при 1.0 просто закрывает девять шаров, которых в оригинале видно.
DEFAULT_OPACITY = 0.5

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()


def log(m):
    unreal.log("[гача-стекло] " + str(m))


def connect(frm, frm_out, to, to_in):
    """★ Закон ue-material-script-gotchas: connect МОЛЧА возвращает False при неверном входе."""
    if not MEL.connect_material_expressions(frm, frm_out, to, to_in):
        raise RuntimeError("не соединилось: %s[%s] -> %s[%s]" % (frm, frm_out, to, to_in))


def build_master(save=True):
    """`save=False` — собрать граф ТОЛЬКО В ПАМЯТИ (для замера при занятом .uasset)."""
    log(LN.assert_all_unlit())          # ★ проверка по данным: освещённых подсеток у гачи нет
    path = "%s/%s" % (MASTER_PKG, MASTER_NAME)
    # ★ Пересборка НА МЕСТЕ: удалять ассет нельзя — при открытом редакторе `.uasset` занят
    #   (Error Code 32), и `create_asset` вернул бы None; заодно сохраняются родители MIC.
    mat = LN.make_or_reset_master(MASTER_PKG, MASTER_NAME)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    # ★★★ ОДНОСТОРОННИЙ. `d3dApplyMaterialRenderState@0x111DA90` ставит `CULLMODE = NONE` только
    #   при `meshType & 1`; у купола `Bowl` meshType = 0x200 — бита НЕТ, значит
    #   `gfxApplyColorWriteState@0x97ACE0` → `CULLMODE = 2 (D3DCULL_CW)`, задняя грань ОТСЕКАЕТСЯ.
    #   Пока здесь стояло `two_sided = True`, полупрозрачный купол рисовался ДВАЖДЫ: при
    #   `GlassOpacity = 0.5` итоговая непрозрачность выходила `1 − 0.5² = 0.75`, а цвет
    #   домножался сам на себя — тот самый «мутно-серый купол, шары под ним сливаются».
    mat.set_editor_property("two_sided", False)
    LN.apply_skeletal_usage(mat)                               # автомат — скелеталка с клипами

    tex = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -420, 0)
    tex.set_editor_property("parameter_name", "Tex")           # имя параметра — общее для проекта
    tex.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)

    op = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -420, 260)
    op.set_editor_property("parameter_name", "GlassOpacity")
    op.set_editor_property("default_value", DEFAULT_OPACITY)

    # Непрозрачность = Tex.A × GlassOpacity — формула оригинала (альфа текстуры × фейд объекта).
    mul = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -180, 180)
    connect(tex, "A", mul, "A")
    connect(op, "", mul, "B")

    # ★★★ ЦВЕТ = РОВНО ТЕКСТУРА. У купола `mr+0x004 & 0x8 == 0`, значит выбирается НЕосвещённый
    #   вершинный шейдер (`g_pVtxShaderC`: `mov oD0.xyz, c95.w` = 1.0), а стадия текстуры —
    #   `COLOROP = MODULATE(TEXTURE, DIFFUSE)` ⇒ `out = Tex · 1.0`. Общая точка —
    #   `asda_gacha_light_node.build_unlit_color` (там же полный вывод и защита `assert_all_unlit`).
    node, out = LN.build_unlit_color(mat, tex, "RGB")

    # ★★★ ПОРЯДОК ОРИГИНАЛА: купол рисуется ПЕРВЫМ проходом и БЕЗ записи Z, а всё непрозрачное
    #   кладётся ПОВЕРХ (полный вывод — `LN.apply_behind_opaque`). Без этого полупрозрачный купол
    #   ложился молочной плёнкой на девять шаров, которые лежат ВНУТРИ него, и на верх корпуса:
    #   замер `Reports/gacha_shadow_black.md` §3 — медианная насыщенность кадра с пятью
    #   автоматами 0.3515 с плёнкой против 0.4050 без неё.
    gate = LN.apply_behind_opaque(mat, log)
    op_final = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -40, 260)
    connect(mul, "", op_final, "A")
    connect(gate, "", op_final, "B")

    MEL.connect_material_property(node, out, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(op_final, "", unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(mat)
    if save:
        LN.save_master(mat)      # ★ падаем, если .uasset занят: молчаливый пропуск уже стоил часа
    log("мастер готов: %s (GlassOpacity=%.2f)" % (path, DEFAULT_OPACITY))
    return mat


def main():
    master = build_master()
    # Переподвязка сама по себе не нужна — её делает импортёр (asda_import_gachya_models.py);
    # но если модели уже импортированы, чиним их на месте, чтобы не гонять переимпорт ради мастера.
    fixed = 0
    for ad in AR.get_assets_by_path(unreal.Name(GACHYA_ROOT), recursive=True):
        if str(ad.asset_class_path.asset_name) != "MaterialInstanceConstant":
            continue
        name = str(ad.asset_name).lower()
        if "_blend" not in name or name.startswith("gachya_shadow"):
            continue                                            # тень — на своём мастере
        mic = EAL.load_asset(str(ad.package_name))
        if not mic:
            continue
        tex = MEL.get_material_instance_texture_parameter_value(mic, "Tex")
        mic.set_editor_property("parent", master)
        if tex:
            MEL.set_material_instance_texture_parameter_value(mic, "Tex", tex)
        # ★ Значение пишем ЯВНО: развёртка `asda_gacha_glass_sweep.py` оставляет в MIC последнее
        #   опробованное значение, и без этой строки прогон замера молча менял бы итог.
        MEL.set_material_instance_scalar_parameter_value(mic, "GlassOpacity", DEFAULT_OPACITY)
        EAL.save_loaded_asset(mic)
        fixed += 1
        log("переподвязан %s (Tex=%s)" % (str(ad.asset_name), tex.get_name() if tex else "НЕТ"))
    log("готово: MIC купола переподвязано %d" % fixed)


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
