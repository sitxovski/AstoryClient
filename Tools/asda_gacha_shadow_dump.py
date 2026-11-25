# Copyright (c) 2026 @sitxovski (Discord)
# asda_gacha_shadow_dump.py — ПОЛНЫЙ СТРУКТУРНЫЙ ДАМП материалов/текстур сцены премиум-гачи.
#
# ★ ЗАЧЕМ. Замер по кадру `cc_final_mach_*` дал у квадов тени НЕПРОЗРАЧНОСТЬ максимум 0.349,
#   тогда как байты текстуры `gachya_shadow` дают в ядре пятна alpha = 204/255 = 0.800.
#   Расхождение вдвое ⇒ где-то по дороге альфа теряется. Дамп печатает ВСЁ, что на неё влияет:
#   родителя каждого MIC, ВСЕ его оверрайды (скаляры/векторы/текстуры), настройки самой текстуры
#   (srgb, сжатие, мипы, фильтр) и фактические байты альфы, прочитанные из ассета UE.
#
# Ничего не меняет — только читает. Запуск: run_headless.bat asda_gacha_shadow_dump.py
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()

GACHYA_ROOT = "/Game/AsdaAssets/Models/Skeletal/Gachya"
MASTERS = ["/Game/Materials/M_AsdaGachaMachine",
           "/Game/Materials/M_AsdaGachaGlass",
           "/Game/Materials/M_AsdaGachaShadow",
           "/Game/Materials/M_AsdaGachaScene3D"]


def log(m):
    unreal.log("[гача-дамп] " + str(m))


def dump_texture(tex, tag):
    if not tex:
        log("  %s: текстуры НЕТ" % tag)
        return
    log("  %s: %s" % (tag, tex.get_path_name()))
    for p in ("srgb", "compression_settings", "mip_gen_settings", "filter", "lod_group",
              "compression_no_alpha", "never_stream", "lod_bias", "compression_quality"):
        try:
            log("      %-22s = %s" % (p, tex.get_editor_property(p)))
        except Exception as e:
            log("      %-22s = <нет: %s>" % (p, e))
    try:
        src = tex.get_editor_property("source")
        log("      source                 = %dx%d, формат %s"
            % (src.get_size_x(), src.get_size_y(), src.get_format()))
    except Exception as e:
        log("      source                 = <нет: %s>" % e)
    # ★ Фактические байты: mip0 из платформенных данных читаем через экспорт в PNG нельзя
    #   (headless без RHI), поэтому берём SourceData — это ровно то, что импортировалось.
    try:
        data = tex.get_editor_property("source").get_mip_data(0)
        b = bytes(data)
        n = len(b)
        # BGRA8 — стандартный порядок TextureSource для PNG RGBA
        alphas = b[3::4]
        if alphas:
            hist = {}
            for v in alphas:
                hist[v] = hist.get(v, 0) + 1
            top = sorted(hist.items(), key=lambda kv: -kv[1])[:5]
            log("      SOURCE mip0: %d байт, альфа min=%d max=%d сред=%.2f, топ-значения %s"
                % (n, min(alphas), max(alphas), sum(alphas) / float(len(alphas)),
                   ", ".join("%d×%d" % (v, c) for v, c in top)))
    except Exception as e:
        log("      SOURCE mip0            = <не прочиталось: %s>" % e)


def dump_mi(mi):
    log("MIC %s" % mi.get_path_name())
    par = mi.get_editor_property("parent")
    log("  родитель: %s" % (par.get_path_name() if par else "НЕТ"))
    for grp, getter in (("скаляр", "scalar_parameter_values"),
                        ("вектор", "vector_parameter_values"),
                        ("текстура", "texture_parameter_values")):
        try:
            vals = mi.get_editor_property(getter)
        except Exception:
            continue
        for v in vals:
            info = v.get_editor_property("parameter_info")
            log("  ОВЕРРАЙД %s %-16s = %s"
                % (grp, info.get_editor_property("name"), v.get_editor_property("parameter_value")))
    tex = MEL.get_material_instance_texture_parameter_value(mi, "Tex")
    dump_texture(tex, "Tex")


def dump_master(path):
    m = EAL.load_asset(path)
    if not m:
        log("МАСТЕР %s — НЕ НАЙДЕН" % path)
        return
    log("МАСТЕР %s" % path)
    for p in ("blend_mode", "shading_model", "two_sided", "material_domain",
              "translucency_pass", "dither_opacity_mask", "used_with_skeletal_mesh",
              "allow_translucent_custom_depth_writes", "disable_depth_test",
              "translucent_shadow_density_scale"):
        try:
            log("  %-38s = %s" % (p, m.get_editor_property(p)))
        except Exception as e:
            log("  %-38s = <нет: %s>" % (p, e))
    try:
        names = unreal.MaterialEditingLibrary.get_scalar_parameter_names(m)
        for n in names:
            log("  скаляр %-20s = %s" % (n, MEL.get_material_default_scalar_parameter_value(m, n)))
    except Exception as e:
        log("  скаляры: <%s>" % e)
    try:
        st = MEL.get_statistics(m)
        log("  статистика: ps=%s vs=%s samplers=%s"
            % (st.get_editor_property("num_pixel_shader_instructions"),
               st.get_editor_property("num_vertex_shader_instructions"),
               st.get_editor_property("num_samplers")))
    except Exception:
        pass


def main():
    log("=" * 78)
    for p in MASTERS:
        dump_master(p)
        log("-" * 78)
    seen = set()
    for ad in AR.get_assets_by_path(unreal.Name(GACHYA_ROOT), recursive=True):
        if str(ad.asset_class_path.asset_name) != "MaterialInstanceConstant":
            continue
        pn = str(ad.package_name)
        if pn in seen:
            continue
        seen.add(pn)
        mi = EAL.load_asset(pn)
        if mi:
            dump_mi(mi)
            log("-" * 78)
    log("=== дамп закончен: MIC %d ===" % len(seen))


if __name__ == "__main__":
    main()
