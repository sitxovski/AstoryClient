# Copyright (c) 2026 @sitxovski (Discord)
# _probe_objectlit_opacity.py — куда в мастер-материале M_AsdaObjectLit приходит OpacityMask.
#
# Без этого «вернули альфу текстуре» ещё ничего не значит: если маска непрозрачности берётся не
# из альфы Tex, белый фон останется на месте.
import unreal


def main():
    m = unreal.EditorAssetLibrary.load_asset("/Game/Materials/M_AsdaObjectLit")
    unreal.log("=== M_AsdaObjectLit: blend=%s clip=%s" % (m.get_editor_property("blend_mode"),
                                                          m.get_editor_property("opacity_mask_clip_value")))
    # ★ в UE 5.8 список выражений лежит по-разному в разных сборках — берём первый доступный вариант
    exprs = []
    for attempt in ("expression_collection", "expressions"):
        try:
            v = m.get_editor_property(attempt)
            exprs = v.get_editor_property("expressions") if attempt == "expression_collection" else v
            break
        except Exception:
            continue
    if not exprs:
        try:
            exprs = unreal.MaterialEditingLibrary.get_material_expressions(m)
        except Exception:
            exprs = []
    for e in exprs:
        info = e.get_class().get_name()
        extra = ""
        if isinstance(e, unreal.MaterialExpressionTextureSampleParameter2D):
            t = e.get_editor_property("texture")
            extra = " параметр=%s текстура=%s sampler=%s" % (
                e.get_editor_property("parameter_name"),
                t.get_path_name() if t else None,
                e.get_editor_property("sampler_type"))
        elif isinstance(e, unreal.MaterialExpressionScalarParameter):
            extra = " параметр=%s знач=%s" % (e.get_editor_property("parameter_name"),
                                              e.get_editor_property("default_value"))
        elif isinstance(e, unreal.MaterialExpressionVectorParameter):
            extra = " параметр=%s" % e.get_editor_property("parameter_name")
        unreal.log("   выражение: %-50s%s" % (info, extra))

    # какие входы материала на что заведены
    for prop in ("EmissiveColor", "Opacity", "OpacityMask", "BaseColor", "Metallic", "Roughness",
                 "Normal", "AmbientOcclusion"):
        try:
            conn = unreal.MaterialEditingLibrary.get_inputs_for_material_property(
                m, getattr(unreal.MaterialProperty, "MP_" + prop.upper()))
            unreal.log("   вход %s: %s" % (prop, conn))
        except Exception:
            pass

    # прямой опрос: что подключено к OpacityMask
    try:
        conn = m.get_editor_property("opacity_mask")
        unreal.log("   OpacityMask expression=%s output=%s mask=%s r=%s g=%s b=%s a=%s" % (
            conn.get_editor_property("expression"), conn.get_editor_property("output_index"),
            conn.get_editor_property("mask"), conn.get_editor_property("mask_r"),
            conn.get_editor_property("mask_g"), conn.get_editor_property("mask_b"),
            conn.get_editor_property("mask_a")))
    except Exception as exc:
        unreal.log("   OpacityMask: ? (%s)" % exc)
    unreal.log("=== ГОТОВО ===")


# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
