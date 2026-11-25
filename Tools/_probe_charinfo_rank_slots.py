# Copyright (c) 2026 @sitxovski (Discord)
# _probe_charinfo_rank_slots.py — дамп слотов узлов фракции окна 900 (925/926/927 и соседей) в WBP_CHARAINFO_WIN.
#   Только чтение (пачка 15, расширение поля званий 926, 08.11.2026). Побочных эффектов при импорте нет.
import unreal

ASSET = "/Game/AsdaAssets/UI/Player_UI/Character/WBP_CHARAINFO_WIN"   # окно 900


def dump(w, d, path, on):
    """Печать узла со слотом (смещения, якоря, выравнивание) и всех потомков; on — внутри ветки фракции."""
    nm = w.get_name()
    on = on or nm.startswith("CHARAINFO_CAMPWAR")
    if on:
        extra = ""
        try:
            s = w.get_editor_property("slot")
            if isinstance(s, unreal.CanvasPanelSlot):
                o = s.get_offsets(); a = s.get_anchors(); al = s.get_alignment()
                extra = " off=(%.1f,%.1f,%.1f,%.1f) anch=(%.2f,%.2f,%.2f,%.2f) align=(%.2f,%.2f) auto=%s" % (
                    o.left, o.top, o.right, o.bottom, a.minimum.x, a.minimum.y, a.maximum.x, a.maximum.y, al.x, al.y, s.get_auto_size())
            elif s:
                extra = " slot=%s" % s.get_class().get_name()
        except Exception as e:
            extra = " slot?%s" % e
        try:
            if isinstance(w, unreal.TextBlock):
                extra += " just=%s clip=%s" % (w.get_editor_property("justification"), w.get_editor_property("clipping"))
        except Exception:
            pass
        print("RANKDUMP " + "  " * d + "%s [%s]%s" % (nm, w.get_class().get_name(), extra))
    if isinstance(w, unreal.PanelWidget):
        for i in range(w.get_children_count()):
            dump(w.get_child_at(i), d + 1, path + "/" + nm, on)


def main():
    bp = unreal.EditorAssetLibrary.load_asset(ASSET)
    if not bp:
        print("RANKDUMP НЕТ АССЕТА"); print("ЗАВЕРШЕНО"); return
    dump(unreal.AsdaUmgBuilder.get_root_canvas(bp), 0, "", False)
    print("ЗАВЕРШЕНО")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
