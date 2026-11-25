# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# ★★★ ГРАБЛИ (31.10.2026) — СКРИПТ ОТКЛЮЧЁН, НЕ ВКЛЮЧАТЬ. Запуск 30.10 18:06 снял текстуры с фона Login и ВСЕХ объектов карт.
#   Что сделал: в ОБЩИЙ `M_AsdaObjectLit` добавил 7 узлов — скаляры ObjTintR/G/B (use_custom_primitive_data, индексы 2/3/4,
#   умолчание 1.0) → 2 AppendVector → 2 Multiply, и переподключил на них BaseColor и Emissive. Узлы текстур (Tex/TexC/TexN/TexRE),
#   их связи и параметры не менялись.
#   Почему обнулило: Custom Primitive Data, НЕ прописанные компонентом, движок заполняет нулями
#   (`Engine/Public/PrimitiveUniformShaderParametersBuilder.h:267-271`, «Clear to 0»); `default_value` параметра в шейдер не идёт ⇒
#   у каждого объекта без CPD BaseColor × 0 и Emissive × 0 — остаются лишь блики и небо, «плоский тёмный цвет».
#   Почему не поймано: сверка «свойства/параметры до/после» смотрит граф и умолчания, а они «в порядке» — сломался кадр.
#   Правило: в общем мастере CPD только с тождеством при 0 (образец HitFlinchTint: умолчание 0, формула 1 − F) или отдельный мастер;
#   правку общего мастера проверять КАДРАМИ (Login + карты). Тон точки теперь — `Tools/asda_campwar_point_tint_mat.py`.
#
# asda_add_object_tint.py — ★ 30.10.2026 ТОН ОБЪЕКТА ТОЧКИ «БИТВЫ АРМИЙ» (боевая фаза, карта 19). [ОРИГИНАЛ]
#
# ЭТАЛОН. `sub_9354E4(obj, r, g, b)` → `childArray_BatchApply@0x114A5D0` → `meshParts_SetVec3Field360` у каждой части меша объекта:
#   поле +360 частей — «амбиент» вершинного освещения (то же поле, что у вздрагивания персонажа, см. `asda_add_hit_flinch_tint.py`).
#   Обычно +360 = (1,1,1) ⇒ на экране чистая текстура; кадровый тик `LightFx_UpdateAll@0xCA5F90` и 6724 пишут туда цвет армии:
#   state 1 — «половинный» (Свет (0,0,.5), Тьма (.5,0,0), Хаос (0,.5,0)), state 2 — «полный», захват — тон от белого к половинному
#   цвету армии захватчика (−0.0001·dt за кадр), конец захвата — цвет слота (по владельцу или белый).
# ПОРТ. Объекты точек (NPC 188…197, модель `mdel_obscuranpotal-a_00`) рисуются инстансами мастера `M_AsdaObjectLit`. Переносим
#   ИТОГ как у вздрагивания: BaseColor и Emissive умножаются на Tint = (R, G, B), где R/G/B — скаляры с use_custom_primitive_data,
#   индексы 2/3/4 (0 — мастера эффектов, 1 — HitFlinchTint персонажа). По умолчанию 1.0: пока C++ не пишет Custom Primitive Data
#   (все прочие объекты и карты), множитель (1,1,1) ⇒ картинка побайтно прежняя. [ПОРТ: приближение — слагаемое направленного
#   света эталона c87·N·L здесь отдельно не повторяется, освещение UE масштабируется целиком.]
#
# ПОЧЕМУ МАСТЕР, А НЕ ОТДЕЛЬНЫЙ ИНСТАНС. Параметр цвета нужен в шейдере в любом случае (у инстанса своих узлов нет); копия мастера
#   разошлась бы с ручными правками владельца в `M_AsdaObjectLit`. Ветка инертна (значения по умолчанию — тождество), правка проверяется
#   снимком всех свойств и параметров ДО/ПОСЛЕ (как у `asda_add_hit_flinch_tint.py`), инстансы (MIC) не трогаются.
#
# Повторный запуск — пропуск (метка AsdaObjectTint в описании корневого узла BaseColor).
# Запуск: python Tools/run_headless_safe.py Tools/asda_add_object_tint.py --marker "=== ГОТОВО: тон объекта ===" --timeout 1200
#   Сухой прогон (ничего не пишет): ASDA_OBJTINT_DRYRUN=1.
import os

MATS = ["M_AsdaObjectLit"]
PKG = "/Game/Materials"
MARK = "AsdaObjectTint"
PARAMS = [("ObjTintR", 2), ("ObjTintG", 3), ("ObjTintB", 4)]
PROPS = ["blend_mode", "shading_model", "two_sided", "used_with_skeletal_mesh", "used_with_static_lighting", "used_with_instanced_static_meshes",
         "opacity_mask_clip_value", "dithered_lod_transition", "material_domain", "cast_dynamic_shadow_as_masked", "tangent_space_normal",
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
    s["expressions"] = int(mel.get_num_material_expressions(m))
    return s


def main():
    # ★★★ 31.10.2026 СКРИПТ ОТКЛЮЧЁН — СЛОМАЛ ТЕКСТУРЫ ВСЕХ ОБЪЕКТОВ. Custom Primitive Data, не прописанные компонентом, движок
    #   обнуляет (`PrimitiveUniformShaderParametersBuilder.h`, «Clear to 0»), значение по умолчанию параметра (1.0) в шейдер не идёт ⇒
    #   BaseColor/Emissive общего `M_AsdaObjectLit` умножились на 0 у фона логина и всех объектов карт. Мастер возвращён из бэкапа
    #   (md5 db6c2f39). Тон точки теперь — отдельный мастер `Tools/asda_campwar_point_tint_mat.py`. НЕ ВКЛЮЧАТЬ.
    import unreal
    unreal.log_error("[тон объекта] asda_add_object_tint.py ОТКЛЮЧЁН (31.10.2026, ломал общий мастер) — см. asda_campwar_point_tint_mat.py")
    unreal.log("=== ГОТОВО: тон объекта ===")
    return
    eal = unreal.EditorAssetLibrary
    mel = unreal.MaterialEditingLibrary
    dry = os.environ.get("ASDA_OBJTINT_DRYRUN", "") == "1"
    for name in MATS:
        path = "%s/%s" % (PKG, name)
        m = eal.load_asset(path)
        if not m:
            unreal.log_warning("[тон объекта] нет %s — пропуск" % name)
            continue
        old_bc = mel.get_material_property_input_node(m, unreal.MaterialProperty.MP_BASE_COLOR)
        old_em = mel.get_material_property_input_node(m, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        if isinstance(old_bc, unreal.MaterialExpressionMultiply) and MARK in str(old_bc.get_editor_property("desc")):
            unreal.log("[тон объекта] %s: ветка уже стоит — пропуск" % name)
            continue
        if not old_bc:
            unreal.log_error("[тон объекта] %s: BaseColor не подключён — правка отменена" % name)
            continue
        before = snapshot(unreal, m)
        unreal.log("[тон объекта] %s: снимок ДО — параметров/свойств %d; Emissive %s" % (name, len(before), "есть" if old_em else "пуст"))
        if dry:
            for k in sorted(before):
                unreal.log("[тон объекта]   %s = %s" % (k, before[k]))
            continue

        def E(cls, x, y):
            return mel.create_material_expression(m, cls, x, y)

        nodes = []
        for i, (pn, idx) in enumerate(PARAMS):
            f = E(unreal.MaterialExpressionScalarParameter, -900, 1600 + 80 * i)
            f.set_editor_property("parameter_name", pn)
            f.set_editor_property("default_value", 1.0)
            f.set_editor_property("use_custom_primitive_data", True)
            f.set_editor_property("primitive_data_index", idx)
            f.set_editor_property("desc", MARK + ": канал тона объекта точки (C++ пишет CPD[%d]; по умолчанию 1)" % idx)
            nodes.append(f)
        a2 = E(unreal.MaterialExpressionAppendVector, -700, 1620)
        mel.connect_material_expressions(nodes[0], "", a2, "A")
        mel.connect_material_expressions(nodes[1], "", a2, "B")
        a3 = E(unreal.MaterialExpressionAppendVector, -550, 1640)
        mel.connect_material_expressions(a2, "", a3, "A")
        mel.connect_material_expressions(nodes[2], "", a3, "B")

        mb = E(unreal.MaterialExpressionMultiply, -250, 1500)
        mb.set_editor_property("desc", MARK)
        mel.connect_material_expressions(old_bc, "", mb, "A")
        mel.connect_material_expressions(a3, "", mb, "B")
        mel.connect_material_property(mb, "", unreal.MaterialProperty.MP_BASE_COLOR)
        if old_em:
            me = E(unreal.MaterialExpressionMultiply, -250, 1750)
            me.set_editor_property("desc", MARK)
            mel.connect_material_expressions(old_em, "", me, "A")
            mel.connect_material_expressions(a3, "", me, "B")
            mel.connect_material_property(me, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

        after = snapshot(unreal, m)
        new_keys = {"scalar:" + pn for pn, _ in PARAMS} | {"expressions"}
        diff = []
        for k in sorted(set(before) | set(after)):
            if k in new_keys:
                continue
            if before.get(k) != after.get(k):
                diff.append("%s: %s → %s" % (k, before.get(k), after.get(k)))
        added = after.get("expressions", 0) - before.get("expressions", 0)
        if diff or any(("scalar:" + pn) not in after for pn, _ in PARAMS):
            unreal.log_error("[тон объекта] %s: ПРОВЕРКА РУЧНЫХ ПРАВОК НЕ ПРОЙДЕНА — НЕ СОХРАНЯЮ: %s" % (name, "; ".join(diff) or "нет новых параметров"))
            continue
        m.modify()
        mel.recompile_material(m)
        ok = eal.save_loaded_asset(m)
        unreal.log("[тон объекта] %s: ветка добавлена (узлов +%d), параметров/свойств без изменений %d (сверено), save=%s" % (
            name, added, len(before) - 1, ok))
    unreal.log("=== ГОТОВО: тон объекта ===")


if __name__ == "__main__":
    main()
