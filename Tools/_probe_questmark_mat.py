# Copyright (c) 2026 @sitxovski (Discord)
# _probe_questmark_mat.py — структурный дамп материалов квестовых меток и мастеров M_AsdaObject/Lit.
#   Нужен ПЕРЕД правкой (закон «полный структурный разбор всегда»): какие параметры есть у мастеров,
#   что переопределено у трёх MIC меток, какой у них parent/blend/shading.
import unreal

EAL = unreal.EditorAssetLibrary
MLIB = unreal.MaterialEditingLibrary

MASTERS = ["/Game/Materials/M_AsdaObject", "/Game/Materials/M_AsdaObjectLit"]
MICS = ["/Game/AsdaAssets/Models/NPC/eff_0_npc_99_exclam-a_%02d/Materials/eff_0_npc_99_exclam-a_%02d" % (i, i)
        for i in range(3)]


def dump_master(p):
    m = EAL.load_asset(p)
    if not m:
        unreal.log("[qmat] МАСТЕР %s — НЕТ" % p)
        return
    unreal.log("[qmat] МАСТЕР %s: shading=%s blend=%s twosided=%s" % (
        p, m.get_editor_property("shading_model"), m.get_editor_property("blend_mode"),
        m.get_editor_property("two_sided")))
    for nm in MLIB.get_scalar_parameter_names(m):
        unreal.log("[qmat]    scalar  %s = %s" % (nm, MLIB.get_material_default_scalar_parameter_value(m, nm)))
    for nm in MLIB.get_vector_parameter_names(m):
        unreal.log("[qmat]    vector  %s = %s" % (nm, MLIB.get_material_default_vector_parameter_value(m, nm)))
    for nm in MLIB.get_texture_parameter_names(m):
        t = MLIB.get_material_default_texture_parameter_value(m, nm)
        unreal.log("[qmat]    texture %s = %s" % (nm, t.get_name() if t else None))
    for nm in MLIB.get_static_switch_parameter_names(m):
        unreal.log("[qmat]    switch  %s = %s" % (nm, MLIB.get_material_default_static_switch_parameter_value(m, nm)))


def dump_mic(p):
    mi = EAL.load_asset(p)
    if not mi:
        unreal.log("[qmat] MIC %s — НЕТ" % p)
        return
    par = mi.get_editor_property("parent")
    unreal.log("[qmat] MIC %s: parent=%s" % (p, par.get_name() if par else None))
    bo = mi.get_editor_property("base_property_overrides")
    unreal.log("[qmat]    overrides: blend=%s(ovr=%s) shading=%s(ovr=%s) twosided=%s(ovr=%s) clip=%s(ovr=%s)" % (
        bo.get_editor_property("blend_mode"), bo.get_editor_property("override_blend_mode"),
        bo.get_editor_property("shading_model"), bo.get_editor_property("override_shading_model"),
        bo.get_editor_property("two_sided"), bo.get_editor_property("override_two_sided"),
        bo.get_editor_property("opacity_mask_clip_value"), bo.get_editor_property("override_opacity_mask_clip_value")))
    for e in mi.get_editor_property("scalar_parameter_values"):
        unreal.log("[qmat]    scalar  %s = %s" % (e.get_editor_property("parameter_info").name,
                                                  e.get_editor_property("parameter_value")))
    for e in mi.get_editor_property("vector_parameter_values"):
        unreal.log("[qmat]    vector  %s = %s" % (e.get_editor_property("parameter_info").name,
                                                  e.get_editor_property("parameter_value")))
    for e in mi.get_editor_property("texture_parameter_values"):
        t = e.get_editor_property("parameter_value")
        unreal.log("[qmat]    texture %s = %s" % (e.get_editor_property("parameter_info").name,
                                                  t.get_name() if t else None))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global p
    unreal.log("[qmat] ==== МАТЕРИАЛЫ КВЕСТОВЫХ МЕТОК ====")
    for p in MASTERS:
        dump_master(p)
    for p in MICS:
        dump_mic(p)
    unreal.log("[qmat] ==== КОНЕЦ ====")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
