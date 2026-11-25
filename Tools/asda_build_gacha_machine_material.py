# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_gacha_machine_material.py — мастер КОРПУСА и ПРИЗОВОГО ШАРА автоматов премиум-гачи.
#
# ★★★ ОСВЕЩЕНИЯ НЕТ — И ЭТО НЕ УПРОЩЕНИЕ, А ОРИГИНАЛ. Полный вывод — в шапке
#   `asda_gacha_light_node.py` и в `Reports/gacha_color_fix.md`. Коротко:
#     · сцена гачи выключает `.fx`-путь (`PetWindow_RenderToTarget@0xB6D5B0`: `g_bUseEffectPath = 0`),
#       поэтому `system/EmptyProject.fx` (`tSkin`/`PShade`) к автоматам НЕ ОТНОСИТСЯ;
#     · `renderApplyMeshState@0x1116FE0` выбирает освещённый шейдер только при `mr+0x004 & 0x8`;
#       у всех 100 подсеток пяти автоматов этот бит = 0 (проверяется `LN.assert_all_unlit()`);
#     · неосвещённый шейдер даёт `mov oD0.xyz, c95.w` = 1.0, а стадия текстуры —
#       `COLOROP=MODULATE, ARG1=TEXTURE, ARG2=DIFFUSE` ⇒ **out = Tex · 1.0**.
#   ⇒ `Emissive = Tex`, Unlit. Прежний плоский множитель ×0.6 съедал 40 % яркости и всю
#     светотень — это и была жалоба «бледные, вымытые, без контраста».
#
# ★ БЛЕНД. Все текстуры этих слотов непрозрачны: замер по `RawAssets/Textures/Data/Character/Texture`
#   — у `md_gachya_{a..e}_00` и `ball_ani_{01..05}` альфа = 255 у 100 % пикселей. Поэтому
#   `BLEND_OPAQUE`, без бесполезного альфа-теста. Полупрозрачные подсетки (купол `Bowl`, квады
#   теней) идут своими мастерами `M_AsdaGachaGlass` / `M_AsdaGachaShadow`.
#
# ★ TWO-SIDED. `d3dApplyMaterialRenderState@0x111DA90` ставит `CULLMODE = NONE` при `meshType & 1`.
#   У автомата такая ровно ОДНА подсетка — корпус (`meshType = 0x201`); остальные непрозрачные
#   идут с `CULLMODE = CW`. Мастер один на все непрозрачные слоты, поэтому он двусторонний: для
#   НЕПРОЗРАЧНОЙ геометрии лишняя задняя грань невидима (перекрыта Z-буфером) и кадр не меняет,
#   а без флага у корпуса появились бы дыры. У полупрозрачных мастеров так делать НЕЛЬЗЯ —
#   там двусторонность удваивает блендинг (см. `asda_build_gacha_glass_material.py`).
#
# Запуск: run_headless.bat asda_build_gacha_machine_material.py
import os as _os
import sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))

import unreal
import asda_gacha_light_node as LN

MASTER_PKG = "/Game/Materials"
MASTER_NAME = "M_AsdaGachaMachine"

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary


def log(m):
    unreal.log("[гача-корпус] " + str(m))


def build_master(save=True):
    """`save=False` — собрать граф ТОЛЬКО В ПАМЯТИ (для замера при занятом .uasset)."""
    # ★ Сначала — проверка по данным, что неосвещённый путь действительно верен для ВСЕХ подсеток.
    log(LN.assert_all_unlit())

    path = "%s/%s" % (MASTER_PKG, MASTER_NAME)
    # ★ Пересборка НА МЕСТЕ: удалять ассет нельзя — при открытом редакторе `.uasset` занят
    #   (Error Code 32), и `create_asset` вернул бы None; заодно сохраняются родители MIC.
    mat = LN.make_or_reset_master(MASTER_PKG, MASTER_NAME)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    # Unlit: движковый свет сцене не нужен — у оригинала множитель к альбедо тождественно 1.0.
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    LN.apply_skeletal_usage(mat)

    tex = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -400, 0)
    tex.set_editor_property("parameter_name", "Tex")     # имя параметра — общее для проекта
    tex.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)

    node, out = LN.build_unlit_color(mat, tex, "RGB")
    MEL.connect_material_property(node, out, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    if save:
        LN.save_master(mat)      # ★ падаем, если .uasset занят: молчаливый пропуск уже стоил часа
    log("мастер готов: %s (Unlit, Opaque, множитель к текстуре ≡ 1.0 — как у оригинала)" % path)
    return mat


def main():
    build_master()


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
