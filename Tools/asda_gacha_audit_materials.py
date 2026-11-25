# Copyright (c) 2026 @sitxovski (Discord)
# asda_gacha_audit_materials.py — ПОЛНЫЙ структурный разбор материалов и текстур автоматов гачи.
#
# Зачем: жалоба владельца «много серого, будто без текстуры». Закон проекта
# [[full-structural-analysis-always]] — до любой правки выписать КАЖДЫЙ элемент: слот меша,
# назначенный материал, его мастер, все параметры, текстуру в слоте и НАСТРОЙКИ этой текстуры
# (размер, mip-gen, LOD-группа, LOD-bias, фильтр, сжатие, sRGB) — то есть всё, что даёт и
# «серое», и «мыльное».
#
# Эталон, с чем сверяем (истина = данные оригинала, не догадки):
#   · пер-меш текстура берётся из .jbm: matTable + matIdx@0x1f4 (см. [[npc-texture-source]]);
#     у автоматов ровно три текстуры на модель — md_Gachya_<x>_00, ball_ani_0<N>, gachya_shadow;
#   · `PetWindow_RenderToTarget@0xB6D5B0` (npc_mob_pet.c:56-181) снимает сцену в RT размером
#     бэкбуфера — то есть 1 пиксель кадра = 1 пиксель экрана, размытия быть не должно.
#
# Только ЧТЕНИЕ, ничего не меняет. Запуск: run_headless.bat asda_gacha_audit_materials.py
import unreal

SKEL_ROOT = "/Game/AsdaAssets/Models/Skeletal/Gachya"
MODELS = ["md_gachya_%s_00" % c for c in "abcde"]
MASTERS = ["/Game/Materials/M_AsdaObjectLit", "/Game/Materials/M_AsdaObject",
           "/Game/Materials/M_AsdaGachaShadow"]

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()


def log(m):
    unreal.log("[гача-аудит] " + str(m))


def tex_info(t):
    """Всё, что влияет на резкость текстуры в кадре."""
    if not t:
        return "НЕТ"
    def prop(n, d="?"):
        try:
            return t.get_editor_property(n)
        except Exception:
            return d
    src = prop("source")
    try:
        sw, sh = src.get_size_x(), src.get_size_y()
    except Exception:
        sw = sh = -1
    return ("%s исходник=%dx%d импорт=%dx%d mip=%s LOD-группа=%s lod_bias=%s фильтр=%s "
            "сжатие=%s sRGB=%s never_stream=%s maxsize=%s" % (
                t.get_name(), sw, sh, t.blueprint_get_size_x(), t.blueprint_get_size_y(),
                prop("mip_gen_settings"), prop("lod_group"), prop("lod_bias"), prop("filter"),
                prop("compression_settings"), prop("srgb"), prop("never_stream"),
                prop("max_texture_size")))


def dump_master(path):
    m = EAL.load_asset(path)
    if not m:
        log("мастер %s — НЕТ" % path)
        return
    def names(fn):
        try:
            return [str(x) for x in fn(m)]
        except Exception as e:
            return ["<%s>" % e]
    log("мастер %s: домен=%s модель=%s блендмод=%s two_sided=%s непрозр.маска=%s" % (
        path, m.get_editor_property("material_domain"), m.get_editor_property("shading_model"),
        m.get_editor_property("blend_mode"), m.get_editor_property("two_sided"),
        m.get_editor_property("opacity_mask_clip_value")))
    log("   параметры: тексты=%s скаляры=%s векторы=%s" % (
        names(MEL.get_texture_parameter_names), names(MEL.get_scalar_parameter_names),
        names(MEL.get_vector_parameter_names)))


def main():
    for p in MASTERS:
        dump_master(p)

    seen_tex = {}
    for name in MODELS:
        mesh = EAL.load_asset("%s/%s/SkeletalMeshes/%s" % (SKEL_ROOT, name, name))
        if not mesh:
            log("★ НЕТ меша %s" % name)
            continue
        mats = mesh.get_editor_property("materials")
        # число секций LOD0 — сколько кусков реально рисуется
        try:
            n_sec = len(mesh.get_editor_property("lod_info")[0].get_editor_property("lod_material_map"))
        except Exception:
            n_sec = -1
        log("=== %s: слотов=%d, карта секций LOD0=%d, UV-каналов=%s" % (
            name, len(mats), n_sec, mesh.get_editor_property("num_uv_sets") if hasattr(mesh, "num_uv_sets") else "?"))
        for i, sm in enumerate(mats):
            mi = sm.get_editor_property("material_interface")
            slot = sm.get_editor_property("material_slot_name")
            if not mi:
                log("  слот %2d '%s' = ★ ПУСТО" % (i, slot))
                continue
            parent = mi.get_editor_property("parent") if isinstance(mi, unreal.MaterialInstanceConstant) else None
            log("  слот %2d '%s' = %s (%s) мастер=%s" % (
                i, slot, mi.get_name(), type(mi).__name__, parent.get_name() if parent else "—"))
            if isinstance(mi, unreal.MaterialInstanceConstant):
                for v in mi.get_editor_property("texture_parameter_values"):
                    pn = v.get_editor_property("parameter_info").get_editor_property("name")
                    t = v.get_editor_property("parameter_value")
                    log("        текстура %s -> %s" % (pn, tex_info(t)))
                    if t:
                        seen_tex[t.get_path_name()] = t
                for v in mi.get_editor_property("scalar_parameter_values"):
                    log("        скаляр %s = %s" % (
                        v.get_editor_property("parameter_info").get_editor_property("name"),
                        v.get_editor_property("parameter_value")))
                for v in mi.get_editor_property("vector_parameter_values"):
                    log("        вектор %s = %s" % (
                        v.get_editor_property("parameter_info").get_editor_property("name"),
                        v.get_editor_property("parameter_value")))

        # ★ Все MIC модели, а не только те, что попали на слоты: импортёр правит ВСЕ, а движок
        #   рисует только слотовые — расхождение здесь = «правил не то, что видно».
        mics = [str(a.asset_name) for a in AR.get_assets_by_path(
            unreal.Name("%s/%s" % (SKEL_ROOT, name)), recursive=True)
            if str(a.asset_class_path.asset_name) == "MaterialInstanceConstant"]
        log("  MIC в папке модели (%d): %s" % (len(mics), ", ".join(sorted(mics))))

    log("=== текстуры, реально попавшие в слоты (%d) ===" % len(seen_tex))
    for p in sorted(seen_tex):
        log("  " + tex_info(seen_tex[p]))


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
