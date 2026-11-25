# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# Пробник: полный структурный дамп деревьев окон отряда PARTYTAKE_WIN (2250) и PARTYBOX_WIN (2245).
#   Закон [[full-structural-analysis-always]]: перед правкой — дамп КАЖДОГО контрола.
import unreal

OUT = r"@@ASTORY_DEV_ROOT@@\Reports\_party_wbp_dump.txt"
PATHS = [
    "/Game/AsdaAssets/UI/Player_UI/Social/WBP_PARTYTAKE_WIN.WBP_PARTYTAKE_WIN",
    "/Game/AsdaAssets/UI/Player_UI/Social/WBP_PARTYBOX_WIN.WBP_PARTYBOX_WIN",
]

def slot_str(w):
    try:
        o = w.slot.get_editor_property("offsets")
        a = w.slot.get_editor_property("anchors")
        return "off=(%.0f,%.0f,%.0f,%.0f) anch=(%.2f,%.2f)-(%.2f,%.2f) z=%s" % (
            o.left, o.top, o.right, o.bottom,
            a.minimum.x, a.minimum.y, a.maximum.x, a.maximum.y,
            w.slot.get_editor_property("z_order"))
    except Exception:
        return ""

def walk(w, depth, lines):
    if not w:
        return
    lines.append("%s%-42s [%s] vis=%s %s" % (
        "  " * depth, w.get_name(), w.get_class().get_name(),
        str(w.get_editor_property("visibility")).split(".")[-1], slot_str(w)))
    try:
        n = w.get_children_count()
    except Exception:
        n = 0
    for i in range(n):
        walk(w.get_child_at(i), depth + 1, lines)

def main():
    lines = []
    for p in PATHS:
        lines.append("=" * 100)
        lines.append("АССЕТ: " + p)
        bp = unreal.load_object(None, p)
        if not bp:
            lines.append("  НЕ ЗАГРУЗИЛСЯ")
            continue
        root = None
        try:
            root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
        except Exception as e:
            lines.append("  get_root_canvas: " + str(e))
        if not root:
            wt = bp.get_editor_property("widget_tree")
            root = wt.get_editor_property("root_widget") if wt else None
        if not root:
            lines.append("  КОРЕНЬ НЕ ПОЛУЧЕН")
            continue
        walk(root, 1, lines)
    txt = "\n".join(lines)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(txt)
    unreal.log("[PARTY] дамп записан: %s (строк %d)" % (OUT, len(lines)))

if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
