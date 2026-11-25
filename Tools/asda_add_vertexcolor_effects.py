# Copyright (c) 2026 @sitxovski (Discord)
# asda_add_vertexcolor_effects.py — ★ ГЛАВНЫЙ ФИКС аудита 2026-07-27: канал цвета эффектов.
#   Эталон доносит цвет/альфу ВЕЗДЕ вершинным diffuse (FVF 0x142); мастера M_AsdaEffectAdd/Alpha
#   VertexColor не содержали → RGB-марш/альфа-фейды/мерцания были МЕРТВЫ во всех путях порта.
#   Add (BLEND_ADDITIVE): Emissive = старая цепь × VC.rgb × VC.a   (БЕЗ ×2 — MODULATE2X опровергнут)
#   Alpha (BLEND_TRANSLUCENT): Emissive(или BaseColor) ×= VC.rgb; Opacity ×= VC.a
#   Идемпотентность: маркер-параметр "VCApplied" (не подключён; default 0.5 = уже применён).
import unreal
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

def has_marker(m):
    try:
        return abs(MEL.get_material_default_scalar_parameter_value(m, "VCApplied") - 0.5) < 1e-3
    except Exception:
        return False

def add_marker(m):
    sp = MEL.create_material_expression(m, unreal.MaterialExpressionScalarParameter, 900, 900)
    sp.set_editor_property("parameter_name", "VCApplied")
    sp.set_editor_property("default_value", 0.5)

def mul_into(m, prop, vc, out_name):
    node = MEL.get_material_property_input_node(m, prop)
    if not node:
        unreal.log_warning("  вход %s пуст — пропуск" % prop); return False
    try: out = str(MEL.get_material_property_input_node_output_name(m, prop))
    except Exception: out = ""
    mul = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, 700, 300)
    MEL.connect_material_expressions(node, out, mul, "A")
    MEL.connect_material_expressions(vc, out_name, mul, "B")
    MEL.connect_material_property(mul, "", prop)
    return True

def process(path, additive):
    m = EAL.load_asset(path)
    if not m:
        unreal.log_warning("[VC] нет %s" % path); return
    if has_marker(m):
        unreal.log("[VC] %s: уже применён — скип" % path.rsplit('/',1)[-1]); return
    vc = MEL.create_material_expression(m, unreal.MaterialExpressionVertexColor, 500, 300)
    ok = False
    if additive:
        # Emissive ×VC.rgb ×VC.a (двумя Multiply)
        if mul_into(m, unreal.MaterialProperty.MP_EMISSIVE_COLOR, vc, ""):
            ok = mul_into(m, unreal.MaterialProperty.MP_EMISSIVE_COLOR, vc, "A")
    else:
        e_ok = mul_into(m, unreal.MaterialProperty.MP_EMISSIVE_COLOR, vc, "")
        if not e_ok:
            e_ok = mul_into(m, unreal.MaterialProperty.MP_BASE_COLOR, vc, "")
        o_ok = mul_into(m, unreal.MaterialProperty.MP_OPACITY, vc, "A")
        ok = e_ok or o_ok
    if ok:
        add_marker(m)
        m.modify()
        saved = EAL.save_loaded_asset(m)
        unreal.log("[VC] %s: VertexColor применён, save=%s" % (path.rsplit('/',1)[-1], saved))
    else:
        unreal.log_warning("[VC] %s: не удалось подключить" % path)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    process("/Game/Effects/M_AsdaEffectAdd", True)
    process("/Game/Effects/M_AsdaEffectAlpha", False)
    unreal.log("=== VC DONE ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
