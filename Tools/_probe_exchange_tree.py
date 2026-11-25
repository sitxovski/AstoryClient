# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# ★ Живой дамп ДЕРЕВА окна аукциона в TSV: имя, класс, родитель, глубина, видимость, слот, zorder.
#   Файл: @@ASTORY_DEV_ROOT@@\Reports\exchange_tree.tsv
import unreal

ASSET = "/Game/AsdaAssets/UI/Player_UI/Trade/WBP_EXCHANGE_WIN"
OUT = r"@@ASTORY_DEV_ROOT@@\Reports\exchange_tree.tsv"


def main():
    bp = unreal.EditorAssetLibrary.load_asset(ASSET)
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    rows = []

    def slot(w):
        try:
            s = w.get_editor_property("slot")
            if isinstance(s, unreal.CanvasPanelSlot):
                o = s.get_editor_property("layout_data").get_editor_property("offsets")
                a = s.get_editor_property("layout_data").get_editor_property("anchors")
                return ("%.1f\t%.1f\t%.1f\t%.1f" % (o.left, o.top, o.right, o.bottom),
                        "%.2f,%.2f" % (a.minimum.x, a.minimum.y), s.get_editor_property("z_order"))
        except Exception:                                    # noqa: BLE001
            pass
        return ("\t\t\t", "", 0)

    def walk(w, parent, d):
        off, anc, z = slot(w)
        rows.append("%s\t%s\t%s\t%d\t%s\t%s\t%s\t%d" % (
            w.get_name(), w.get_class().get_name(), parent, d,
            str(w.get_editor_property("visibility")).split(".")[-1].split(":")[0], off, anc, z))
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                c = w.get_child_at(i)
                if c:
                    walk(c, w.get_name(), d + 1)

    walk(root, "", 0)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("name\tclass\tparent\tdepth\tvis\tL\tT\tW\tH\tanchor\tz\n")
        f.write("\n".join(rows))
    unreal.log("[дерево] строк %d → %s" % (len(rows), OUT))


if __name__ == "__main__":
    main()
