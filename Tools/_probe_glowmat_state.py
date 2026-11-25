# Copyright (c) 2026 @sitxovski (Discord)
# _probe_glowmat_state.py — почему метки СЕРЫЕ: состояние мастера M_AsdaMeshGlowTint.
#   Печатает выражения графа, их связи, дефолт текстур-параметра и ОШИБКИ КОМПИЛЯЦИИ.
import unreal

EAL = unreal.EditorAssetLibrary
MLIB = unreal.MaterialEditingLibrary
M = "/Game/Materials/M_AsdaMeshGlowTint"


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    mat = EAL.load_asset(M)
    if not mat:
        unreal.log("[glowmat] НЕТ %s" % M)
    else:
        unreal.log("[glowmat] %s shading=%s blend=%s twosided=%s" % (
            M, mat.get_editor_property("shading_model"), mat.get_editor_property("blend_mode"),
            mat.get_editor_property("two_sided")))
        try:
            eds = mat.get_editor_property("editor_only_data")
            exprs = eds.get_editor_property("expression_collection").get_editor_property("expressions")
        except Exception as e:
            exprs = []
            unreal.log("[glowmat] выражения недоступны: %s" % e)
        for e in exprs:
            extra = ""
            if isinstance(e, unreal.MaterialExpressionTextureSampleParameter2D):
                t = e.get_editor_property("texture")
                extra = "param=%s texture=%s sampler=%s" % (
                    e.get_editor_property("parameter_name"), t.get_name() if t else "None",
                    e.get_editor_property("sampler_type"))
            elif isinstance(e, unreal.MaterialExpressionVectorParameter):
                extra = "param=%s default=%s" % (e.get_editor_property("parameter_name"),
                                                 e.get_editor_property("default_value"))
            unreal.log("[glowmat]   expr %s %s" % (e.get_class().get_name(), extra))
        for nm in MLIB.get_texture_parameter_names(mat):
            t = MLIB.get_material_default_texture_parameter_value(mat, nm)
            unreal.log("[glowmat]   texparam %s default=%s" % (nm, t.get_name() if t else "None"))
        for nm in MLIB.get_vector_parameter_names(mat):
            unreal.log("[glowmat]   vecparam %s default=%s" % (
                nm, MLIB.get_material_default_vector_parameter_value(mat, nm)))
        # компиляция: ошибки видны в логе редактора при recompile
        unreal.log("[glowmat] --- recompile ---")
        MLIB.recompile_material(mat)
        unreal.log("[glowmat] --- recompile done ---")
    # что реально лежит в текстурах меток
    for i in range(3):
        tp = "/Game/AsdaAssets/Textures/NPC/Texture/eff_0_npc_99_exclam-a_%02d" % i
        t = EAL.load_asset(tp)
        if t:
            unreal.log("[glowmat] tex %s: %dx%d srgb=%s comp=%s group=%s" % (
                tp, t.blueprint_get_size_x(), t.blueprint_get_size_y(),
                t.get_editor_property("srgb"), t.get_editor_property("compression_settings"),
                t.get_editor_property("lod_group")))
        else:
            unreal.log("[glowmat] tex %s — НЕТ" % tp)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
