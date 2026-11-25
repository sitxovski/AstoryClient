# Copyright (c) 2026 @sitxovski (Discord)
# M_UI_CooldownReadyIcon — UI-материал значка «откат навыка истёк» (S→C 5274, `UAsdaCooldownReadyIconSubsystem`, 21.10.2026).
#   Эталон перед квадом значка ставит фильтры сэмплера MIN/MAG = LINEAR (`MiniMap_RenderBigMap@0xB4FCC0`, разбор
#   `Reports/skill_cooldown_ready_icon/01_etalon.md` §2.5), а текстуры иконок JUI импортированы с фильтром NEAREST (1:1 для
#   окон). Значок растягивает иконку 32 → 256 px, поэтому нужен свой сэмплер: «общий, по настройкам группы мира» — линейный
#   независимо от фильтра самой текстуры.
#   Граф: Tex (TextureSampleParameter2D, Clamp по группе мира) × VertexColor (цвет и альфа кисти Slate):
#     Final Color = Tex.RGB · VC.RGB;  Opacity = Tex.A · VC.A;  домен UI, бленд Translucent.
#   Запуск (headless, без окон): python Tools/run_headless_safe.py Tools/asda_make_cooldown_icon_material.py --log <лог>
#     --marker "ЗАВЕРШЕНО: материал значка" --expect-asset /Game/AsdaAssets/UI/Materials/M_UI_CooldownReadyIcon
#   Существующий ассет НЕ пересоздаётся (правки владельца не теряются); пересоздать — ключ окружения ASDA_FORCE_REBUILD=1.
import os
import unreal

PKG = "/Game/AsdaAssets/UI/Materials"
NAME = "M_UI_CooldownReadyIcon"
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary


def main():
    """Работа скрипта (импорт модуля ничего не делает — закон tools-no-import-side-effects)."""
    path = "%s/%s" % (PKG, NAME)
    if EAL.does_asset_exist(path):
        if os.environ.get("ASDA_FORCE_REBUILD") != "1":
            unreal.log("[cd_icon_mat] %s уже есть — не трогаю (ASDA_FORCE_REBUILD=1 для пересоздания)" % path)
            unreal.log("ЗАВЕРШЕНО: материал значка (без изменений)")
            return
        EAL.delete_asset(path)
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, PKG, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_UI)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)

    tex = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -600, 0)
    tex.set_editor_property("parameter_name", "Tex")
    tex.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    # Сэмплер «общий, по настройкам группы мира»: фильтр берётся из группы (линейный), а не из NEAREST самой текстуры.
    tex.set_editor_property("sampler_source", unreal.SamplerSourceMode.SSM_CLAMP_WORLD_GROUP_SETTINGS)
    deftex = unreal.load_asset("/Engine/EngineResources/DefaultTexture")
    if deftex:
        tex.set_editor_property("texture", deftex)

    vc = MEL.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -600, 260)
    rgb = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, 0)
    MEL.connect_material_expressions(tex, "RGB", rgb, "A")
    MEL.connect_material_expressions(vc, "", rgb, "B")
    alpha = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, 200)
    MEL.connect_material_expressions(tex, "A", alpha, "A")
    MEL.connect_material_expressions(vc, "A", alpha, "B")
    MEL.connect_material_property(rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(mat)
    EAL.save_asset(path)
    unreal.log("[cd_icon_mat] %s создан" % path)
    unreal.log("ЗАВЕРШЕНО: материал значка")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
