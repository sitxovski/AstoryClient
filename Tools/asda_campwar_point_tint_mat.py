# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_campwar_point_tint_mat.py — ★ 31.10.2026 ТОН ОБЪЕКТА ТОЧКИ «БИТВЫ АРМИЙ» — ОТДЕЛЬНЫЙ МАСТЕР ТОЛЬКО ДЛЯ ТОЧЕК. [ОРИГИНАЛ]
#
# ЭТАЛОН. `sub_9354E4(obj, r, g, b)` → `childArray_BatchApply@0x114A5D0` → `meshParts_SetVec3Field360` у каждой части меша объекта
#   точки: поле +360 — «амбиент» вершинного освещения; обычно (1,1,1), тик `LightFx_UpdateAll@0xCA5F90` и 6724 пишут туда цвет армии
#   (state 1 — половинный, state 2 — полный, захват — от белого к половинному цвету захватчика).
#
# ★ ПОЧЕМУ ОТДЕЛЬНЫЙ МАСТЕР (грабли 30.10). Первая версия (`asda_add_object_tint.py`, ОТКЛЮЧЁН) правила ОБЩИЙ `M_AsdaObjectLit`:
#   умножала BaseColor/Emissive на скаляры с use_custom_primitive_data и значением по умолчанию 1.0. Но Custom Primitive Data, которые
#   компонент не прописал, движок обнуляет (`PrimitiveUniformShaderParametersBuilder.h`: «Clear to 0»), значение по умолчанию
#   параметра в шейдер НЕ попадает ⇒ у ВСЕХ объектов карт и фона логина цвет и свечение умножились на 0 — текстуры пропали.
#   Здесь: копия мастера `M_AsdaObjectLit_CampTint` (общий мастер НЕ открывается на запись), тон — ОБЫЧНЫЙ векторный параметр
#   `ObjTint` (по умолчанию (1,1,1)), без CPD; C++ (`UAsdaCampWarBattleSubsystem::ApplyObjectColor`) ставит на слоты меша ТОЛЬКО
#   объекта точки динамический инстанс этой копии с параметрами исходного MIC. Остальные объекты копию не видят.
#
# Повторный запуск — пропуск (метка AsdaCampTint в описании корневого узла BaseColor копии).
# Запуск: python Tools/run_headless_safe.py Tools/asda_campwar_point_tint_mat.py --marker "=== ГОТОВО: мастер тона точки ===" --timeout 1200
import os

SRC = "/Game/Materials/M_AsdaObjectLit"
DST = "/Game/Materials/M_AsdaObjectLit_CampTint"
MARK = "AsdaCampTint"
PARAM = "ObjTint"
POINT_MICS = "/Game/AsdaAssets/Models/NPC/mdel_obscuranpotal-a_00/Materials"
PROPS = ["blend_mode", "shading_model", "two_sided", "used_with_skeletal_mesh", "used_with_static_lighting",
         "used_with_instanced_static_meshes", "opacity_mask_clip_value", "dithered_lod_transition", "material_domain",
         "cast_dynamic_shadow_as_masked", "tangent_space_normal", "use_material_attributes"]


def snapshot(unreal, m):
    """Свойства + параметры с умолчаниями + число узлов — для сверки копии с источником."""
    mel = unreal.MaterialEditingLibrary
    s = {}
    for p in PROPS:
        try:
            s["prop:" + p] = str(m.get_editor_property(p))
        except Exception:
            pass
    for n in mel.get_scalar_parameter_names(m):
        s["scalar:%s" % n] = round(float(mel.get_material_default_scalar_parameter_value(m, n)), 6)
    for n in mel.get_vector_parameter_names(m):
        v = mel.get_material_default_vector_parameter_value(m, n)
        s["vector:%s" % n] = (round(v.r, 6), round(v.g, 6), round(v.b, 6), round(v.a, 6))
    for n in mel.get_texture_parameter_names(m):
        t = mel.get_material_default_texture_parameter_value(m, n)
        s["texture:%s" % n] = t.get_path_name() if t else None
    for n in mel.get_static_switch_parameter_names(m):
        s["switch:%s" % n] = bool(mel.get_material_default_static_switch_parameter_value(m, n))
    s["expressions"] = int(mel.get_num_material_expressions(m))
    return s


def main():
    import unreal
    eal = unreal.EditorAssetLibrary
    mel = unreal.MaterialEditingLibrary
    src = eal.load_asset(SRC)
    if not src:
        unreal.log_error("[тон точки] нет %s — выход" % SRC)
        print("=== ГОТОВО: мастер тона точки ===")
        return
    src_snap = snapshot(unreal, src)

    # Переключатели MIC точки: у динамического инстанса статические переопределения недоступны — они обязаны совпадать с умолчанием.
    bad = []
    for a in eal.list_assets(POINT_MICS, recursive=False, include_folder=False):
        mic = eal.load_asset(a)
        if not isinstance(mic, unreal.MaterialInstanceConstant):
            continue
        for sw in mel.get_static_switch_parameter_names(src):
            v = mel.get_material_instance_static_switch_parameter_value(mic, sw)
            d = src_snap.get("switch:%s" % sw)
            unreal.log("[тон точки] %s: переключатель %s = %s (умолчание мастера %s)" % (mic.get_name(), sw, v, d))
            if bool(v) != bool(d):
                bad.append("%s.%s" % (mic.get_name(), sw))
    if bad:
        unreal.log_error("[тон точки] у MIC точки переключатели отличны от умолчания (%s) — динамический инстанс их потеряет; правка отменена" % bad)
        print("=== ГОТОВО: мастер тона точки ===")
        return

    m = eal.load_asset(DST) if eal.does_asset_exist(DST) else None
    if m:
        bc = mel.get_material_property_input_node(m, unreal.MaterialProperty.MP_BASE_COLOR)
        if isinstance(bc, unreal.MaterialExpressionMultiply) and MARK in str(bc.get_editor_property("desc")):
            unreal.log("[тон точки] %s уже готов — пропуск" % DST)
            print("=== ГОТОВО: мастер тона точки ===")
            return
        unreal.log_error("[тон точки] %s существует без метки %s — не трогаю, разобрать вручную" % (DST, MARK))
        print("=== ГОТОВО: мастер тона точки ===")
        return

    m = eal.duplicate_asset(SRC, DST)
    if not m:
        unreal.log_error("[тон точки] копия %s → %s не создана" % (SRC, DST))
        print("=== ГОТОВО: мастер тона точки ===")
        return
    dup_snap = snapshot(unreal, m)
    diff = ["%s: %s → %s" % (k, src_snap.get(k), dup_snap.get(k)) for k in sorted(set(src_snap) | set(dup_snap))
            if src_snap.get(k) != dup_snap.get(k)]
    if diff:
        unreal.log_error("[тон точки] копия не совпала с источником: %s — не сохраняю" % "; ".join(diff))
        print("=== ГОТОВО: мастер тона точки ===")
        return

    old_bc = mel.get_material_property_input_node(m, unreal.MaterialProperty.MP_BASE_COLOR)
    old_bc_out = mel.get_material_property_input_node_output_name(m, unreal.MaterialProperty.MP_BASE_COLOR)
    old_em = mel.get_material_property_input_node(m, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    old_em_out = mel.get_material_property_input_node_output_name(m, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if not old_bc:
        unreal.log_error("[тон точки] у копии BaseColor не подключён — не сохраняю")
        print("=== ГОТОВО: мастер тона точки ===")
        return

    def E(cls, x, y):
        return mel.create_material_expression(m, cls, x, y)

    tint = E(unreal.MaterialExpressionVectorParameter, -900, 1600)
    tint.set_editor_property("parameter_name", PARAM)
    tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    tint.set_editor_property("desc", MARK + ": тон объекта точки CampWar (sub_9354E4, поле +360); C++ ставит через динамический инстанс")
    rgb = E(unreal.MaterialExpressionComponentMask, -700, 1600)
    rgb.set_editor_property("r", True)
    rgb.set_editor_property("g", True)
    rgb.set_editor_property("b", True)
    rgb.set_editor_property("a", False)
    mel.connect_material_expressions(tint, "", rgb, "")

    mb = E(unreal.MaterialExpressionMultiply, -250, 1500)
    mb.set_editor_property("desc", MARK)
    mel.connect_material_expressions(old_bc, old_bc_out, mb, "A")
    mel.connect_material_expressions(rgb, "", mb, "B")
    mel.connect_material_property(mb, "", unreal.MaterialProperty.MP_BASE_COLOR)
    if old_em:
        me = E(unreal.MaterialExpressionMultiply, -250, 1750)
        me.set_editor_property("desc", MARK)
        mel.connect_material_expressions(old_em, old_em_out, me, "A")
        mel.connect_material_expressions(rgb, "", me, "B")
        mel.connect_material_property(me, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    after = snapshot(unreal, m)
    diff = []
    for k in sorted(set(src_snap) | set(after)):
        if k in ("vector:" + PARAM, "expressions"):
            continue
        if src_snap.get(k) != after.get(k):
            diff.append("%s: %s → %s" % (k, src_snap.get(k), after.get(k)))
    if diff or after.get("vector:" + PARAM) != (1.0, 1.0, 1.0, 1.0):
        unreal.log_error("[тон точки] сверка не прошла — не сохраняю: %s" % ("; ".join(diff) or "параметр %s" % after.get("vector:" + PARAM)))
        print("=== ГОТОВО: мастер тона точки ===")
        return
    mel.recompile_material(m)
    ok = eal.save_loaded_asset(m)
    unreal.log("[тон точки] %s создан: узлов %d → %d, параметр %s = (1,1,1), прочие параметры/свойства совпадают с %s (%d), save=%s" % (
        DST, src_snap["expressions"], after["expressions"], PARAM, SRC, len(src_snap) - 1, ok))
    print("=== ГОТОВО: мастер тона точки ===")


if __name__ == "__main__":
    main()
