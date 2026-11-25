# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_add_hit_flinch_tint.py — ★ 29.10.2026 ПОКРАСНЕНИЕ ПЕРСОНАЖА ПРИ ВЗДРАГИВАНИИ ОТ УДАРА (разрешение владельца 29.10). [ОРИГИНАЛ]
#
# ЭТАЛОН. Старт вздрагивания `gfxBias_LazyInitHalfTexel@0xA2C7B0` (МИСНЕЙМ) и тик `charEntityUpdateTick@0xA1ED30`
#   (entity_char.c:22713-22800) меняют константы вершинного шейдера skin (строка exe 0x15C39B0):
#     oD0 = c94(+360 «амбиент») + c87·max(0, N·L) + c91.xyz(+640 точечный свет) · max(0, 1 − c91.w·|c92 − p|²).
#   Обычно +360 = (1,1,1), +640 = (1,1,1,0) ⇒ вершинный цвет насыщен ⇒ на экране чистая текстура.
#   Во вздрагивании (125 мс): +360 = (0.3,0.3,0.3), +640 = (1,0,0,0) ⇒ R = текстура (насыщен), G/B = текстура·(0.3 + c87·N·L).
# ПОРТ. Освещение UE устроено иначе (вершинного цвета-множителя нет), поэтому переносим ИТОГ: красный канал без изменений,
#   зелёный и синий — умножение на (1 − F). F пишет C++ в Custom Primitive Data[1] каждой части тела жертвы на время
#   вздрагивания: F = 1 − asda.Char.HitFlinchTintGB (по умолчанию 0.3 — амбиент эталона), вне вздрагивания F = 0 ⇒ множитель
#   (1,1,1) ⇒ картинка побайтно прежняя. [ПОРТ: приближение — слагаемое c87·N·L эталона здесь не повторяется отдельно,
#   G/B освещаются штатным светом UE и масштабируются целиком.]
#
# ЧТО ДЕЛАЕТ (точечно, только два мастера персонажа): BaseColor := СТАРЫЙ_BaseColor · Tint, Emissive := СТАРЫЙ_Emissive · Tint,
#   Tint = (1, 1 − F, 1 − F), F = скаляр «HitFlinchTint» с use_custom_primitive_data, индекс 1 (индекс 0 — у мастеров эффектов).
#   Ни один существующий узел, параметр, значение по умолчанию и свойство материала не меняется — СКРИПТ ЭТО ПРОВЕРЯЕТ:
#   до и после правки снимает все свойства мастера и значения по умолчанию всех его параметров и НЕ СОХРАНЯЕТ при любом отличии,
#   кроме нового параметра HitFlinchTint. Инстансы (MIC) не трогаются — их ручные оверрайды живут на них.
#   Повторный запуск — пропуск (метка AsdaHitFlinchTint в описании корневого узла BaseColor).
#
# Запуск: python Tools/run_headless_safe.py Tools/asda_add_hit_flinch_tint.py --marker "=== ГОТОВО: покраснение ===" --timeout 1200
#   Сухой прогон (ничего не пишет): переменная окружения ASDA_HITTINT_DRYRUN=1.
import os

MATS = ["M_AsdaCharLit", "M_AsdaSkinLit"]
PKG = "/Game/Materials"
MARK = "AsdaHitFlinchTint"
PARAM = "HitFlinchTint"
CPD_INDEX = 1
PROPS = ["blend_mode", "shading_model", "two_sided", "used_with_skeletal_mesh", "opacity_mask_clip_value",
         "dithered_lod_transition", "material_domain", "cast_dynamic_shadow_as_masked", "tangent_space_normal",
         "use_material_attributes", "is_thin_surface", "fully_rough", "allow_translucent_custom_depth_writes"]


def snapshot(unreal, m):
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
    return s


def main():
    import unreal
    eal = unreal.EditorAssetLibrary
    mel = unreal.MaterialEditingLibrary
    dry = os.environ.get("ASDA_HITTINT_DRYRUN", "") == "1"
    for name in MATS:
        path = "%s/%s" % (PKG, name)
        m = eal.load_asset(path)
        if not m:
            unreal.log_warning("[покраснение] нет %s — пропуск" % name)
            continue
        old_bc = mel.get_material_property_input_node(m, unreal.MaterialProperty.MP_BASE_COLOR)
        old_em = mel.get_material_property_input_node(m, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        if isinstance(old_bc, unreal.MaterialExpressionMultiply) and MARK in str(old_bc.get_editor_property("desc")):
            unreal.log("[покраснение] %s: ветка уже стоит — пропуск" % name)
            continue
        if not old_bc:
            unreal.log_error("[покраснение] %s: BaseColor не подключён — правка отменена" % name)
            continue
        before = snapshot(unreal, m)
        unreal.log("[покраснение] %s: снимок ДО — параметров/свойств %d; Emissive %s" % (name, len(before), "есть" if old_em else "пуст"))
        if dry:
            for k in sorted(before):
                unreal.log("[покраснение]   %s = %s" % (k, before[k]))
            continue

        def E(cls, x, y):
            return mel.create_material_expression(m, cls, x, y)

        f = E(unreal.MaterialExpressionScalarParameter, -900, 1300)
        f.set_editor_property("parameter_name", PARAM)
        f.set_editor_property("default_value", 0.0)
        f.set_editor_property("use_custom_primitive_data", True)
        f.set_editor_property("primitive_data_index", CPD_INDEX)
        f.set_editor_property("desc", MARK + ": доля притушения G/B во вздрагивании (C++ пишет CPD[1])")
        om = E(unreal.MaterialExpressionOneMinus, -700, 1300)
        mel.connect_material_expressions(f, "", om, "")
        one = E(unreal.MaterialExpressionConstant, -700, 1220)
        one.set_editor_property("r", 1.0)
        a2 = E(unreal.MaterialExpressionAppendVector, -550, 1240)
        mel.connect_material_expressions(one, "", a2, "A")
        mel.connect_material_expressions(om, "", a2, "B")
        a3 = E(unreal.MaterialExpressionAppendVector, -400, 1260)
        mel.connect_material_expressions(a2, "", a3, "A")
        mel.connect_material_expressions(om, "", a3, "B")

        mb = E(unreal.MaterialExpressionMultiply, -250, 1100)
        mb.set_editor_property("desc", MARK)
        mel.connect_material_expressions(old_bc, "", mb, "A")
        mel.connect_material_expressions(a3, "", mb, "B")
        mel.connect_material_property(mb, "", unreal.MaterialProperty.MP_BASE_COLOR)
        if old_em:
            me = E(unreal.MaterialExpressionMultiply, -250, 1400)
            me.set_editor_property("desc", MARK)
            mel.connect_material_expressions(old_em, "", me, "A")
            mel.connect_material_expressions(a3, "", me, "B")
            mel.connect_material_property(me, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

        after = snapshot(unreal, m)
        diff = []
        for k in sorted(set(before) | set(after)):
            if k == "scalar:" + PARAM:
                continue
            if before.get(k) != after.get(k):
                diff.append("%s: %s → %s" % (k, before.get(k), after.get(k)))
        if diff or ("scalar:" + PARAM) not in after:
            unreal.log_error("[покраснение] %s: ПРОВЕРКА РУЧНЫХ ПРАВОК НЕ ПРОЙДЕНА — НЕ СОХРАНЯЮ: %s" % (name, "; ".join(diff) or "нет нового параметра"))
            continue
        m.modify()
        mel.recompile_material(m)
        ok = eal.save_loaded_asset(m)
        unreal.log("[покраснение] %s: ветка добавлена, параметров/свойств без изменений %d (сверено), save=%s" % (name, len(before), ok))
    unreal.log("=== ГОТОВО: покраснение ===")


if __name__ == "__main__":
    main()
