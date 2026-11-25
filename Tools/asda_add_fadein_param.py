# Copyright (c) 2026 @sitxovski (Discord)
# FadeIn-параметр для фейд-анимации появления превью-персонажей/глифов char-select (эталон: фейд-ин ~1с).
#   M_AsdaObjectLit (masked): OpacityMask *= FadeIn → dither-fade (с TSR выглядит плавной прозрачностью).
#   M_AsdaEffectAdd / M_AsdaEffectAlpha (глифы, translucent): Opacity (или Emissive для additive) *= FadeIn.
#   Дефолт 1.0 → никого не меняет; рантайм крутит через SetScalarParameterValueOnMaterials (авто-MID).
#   МОДИФИКАЦИЯ существующих мастеров (не пересоздание). Запуск: run_headless.bat asda_add_fadein_param.py
import unreal
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

def add_fade(mat_path, prop):
    m = EAL.load_asset(mat_path)
    if not m:
        unreal.log_warning("[FADE] нет %s" % mat_path); return
    node = MEL.get_material_property_input_node(m, prop)
    if not node:
        unreal.log_warning("[FADE] %s: вход %s пуст — пропуск" % (mat_path, prop)); return
    # выход прежнего входа (у OpacityMask это "A" текстуры, не дефолт!)
    try: out = str(MEL.get_material_property_input_node_output_name(m, prop))
    except Exception: out = ""
    sp = MEL.create_material_expression(m, unreal.MaterialExpressionScalarParameter, 550, 600)
    sp.set_editor_property("parameter_name", "FadeIn")
    sp.set_editor_property("default_value", 1.0)
    mul = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, 700, 550)
    MEL.connect_material_expressions(node, out, mul, "A")
    MEL.connect_material_expressions(sp, "", mul, "B")
    MEL.connect_material_property(mul, "", prop)
    m.modify()
    ok = EAL.save_loaded_asset(m)
    unreal.log("[FADE] %s: FadeIn на %s save=%s" % (mat_path.rsplit("/",1)[-1], prop, ok))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global m
    add_fade("/Game/Materials/M_AsdaObjectLit", unreal.MaterialProperty.MP_OPACITY_MASK)
    for eff in ("/Game/Effects/M_AsdaEffectAdd", "/Game/Effects/M_AsdaEffectAlpha"):
        m = EAL.load_asset(eff)
        if not m: continue
        # translucent: Opacity; additive без Opacity-входа → Emissive
        if MEL.get_material_property_input_node(m, unreal.MaterialProperty.MP_OPACITY):
            add_fade(eff, unreal.MaterialProperty.MP_OPACITY)
        else:
            add_fade(eff, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.log("[FADE] done")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
