# Copyright (c) 2026 @sitxovski (Discord)
# asda_gacha_probe_materials.py — почему автоматы гачи выходят БЕЛЫМИ (только чтение).
# Печатает слоты материалов скелетал-меша, чем они заняты, и есть ли у MIC текстура.
# Запуск: run_headless.bat asda_gacha_probe_materials.py
import unreal

ROOT = "/Game/AsdaAssets/Models/Skeletal/Gachya"
MODELS = ["md_gachya_%s_00" % c for c in "ab"]
EAL = unreal.EditorAssetLibrary


def log(m):
    unreal.log("[гача-мат] " + str(m))


def main():
    for name in MODELS:
        mesh = EAL.load_asset("%s/%s/SkeletalMeshes/%s" % (ROOT, name, name))
        if not mesh:
            log("нет меша %s" % name)
            continue
        mats = mesh.get_editor_property("materials")
        log("%s: слотов материалов = %d" % (name, len(mats)))
        for i, sm in enumerate(mats):
            mi = sm.get_editor_property("material_interface")
            slot = sm.get_editor_property("material_slot_name")
            if not mi:
                log("  слот %2d '%s' = ★ ПУСТО" % (i, slot))
                continue
            tex = "—"
            if isinstance(mi, unreal.MaterialInstanceConstant):
                tvals = mi.get_editor_property("texture_parameter_values")
                tex = ", ".join("%s=%s" % (
                    v.get_editor_property("parameter_info").get_editor_property("name"),
                    (v.get_editor_property("parameter_value").get_name()
                     if v.get_editor_property("parameter_value") else "НЕТ"))
                    for v in tvals) or "параметров нет"
                parent = mi.get_editor_property("parent")
                tex += " | мастер=%s" % (parent.get_name() if parent else "НЕТ")
            log("  слот %2d '%s' = %s (%s) :: %s" % (
                i, slot, mi.get_name(), type(mi).__name__, tex))


if __name__ == "__main__":
    main()
