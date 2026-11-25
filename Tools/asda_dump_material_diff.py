# Copyright (c) 2026 @sitxovski (Discord)
# asda_dump_material_diff.py — ПОЛНЫЙ дифф двух мастер-материалов «параметр в параметр».
#   Дампит ВСЕ editor-properties UMaterial (через рефлексию python-обёртки) + все подключённые
#   входы MaterialProperty (BaseColor/Metallic/Specular/Roughness/Normal/Emissive/Opacity/AO/...)
#   + список скалярных/текстурных параметров. Пишет текстовый отчёт и JSON.
#   Запуск: run_headless.bat asda_dump_material_diff.py  [ИмяA ИмяB]
import json, os, sys
import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

args = [a for a in sys.argv if not a.lower().endswith(".py")]
NAMES = args[:2] if len(args) >= 2 else ["M_AsdaObjectLit", "M_AsdaCharLit"]
OUT_TXT = r"@@ASTORY_DEV_ROOT@@\Reports\material_master_diff.txt"
OUT_JSON = r"@@ASTORY_DEV_ROOT@@\Reports\material_master_diff.json"

# MaterialProperty-входы (ТОЛЬКО реальные пины; MP_MATERIAL_ATTRIBUTES/MP_CUSTOM_OUTPUT/MP_MAX
#   роняют UnrealEditor-MaterialEditor.dll при запросе входа — крэш не ловится try/except)
PROPS = ["MP_BASE_COLOR", "MP_METALLIC", "MP_SPECULAR", "MP_ROUGHNESS", "MP_ANISOTROPY",
         "MP_EMISSIVE_COLOR", "MP_OPACITY", "MP_OPACITY_MASK", "MP_NORMAL", "MP_TANGENT",
         "MP_WORLD_POSITION_OFFSET", "MP_SUBSURFACE_COLOR", "MP_AMBIENT_OCCLUSION",
         "MP_REFRACTION", "MP_PIXEL_DEPTH_OFFSET", "MP_SHADING_MODEL",
         "MP_CUSTOM_DATA0", "MP_CUSTOM_DATA1"]
PROPS = [p for p in PROPS if hasattr(unreal.MaterialProperty, p)]

# имена editor-property UMaterial. Рефлексия по dir() в UE-обёртке даёт getset_descriptor, а не
#   property → фильтр по isinstance(property) пропускал ВСЁ. Берём оба источника + явный список
#   светотеневых/теневых свойств (их отсутствие в дампе = ложный вывод «различий нет»).
_EXPLICIT = [
    "blend_mode", "shading_model", "two_sided", "opacity_mask_clip_value", "dithered_lod_transition",
    "cast_dynamic_shadow_as_masked", "used_with_skeletal_mesh", "used_with_static_lighting",
    "used_with_instanced_static_meshes", "used_with_particle_sprites", "used_with_morph_targets",
    "used_with_splines", "used_with_nanite", "used_with_geometry_cache", "used_with_water",
    "used_with_hair_strands", "used_with_virtual_heightfield_mesh", "used_with_editor_composting",
    "allow_negative_emissive_color", "num_customized_u_vs", "wireframe",
    "translucency_lighting_mode", "translucency_directional_lighting_intensity",
    "translucent_shadow_density_scale", "translucent_self_shadow_density_scale",
    "translucent_self_shadow_second_density_scale", "translucent_self_shadow_second_opacity",
    "translucent_backscattering_exponent", "translucent_multiple_scattering_extinction",
    "translucent_shadow_start_offset", "translucency_pass",
    "allow_translucent_custom_depth_writes", "compute_fresnel_angle_based_on_normal",
    "disable_depth_test", "write_only_alpha", "output_translucent_velocity",
    "screen_space_reflections", "contact_shadows", "material_domain", "refraction_mode",
    "refraction_depth_bias", "fully_rough", "use_full_precision", "use_lightmap_directionality",
    "forward_blend_shadows", "shading_rate", "is_thin_surface", "b_enable_stencil_test",
    "b_cast_dynamic_shadow_as_masked", "b_used_with_skeletal_mesh", "b_two_sided",
    "b_screen_space_reflections", "b_contact_shadows", "b_tangent_space_normal",
    "two_sided_separate_pass", "material_decal_response", "b_used_with_nanite",
]


def dump(name):
    path = "/Game/Materials/" + name
    m = EAL.load_asset(path)
    if not m:
        return {"__error__": "нет ассета %s" % path}
    d = {"__path__": path}
    props = {}
    for pn in PROP_NAMES:
        try:
            v = m.get_editor_property(pn)
        except Exception:
            continue          # не UPROPERTY (метод/мусор из dir()) — в дифф не попадает
        try:
            props[pn] = str(v)
        except Exception:
            props[pn] = "<не строкуется>"
    d["properties"] = props
    # подключённые входы
    inputs = {}
    for pname in PROPS:
        try:
            mp = getattr(unreal.MaterialProperty, pname)
            node = MEL.get_material_property_input_node(m, mp)
        except Exception:
            continue
        if node:
            try:
                out = str(MEL.get_material_property_input_node_output_name(m, mp))
            except Exception:
                out = ""
            desc = node.get_class().get_name()
            extra = ""
            for attr in ("parameter_name", "default_value", "constant"):
                try:
                    extra += " %s=%s" % (attr, node.get_editor_property(attr))
                except Exception:
                    pass
            inputs[pname] = "%s%s%s" % (desc, (" out=%s" % out) if out else "", extra)
    d["inputs"] = inputs
    # параметры
    try:
        d["scalar_params"] = {str(p): MEL.get_material_default_scalar_parameter_value(m, p)
                              for p in MEL.get_scalar_parameter_names(m)}
    except Exception as e:
        d["scalar_params"] = {"__error__": str(e)}
    try:
        d["texture_params"] = [str(p) for p in MEL.get_texture_parameter_names(m)]
    except Exception as e:
        d["texture_params"] = [str(e)]
    try:
        d["vector_params"] = [str(p) for p in MEL.get_vector_parameter_names(m)]
    except Exception:
        d["vector_params"] = []
    # все ноды графа (для полноты картины)
    try:
        d["expressions"] = sorted(set(e.get_class().get_name() for e in m.get_editor_property("expression_collection").expressions))
    except Exception:
        try:
            d["expressions"] = sorted(set(e.get_class().get_name() for e in m.get_editor_property("expressions")))
        except Exception as e:
            d["expressions"] = ["<%s>" % e]
    return d


A, B = NAMES[0], NAMES[1]

lines = ["=== ДИФФ МАСТЕР-МАТЕРИАЛОВ: %s  vs  %s ===" % (A, B), ""]

diff_props = []


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global PROP_NAMES
    _REFL = [n for n in dir(unreal.Material) if not n.startswith("_")]
    PROP_NAMES = sorted(set(_EXPLICIT) | set(_REFL))
    da, db = dump(A), dump(B)
    lines.append("--- 1. editor-properties (только РАЗЛИЧИЯ) ---")
    pa, pb = da.get("properties", {}), db.get("properties", {})
    for k in sorted(set(pa) | set(pb)):
        va, vb = pa.get(k, "<нет>"), pb.get(k, "<нет>")
        if va != vb:
            diff_props.append((k, va, vb))
            lines.append("  %-46s %s  ->  %s" % (k, va, vb))
    if not diff_props:
        lines.append("  (различий нет)")
    lines.append("")
    lines.append("--- 2. подключённые входы MaterialProperty ---")
    ia, ib = da.get("inputs", {}), db.get("inputs", {})
    for k in sorted(set(ia) | set(ib)):
        va, vb = ia.get(k, "<НЕ ПОДКЛЮЧЁН (дефолт UE)>"), ib.get(k, "<НЕ ПОДКЛЮЧЁН (дефолт UE)>")
        mark = "   " if va == vb else "★  "
        lines.append("%s%-22s A: %s" % (mark, k, va))
        lines.append("%s%-22s B: %s" % ("   ", "", vb))
    lines.append("")
    lines.append("--- 3. скалярные параметры ---")
    lines.append("  A(%s): %s" % (A, da.get("scalar_params")))
    lines.append("  B(%s): %s" % (B, db.get("scalar_params")))
    lines.append("--- 4. текстурные параметры ---")
    lines.append("  A: %s   B: %s" % (da.get("texture_params"), db.get("texture_params")))
    lines.append("--- 5. ноды графа (типы) ---")
    lines.append("  A: %s" % da.get("expressions"))
    lines.append("  B: %s" % db.get("expressions"))
    txt = "\n".join(lines)
    os.makedirs(os.path.dirname(OUT_TXT), exist_ok=True)
    with open(OUT_TXT, "w", encoding="utf-8") as f:
        f.write(txt + "\n")
    with open(OUT_JSON, "w", encoding="utf-8") as f:
        json.dump({A: da, B: db}, f, ensure_ascii=False, indent=1)
    for l in lines:
        unreal.log_warning("[matdiff] %s" % l)
    unreal.log_warning("[matdiff] отчёт: %s" % OUT_TXT)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
