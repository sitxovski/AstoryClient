# Copyright (c) 2026 @sitxovski (Discord)
# asda_gacha_state_dump.py — ПОЛНЫЙ структурный дамп всего, что участвует в кадре премиум-гачи.
#
# ★ Зачем. Жалоба владельца («тень серая, лутбоксы пересвечены») повторная: прошлый заход считал
#   дефекты исправленными. Значит проверять надо не гипотезу, а СОСТОЯНИЕ: настройки текстур
#   (есть ли альфа после компрессии), флаги мастеров, родителей и параметры MIC-ов, слоты мешей.
#   Дамп ничего не меняет. Запуск: run_headless.bat asda_gacha_state_dump.py  (NullRHI годится)
import unreal

EAL = unreal.EditorAssetLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()
MEL = unreal.MaterialEditingLibrary
GACHYA_ROOT = "/Game/AsdaAssets/Models/Skeletal/Gachya"
MASTERS = ["/Game/Materials/M_AsdaGachaMachine", "/Game/Materials/M_AsdaGachaGlass",
           "/Game/Materials/M_AsdaGachaShadow", "/Game/Materials/M_AsdaGachaScene3D"]


def log(m):
    unreal.log("[дамп-гача] " + str(m))


def dump_master(path):
    m = EAL.load_asset(path)
    if not m:
        log("МАСТЕР НЕТ: %s" % path)
        return
    props = ["blend_mode", "shading_model", "two_sided", "used_with_skeletal_mesh",
             "used_with_morph_targets", "used_with_static_lighting", "material_domain",
             "dither_opacity_mask", "opacity_mask_clip_value", "translucency_lighting_mode",
             "allow_translucent_custom_depth_writes"]
    vals = []
    for p in props:
        try:
            vals.append("%s=%s" % (p, m.get_editor_property(p)))
        except Exception as e:
            vals.append("%s=<нет:%s>" % (p, e))
    log("МАСТЕР %s: %s" % (path, ", ".join(vals)))
    try:
        st = MEL.get_statistics(m)
        log("   статистика: ps=%s vs=%s samplers=%s"
            % (st.get_editor_property("num_pixel_shader_instructions"),
               st.get_editor_property("num_vertex_shader_instructions"),
               st.get_editor_property("num_samplers")))
    except Exception as e:
        log("   статистика недоступна: %s" % e)
    try:
        names = MEL.get_scalar_parameter_names(m)
        log("   скаляры: %s" % ", ".join(str(n) for n in names))
        names = MEL.get_vector_parameter_names(m)
        log("   векторы: %s" % ", ".join(str(n) for n in names))
        names = MEL.get_texture_parameter_names(m)
        log("   текстуры: %s" % ", ".join(str(n) for n in names))
    except Exception as e:
        log("   параметры недоступны: %s" % e)


def dump_texture(tex):
    if not tex:
        return "НЕТ"
    try:
        w = tex.blueprint_get_size_x()
        h = tex.blueprint_get_size_y()
    except Exception:
        w = h = -1
    out = []
    for p in ("compression_settings", "srgb", "compression_no_alpha", "mip_gen_settings",
              "never_stream", "lod_group", "filter", "address_x"):
        try:
            out.append("%s=%s" % (p, tex.get_editor_property(p)))
        except Exception:
            pass
    # ★ Ключевое: есть ли РЕАЛЬНО альфа в скомпрессованном пикселформате.
    try:
        pf = tex.get_editor_property("platform_data")
    except Exception:
        pf = None
    try:
        src = tex.get_editor_property("source")
        out.append("source_format=%s" % src.get_editor_property("format") if src else "source=НЕТ")
    except Exception:
        pass
    try:
        out.append("has_alpha_channel=%s" % tex.has_alpha_channel())
    except Exception:
        pass
    return "%s %dx%d %s" % (tex.get_name(), w, h, ", ".join(out))


def main():
    for p in MASTERS:
        dump_master(p)

    mics = {}
    for ad in AR.get_assets_by_path(unreal.Name(GACHYA_ROOT), recursive=True):
        if str(ad.asset_class_path.asset_name) != "MaterialInstanceConstant":
            continue
        mic = EAL.load_asset(str(ad.package_name))
        if not mic:
            continue
        par = mic.get_editor_property("parent")
        key = par.get_name() if par else "БЕЗ РОДИТЕЛЯ"
        mics.setdefault(key, []).append(mic)
    for k, v in sorted(mics.items()):
        log("MIC на родителе %s: %d — %s" % (k, len(v), ", ".join(sorted(x.get_name() for x in v))[:400]))

    # Детально — все MIC теней и по одному MIC каждого другого родителя.
    seen_tex = {}
    for k, v in sorted(mics.items()):
        sample = [x for x in v if x.get_name().lower().startswith("gachya_shadow")] or v[:1]
        for mic in sample:
            tex = MEL.get_material_instance_texture_parameter_value(mic, "Tex")
            log("  MIC %s (родитель %s): Tex=%s" % (mic.get_name(), k, dump_texture(tex)))
            if tex:
                seen_tex[tex.get_name()] = tex
            for sp in ("ShadowOpacity", "GlassOpacity", "Ambient"):
                try:
                    val = MEL.get_material_instance_scalar_parameter_value(mic, sp)
                    log("     скаляр %s=%s" % (sp, val))
                except Exception:
                    pass

    # Слоты скелетальных мешей автоматов
    for ch in "abcde":
        path = "%s/md_gachya_%s_00/SkeletalMeshes/md_gachya_%s_00" % (GACHYA_ROOT, ch, ch)
        sk = EAL.load_asset(path)
        if not sk:
            log("МЕШ НЕТ: %s" % path)
            continue
        mats = sk.get_editor_property("materials")
        desc = []
        for i, m in enumerate(mats):
            mi = m.get_editor_property("material_interface")
            desc.append("%d:%s(%s)" % (i, mi.get_name() if mi else "ПУСТО",
                                       m.get_editor_property("material_slot_name")))
        log("МЕШ %s: слотов %d — %s" % (sk.get_name(), len(mats), ", ".join(desc)))


# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
