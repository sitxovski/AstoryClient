# Copyright (c) 2026 @sitxovski (Discord)
# asda_add_toon_rim.py — rim-свет («кайма») в игровые Lit-мастера, управляемый MPC_AsdaToon.
#
# ОСНОВАНИЕ (разбор Genshin, Reports/genshin_shading_plan.md, разд. 1.2 и шаг 3):
#   У Genshin rim — это НЕ Френель, а сравнение глубин в screen space; Френель у них отвечает за
#   эффект получения урона. Полноценное сравнение глубин живёт в пост-обработке (M_AsdaToonPP,
#   там уже читается SceneDepth), а здесь ставится дешёвое Френель-приближение прямо в материале:
#   оно не требует ни новых данных, ни лишнего прохода, и включается отдельным параметром.
#
# ЧТО ДЕЛАЕТ: Emissive := СТАРЫЙ_EMISSIVE + Fresnel(RimPower) · RimWidth · RimColor · RimEnable.
#   При RimEnable = 0 добавка равна НУЛЮ ⇒ Emissive численно тот же, что был. Это важно: мастера
#   уже несут ambient-floor (Emissive = BaseColor·AmbientFloor), и его нельзя потерять — иначе
#   персонаж уходит в чёрное (регрессия 2026-07-31, описана в asda_build_materials_lit.py).
#
# Запуск (headless):
#   UnrealEditor-Cmd.exe <проект> -ExecutePythonScript=".../asda_add_toon_rim.py"
#     -unattended -nopause -nosplash -nosound
MATS = ["M_AsdaCharLit", "M_AsdaObjectLit"]
MPC_PATH = "/Game/Materials/MPC_AsdaToon"
MARK = "AsdaToonRim"          # метка узла: повторный запуск не должен плодить вторую ветку


def main():
    import unreal
    eal = unreal.EditorAssetLibrary
    mel = unreal.MaterialEditingLibrary

    mpc = eal.load_asset(MPC_PATH)
    if not mpc:
        unreal.log_error("нет коллекции %s — сначала Tools/asda_build_toon_shaders.py" % MPC_PATH)
        return

    for name in MATS:
        path = "/Game/Materials/" + name
        m = eal.load_asset(path)
        if not m:
            unreal.log_warning("[rim] нет %s" % name)
            continue

        old_em = mel.get_material_property_input_node(m, unreal.MaterialProperty.MP_EMISSIVE_COLOR)

        # ★ Защита от повторного запуска. Перебрать все узлы нельзя: свойство `expressions`
        #   (и `expression_collection` в 5.8) закрыто от чтения из Python — проверено на месте.
        #   Но наша ветка всегда стоит КОРНЕМ Emissive, поэтому достаточно посмотреть его тип
        #   и метку в описании узла.
        if isinstance(old_em, unreal.MaterialExpressionAdd) and \
                MARK in str(old_em.get_editor_property("desc")):
            unreal.log("[rim] %s: ветка уже стоит — пропуск" % name)
            continue

        def E(cls, x, y):
            return mel.create_material_expression(m, cls, x, y)

        def coll(pname, x, y):
            e = E(unreal.MaterialExpressionCollectionParameter, x, y)
            e.set_editor_property("collection", mpc)
            e.set_editor_property("parameter_name", pname)
            return e

        p_on = coll("RimEnable", 900, 700)
        p_pow = coll("RimPower", 900, 780)
        p_wid = coll("RimWidth", 900, 860)
        p_col = coll("RimColor", 900, 940)

        fres = E(unreal.MaterialExpressionFresnel, 1150, 760)
        mel.connect_material_expressions(p_pow, "", fres, "ExponentIn")

        m1 = E(unreal.MaterialExpressionMultiply, 1350, 780)
        mel.connect_material_expressions(fres, "", m1, "A")
        mel.connect_material_expressions(p_wid, "", m1, "B")

        # ★ Параметр-вектор коллекции даёт float4; Emissive-цепь мастеров — float3. Без маски RGB
        #   Add не компилируется («Arithmetic between types float3 and float4 are undefined»),
        #   и вслед за мастером падают ВСЕ его инстансы (проверено на 2585 материалах персонажа).
        col_rgb = E(unreal.MaterialExpressionComponentMask, 1100, 940)
        col_rgb.set_editor_property("r", True)
        col_rgb.set_editor_property("g", True)
        col_rgb.set_editor_property("b", True)
        col_rgb.set_editor_property("a", False)
        mel.connect_material_expressions(p_col, "", col_rgb, "")

        m2 = E(unreal.MaterialExpressionMultiply, 1500, 820)
        mel.connect_material_expressions(m1, "", m2, "A")
        mel.connect_material_expressions(col_rgb, "", m2, "B")

        # ★ Умножение на RimEnable, а не Lerp: добавка обнуляется ровно, без промежуточных величин.
        gate = E(unreal.MaterialExpressionMultiply, 1650, 860)
        mel.connect_material_expressions(m2, "", gate, "A")
        mel.connect_material_expressions(p_on, "", gate, "B")

        add = E(unreal.MaterialExpressionAdd, 1850, 700)
        add.set_editor_property("desc", MARK)
        if old_em:
            mel.connect_material_expressions(old_em, "", add, "A")
        else:
            # Emissive был пуст — тогда «старое значение» это ноль.
            z = E(unreal.MaterialExpressionConstant, 1650, 640)
            z.set_editor_property("r", 0.0)
            mel.connect_material_expressions(z, "", add, "A")
        mel.connect_material_expressions(gate, "", add, "B")
        mel.connect_material_property(add, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

        m.modify()
        mel.recompile_material(m)
        ok = eal.save_loaded_asset(m)
        unreal.log("[rim] %s: ветка добавлена (старый Emissive %s), save=%s" %
                   (name, "сохранён" if old_em else "был пуст", ok))

    unreal.log("=== ГОТОВО: rim-ветка ===")


if __name__ == "__main__":
    main()
