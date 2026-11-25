# Copyright (c) 2026 @sitxovski (Discord)
# _probe_object_lit_master.py — мастер-материал предметов: режим смешивания и что идёт в
# OpacityMask. Нужно, чтобы понять, дойдёт ли восстановленная альфа текстуры до картинки.
import unreal

PATHS = ["/Game/Materials/M_AsdaObjectLit"]


def main():
    for p in PATHS:
        m = unreal.EditorAssetLibrary.load_asset(p)
        unreal.log("=== %s -> %s" % (p, None if m is None else m.get_class().get_name()))
        if m is None:
            continue
        for prop in ("blend_mode", "shading_model", "two_sided", "opacity_mask_clip_value",
                     "dither_opacity_mask", "used_with_static_lighting"):
            try:
                unreal.log("   %s = %s" % (prop, m.get_editor_property(prop)))
            except Exception as exc:
                unreal.log("   %s = ? (%s)" % (prop, exc))
        try:
            exprs = unreal.MaterialEditingLibrary.get_num_material_expressions(m)
            unreal.log("   выражений: %d" % exprs)
        except Exception:
            pass
        try:
            names = unreal.MaterialEditingLibrary.get_static_switch_parameter_names(m)
            unreal.log("   статические переключатели: %s" % list(names))
        except Exception:
            pass
    unreal.log("=== ГОТОВО ===")


# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
