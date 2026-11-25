# Copyright (c) 2026 @sitxovski (Discord)
# asda_nb_build_mats.py — ШАГ 3 превью «нормаль из альбедо»: импорт карт + материалы предпросмотра.
#
# ★ НИЧЕГО ГЛОБАЛЬНОГО. Мастера проекта (M_AsdaObjectLit / M_AsdaCharLit) НЕ трогаются.
#   Всё живёт в /Game/_PreviewNB/ и после показа удаляется одной папкой.
#
# ★ ЧЕСТНОСТЬ СРАВНЕНИЯ: «до» и «после» — это ДВА ИНСТАНСА ОДНОГО материала, а не два разных
#   материала. Отличаются ровно двумя числами:
#       Flatness   = 1 → нормаль принудительно плоская (0,0,1)  ← нынешний вид проекта
#       UseRoughMap= 0 → шероховатость берётся из константы 0.9  ← нынешний вид проекта
#   Всё остальное (альбедо, вершинный цвет, маска прозрачности, свет, камера) идентично.
#
# Запуск: run_headless.bat asda_nb_build_mats.py
import os

SRC_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Tools/_out/nb_preview"
PKG = "/Game/_PreviewNB"
ROUGH_CONST = 0.9   # ровно то, что стоит в мастерах проекта (asda_build_materials_lit.py, ROUGH)

# ключ, подпись, меш, альбедо-текстура проекта, тип материала («char» = непрозрачный со спек-маской)
SAMPLES = [
    ("stone", "Камень (статуя)",
     "/Game/AsdaAssets/Models/NPC/mdel_stoneman-a_00/SkeletalMeshes/mdel_stoneman-a_00",
     "/Game/AsdaAssets/Textures/Maps/rein/Texture/MDEL_STONEMAN-A_00", "obj"),
    ("build", "Кладка и дерево (дом)",
     "/Game/AsdaAssets/Models/NPC/mdel_spabuild-a_00/SkeletalMeshes/mdel_spabuild-a_00",
     "/Game/AsdaAssets/Textures/Maps/rein/Texture/MDEL_SPABUILD-B_00", "obj"),
    ("tree", "Кора и листва (дерево)",
     "/Game/AsdaAssets/Models/NPC/mdel_oldtree-a_00/SkeletalMeshes/mdel_oldtree-a_00",
     "/Game/AsdaAssets/Textures/Maps/nightvalley/Texture/MDEL_OLDTREE-A_00", "obj"),
    ("armor", "Ткань и броня (экип)",
     "/Game/AsdaAssets/Models/Char/ch1_f_00_chst103_00_00_0/StaticMeshes/ch1_f_00_chst103_00_00_0",
     "/Game/AsdaAssets/Textures/Characters/Texture/ch1_f_00_chst103_00_00_0", "char"),
    ("mob", "Шкура моба (голем)",
     "/Game/AsdaAssets/Models/Mob/mon_b_3_firegolem-a_00/StaticMeshes/mon_b_3_firegolem-a_00",
     "/Game/AsdaAssets/Textures/Mobs/Texture/mon_b_3_firegolem-a_00", "mob"),
]


def import_tex(unreal, png, dest_dir, name, is_normal):
    """Импорт PNG как текстуры. Нормаль — TC_NORMALMAP + sRGB off, шероховатость — серая линейная."""
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", png)
    task.set_editor_property("destination_path", dest_dir)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    task.set_editor_property("factory", unreal.TextureFactory())
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    paths = task.get_editor_property("imported_object_paths")
    if not paths:
        unreal.log_error("импорт не прошёл: %s" % png)
        return None
    tex = unreal.EditorAssetLibrary.load_asset(paths[0])
    if tex:
        if is_normal:
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
            tex.set_editor_property("srgb", False)
            tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP)
        else:
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
            tex.set_editor_property("srgb", False)
        unreal.EditorAssetLibrary.save_loaded_asset(tex)
    return tex


def build_master(unreal, name, kind):
    """Мастер предпросмотра. kind: 'obj' — маска прозрачности, 'char'/'mob' — непрозрачный."""
    mel = unreal.MaterialEditingLibrary
    atools = unreal.AssetToolsHelpers.get_asset_tools()
    path = "%s/%s" % (PKG, name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    m = atools.create_asset(name, PKG, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("used_with_skeletal_mesh", True)
    if kind == "obj":
        m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        m.set_editor_property("opacity_mask_clip_value", 0.5)

    def E(cls, x, y):
        return mel.create_material_expression(m, cls, x, y)

    def C(a, ao, b, bi):
        if not mel.connect_material_expressions(a, ao, b, bi):
            unreal.log_error("НЕ соединилось: %s.%s -> %s" % (a, ao, bi))

    def P(e, eo, prop):
        if not mel.connect_material_property(e, eo, prop):
            unreal.log_error("НЕ подключилось к %s" % prop)

    def texp(nm, x, y, stype=None):
        t = E(unreal.MaterialExpressionTextureSampleParameter2D, x, y)
        t.set_editor_property("parameter_name", nm)
        if stype is not None:
            t.set_editor_property("sampler_type", stype)
        return t

    def scal(nm, v, x, y):
        s = E(unreal.MaterialExpressionScalarParameter, x, y)
        s.set_editor_property("parameter_name", nm)
        s.set_editor_property("default_value", v)
        return s

    tex = texp("Tex", -900, -200)
    texn = texp("TexN", -900, 120, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    # ⚠ Имя энума — американское написание: SAMPLERTYPE_LINEAR_GRAYSCALE (C++ SAMPLERTYPE_LinearGrayscale).
    texr = texp("TexR", -900, 420, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)

    # BaseColor = альбедо × вершинный цвет — как в мастерах проекта
    vc = E(unreal.MaterialExpressionVertexColor, -900, -450)
    bc = E(unreal.MaterialExpressionMultiply, -500, -300)
    C(tex, "RGB", bc, "A")
    # ⚠ У VertexColor выход именуется НЕ "RGB": connect по этому имени молча возвращает False
    #   (та же грабля, что записана в ue-material-script-gotchas). Берём первый выход — "".
    C(vc, "", bc, "B")
    P(bc, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # Нормаль: lerp(из карты → плоская) по Flatness. Flatness=1 ⇒ ровно нынешний вид.
    flat = scal("Flatness", 0.0, -900, 300)
    flatv = E(unreal.MaterialExpressionConstant3Vector, -700, 250)
    flatv.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 1.0, 1.0))
    nlerp = E(unreal.MaterialExpressionLinearInterpolate, -350, 150)
    C(texn, "RGB", nlerp, "A")
    C(flatv, "", nlerp, "B")
    C(flat, "", nlerp, "Alpha")
    P(nlerp, "", unreal.MaterialProperty.MP_NORMAL)

    # Шероховатость: lerp(константа 0.9 → карта) по UseRoughMap. 0 ⇒ ровно нынешний вид.
    rconst = scal("RoughConst", ROUGH_CONST, -900, 560)
    useR = scal("UseRoughMap", 1.0, -900, 660)
    rlerp = E(unreal.MaterialExpressionLinearInterpolate, -350, 450)
    C(rconst, "", rlerp, "A")
    C(texr, "R", rlerp, "B")
    C(useR, "", rlerp, "Alpha")
    P(rlerp, "", unreal.MaterialProperty.MP_ROUGHNESS)

    metal = scal("Metallic", 0.0, -900, 760)
    P(metal, "", unreal.MaterialProperty.MP_METALLIC)

    if kind == "obj":
        P(tex, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
    else:
        # ★ Эталон EmptyProject.fx (техники tSkin/tRigid): альфа текстуры персонажа — МАСКА БЛИКА,
        #   а не прозрачность. Мастер проекта M_AsdaCharLit делает так же.
        sspec = scal("SpecScale", 0.5, -900, 860)
        smul = E(unreal.MaterialExpressionMultiply, -350, 700)
        C(tex, "A", smul, "A")
        C(sspec, "", smul, "B")
        P(smul, "", unreal.MaterialProperty.MP_SPECULAR)

    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    return m


def make_mic(unreal, master, name, tex, texn, texr, flat, use_r):
    mel = unreal.MaterialEditingLibrary
    atools = unreal.AssetToolsHelpers.get_asset_tools()
    path = "%s/%s" % (PKG, name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    mic = atools.create_asset(name, PKG, unreal.MaterialInstanceConstant,
                              unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mic, master)
    mel.set_material_instance_texture_parameter_value(mic, "Tex", tex)
    if texn:
        mel.set_material_instance_texture_parameter_value(mic, "TexN", texn)
    if texr:
        mel.set_material_instance_texture_parameter_value(mic, "TexR", texr)
    mel.set_material_instance_scalar_parameter_value(mic, "Flatness", flat)
    mel.set_material_instance_scalar_parameter_value(mic, "UseRoughMap", use_r)
    unreal.EditorAssetLibrary.save_loaded_asset(mic)
    return mic


def main():
    import unreal
    eal = unreal.EditorAssetLibrary
    if not eal.does_directory_exist(PKG):
        eal.make_directory(PKG)
    masters = {
        "obj": build_master(unreal, "M_NB_Object", "obj"),
        "char": build_master(unreal, "M_NB_Char", "char"),
    }
    masters["mob"] = masters["char"]   # моб тоже непрозрачный со спек-маской

    made = 0
    for key, _label, _mesh, tex_path, kind in SAMPLES:
        tex = eal.load_asset(tex_path)
        if not tex:
            unreal.log_error("нет альбедо %s" % tex_path)
            continue
        texn = import_tex(unreal, os.path.join(SRC_DIR, "%s_N.png" % key), PKG, "T_%s_N" % key, True)
        texr = import_tex(unreal, os.path.join(SRC_DIR, "%s_R.png" % key), PKG, "T_%s_R" % key, False)
        if not texn or not texr:
            continue
        m = masters[kind]
        make_mic(unreal, m, "MI_%s_before" % key, tex, texn, texr, 1.0, 0.0)
        make_mic(unreal, m, "MI_%s_after" % key, tex, texn, texr, 0.0, 1.0)
        made += 1
        unreal.log("готово: %s" % key)
    unreal.log("=== nb_build_mats: %d / %d ===" % (made, len(SAMPLES)))


if __name__ == "__main__":
    main()
