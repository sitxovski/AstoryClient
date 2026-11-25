# Copyright (c) 2026 @sitxovski (Discord)
# _probe_item207_material.py — полный разбор материалов предмета itm1_n_98_othr207_00:
# режим смешивания, параметры, какой материал реально стоит на слотах меша.
#
# Без этого правка альфы у текстуры бессмысленна: если материал непрозрачный, восстановленный
# канал никуда не пойдёт и белый фон останется.
import unreal

MATS = [
    "/Game/AsdaAssets/Models/Items/itm1_n_98_othr207_00/Materials/itm1_n_98_othr207_00",
    "/Game/AsdaAssets/Models/Items/itm1_n_98_othr207_00/Materials/itm1_n_98_othr207_00_blend",
]
MESH_DIR = "/Game/AsdaAssets/Models/Items/itm1_n_98_othr207_00"


def main():
    for p in MATS:
        m = unreal.EditorAssetLibrary.load_asset(p)
        unreal.log("=== %s -> %s" % (p, None if m is None else m.get_class().get_name()))
        if m is None:
            continue
        if isinstance(m, unreal.MaterialInstance):
            parent = m.get_editor_property("parent")
            unreal.log("   родитель: %s" % (parent.get_path_name() if parent else None))
            ov = m.get_editor_property("base_property_overrides")
            unreal.log("   override blend: %s (%s)  opacity_mask_clip: %s (%s)  two_sided: %s (%s)" % (
                ov.get_editor_property("blend_mode"), ov.get_editor_property("override_blend_mode"),
                ov.get_editor_property("opacity_mask_clip_value"), ov.get_editor_property("override_opacity_mask_clip_value"),
                ov.get_editor_property("two_sided"), ov.get_editor_property("override_two_sided")))
            for tp in m.get_editor_property("texture_parameter_values"):
                v = tp.get_editor_property("parameter_value")
                unreal.log("   тексура %s = %s" % (tp.get_editor_property("parameter_info").get_editor_property("name"),
                                                   v.get_path_name() if v else None))
            for sp in m.get_editor_property("scalar_parameter_values"):
                unreal.log("   скаляр %s = %s" % (sp.get_editor_property("parameter_info").get_editor_property("name"),
                                                  sp.get_editor_property("parameter_value")))
        elif isinstance(m, unreal.Material):
            unreal.log("   blend=%s  shading=%s" % (m.get_editor_property("blend_mode"),
                                                    m.get_editor_property("shading_model")))

    for a in unreal.EditorAssetLibrary.list_assets(MESH_DIR, recursive=True, include_folder=False):
        obj = unreal.EditorAssetLibrary.load_asset(a)
        if isinstance(obj, unreal.StaticMesh):
            unreal.log("=== меш %s" % a)
            for i, sm in enumerate(obj.get_editor_property("static_materials")):
                mi = sm.get_editor_property("material_interface")
                unreal.log("   слот %d: %s = %s" % (i, sm.get_editor_property("material_slot_name"),
                                                    mi.get_path_name() if mi else None))
    unreal.log("=== ГОТОВО ===")


# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
