# Copyright (c) 2026 @sitxovski (Discord)
# _probe_qmark_final.py — итоговая проверка квестовых меток: тело + мастер обводки.
import unreal
EAL = unreal.EditorAssetLibrary
MLIB = unreal.MaterialEditingLibrary


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for i in range(3):
        p = "/Game/AsdaAssets/Models/NPC/eff_0_npc_99_exclam-a_%02d/Materials/eff_0_npc_99_exclam-a_%02d" % (i, i)
        mi = EAL.load_asset(p)
        par = mi.get_editor_property("parent")
        t = MLIB.get_material_instance_texture_parameter_value(mi, "Tex")
        unreal.log("[final] тело _%02d: parent=%s Tex=%s" % (i, par.get_name() if par else None,
                                                            t.get_name() if t else None))
    m = EAL.load_asset("/Game/Materials/M_AsdaQuestMarkOutline")
    if m:
        unreal.log("[final] обводка: shading=%s blend=%s twosided=%s skel=%s" % (
            m.get_editor_property("shading_model"), m.get_editor_property("blend_mode"),
            m.get_editor_property("two_sided"), m.get_editor_property("used_with_skeletal_mesh")))
        for nm in MLIB.get_scalar_parameter_names(m):
            unreal.log("[final]   scalar %s = %s" % (nm, MLIB.get_material_default_scalar_parameter_value(m, nm)))
        for nm in MLIB.get_vector_parameter_names(m):
            unreal.log("[final]   vector %s = %s" % (nm, MLIB.get_material_default_vector_parameter_value(m, nm)))
    else:
        unreal.log_error("[final] НЕТ мастера обводки")
    unreal.log("[final] устаревший M_AsdaMeshGlowTint существует: %s"
               % EAL.does_asset_exist("/Game/Materials/M_AsdaMeshGlowTint"))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
