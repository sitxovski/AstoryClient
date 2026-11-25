# Copyright (c) 2026 @sitxovski (Discord)
# Разовый зонд: чем именно подписаны плитки карт окна 8500 — шрифт, размер, обрезка, выравнивание.
#   Нужен для дефекта «подпись подземелья длиннее плитки и затекает в соседний столбец».
import unreal

EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder
WBP = "/Game/AsdaAssets/UI/Player_UI/Social/WBP_Find_Party_WIn"


def main():
    bp = EAL.load_asset(WBP)
    root = B.get_root_canvas(bp)
    out = []

    def walk(w):
        if w is None:
            return
        n = w.get_name()
        if n.startswith("Find_Party_Map") and n.endswith("_Text") and isinstance(w, unreal.TextBlock):
            f = w.get_editor_property("font")
            sl = w.get_editor_property("slot")
            off = sl.get_offsets() if isinstance(sl, unreal.CanvasPanelSlot) else None
            out.append((n, str(f.font_object.get_name()) if f.font_object else "?", f.size,
                        str(f.typeface_font_name), str(w.get_editor_property("clipping")),
                        str(w.get_editor_property("justification")),
                        ("%.0f,%.0f,%.0f,%.0f" % (off.left, off.top, off.right, off.bottom)) if off else "?", str(w.get_editor_property("auto_wrap_text")),
                        w.get_editor_property("text")))
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                walk(w.get_child_at(i))

    walk(root)
    for r in sorted(out):
        unreal.log("[зонд-подписи] %s шрифт=%s размер=%s начертание=%s обрезка=%s выравн=%s слот=%s перенос=%s текст=%r"
                   % r)
    unreal.log("[зонд-подписи] всего %d" % len(out))


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
