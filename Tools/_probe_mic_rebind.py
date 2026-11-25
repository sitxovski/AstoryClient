# Copyright (c) 2026 @sitxovski (Discord)
# _probe_mic_rebind.py — ДОКАЗАТЕЛЬСТВО причины «серых меток»: смена parent у MaterialInstanceConstant
#   из Python БЕЗ update_material_instance() не привязывает переопределения к параметрам нового мастера,
#   и инстанс отдаёт рендеру ДЕФОЛТЫ МАСТЕРА (Tex=DefaultTexture — ровный серый, GlowTint=белый).
#   Одноразовая проба: создаёт временный MIC в /Game/Temp, меряет оба сценария, удаляет за собой.
import unreal
EAL = unreal.EditorAssetLibrary
MLIB = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

TINT = unreal.LinearColor(1.0, 0.214041, 0.0, 1.0)


def measure(name, do_update):
    p = "/Game/Temp/%s" % name
    if EAL.does_asset_exist(p):
        EAL.delete_asset(p)
    mi = TOOLS.create_asset(name, "/Game/Temp", unreal.MaterialInstanceConstant,
                            unreal.MaterialInstanceConstantFactoryNew())
    # исходное состояние — как было у меток до правки: parent = M_AsdaObjectLit + Tex
    mi.set_editor_property("parent", OLD)
    MLIB.update_material_instance(mi)
    MLIB.set_material_instance_texture_parameter_value(mi, "Tex", TEX)
    MLIB.update_material_instance(mi)
    # ПЕРЕПРИВЯЗКА к новому мастеру
    mi.set_editor_property("parent", NEW)
    if do_update:
        MLIB.update_material_instance(mi)
    MLIB.set_material_instance_vector_parameter_value(mi, "GlowTint", TINT)
    MLIB.set_material_instance_texture_parameter_value(mi, "Tex", TEX)
    if do_update:
        MLIB.update_material_instance(mi)
    t = MLIB.get_material_instance_texture_parameter_value(mi, "Tex")
    v = MLIB.get_material_instance_vector_parameter_value(mi, "GlowTint")
    unreal.log("[rebind] update_material_instance=%s -> Tex=%s GlowTint=(%.4f,%.4f,%.4f)"
               % (do_update, t.get_name() if t else "None", v.r, v.g, v.b))
    EAL.delete_asset(p)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global NEW, OLD, TEX
    TEX = unreal.load_asset("/Game/AsdaAssets/Textures/NPC/Texture/eff_0_npc_99_exclam-a_00")
    OLD = unreal.load_asset("/Game/Materials/M_AsdaObjectLit")
    NEW = unreal.load_asset("/Game/Materials/M_AsdaMeshGlowTint")
    measure("MIC_probe_noupd", False)
    measure("MIC_probe_upd", True)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
