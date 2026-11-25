# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_premshop_widgets.py — РАЗОВЫЙ ЗОНД: дерево WBP_SHELLITEM_STORE_WIN (окно 6000).
#   Печатает имя, класс, видимость и слот-геометрию каждого узла верхних уровней.
import unreal, os

EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder
ASSETS = [
    "/Game/AsdaAssets/UI/Player_UI/Premium/WBP_SHELLITEM_STORE_WIN",
    "/Game/AsdaAssets/UI/Player_UI/Trade/WBP_HAIRSHOP_WIN",
]
OUT = r"@@ASTORY_DEV_ROOT@@\Reports\_premshop_wbp_tree.txt"

def walk(w, depth, lines, maxdepth):
    if w is None or depth > maxdepth:
        return
    s = w.get_editor_property("slot")
    geo = ""
    if isinstance(s, unreal.CanvasPanelSlot):
        ld = s.get_editor_property("layout_data")
        o = ld.get_editor_property("offsets")
        a = ld.get_editor_property("anchors")
        geo = "off(L%.0f T%.0f R%.0f B%.0f) anc(%.2f %.2f) z%d" % (
            o.left, o.top, o.right, o.bottom,
            a.minimum.x, a.minimum.y, s.get_editor_property("z_order"))
    txt = ""
    if isinstance(w, unreal.TextBlock):
        txt = " text=<%s>" % w.get_text()
    lines.append("%s%s [%s] vis=%s %s%s" % (
        "  " * depth, w.get_name(), type(w).__name__, w.get_visibility(), geo, txt))
    if isinstance(w, unreal.PanelWidget):
        for i in range(w.get_children_count()):
            walk(w.get_child_at(i), depth + 1, lines, maxdepth)

lines = []
for A in ASSETS:
    lines.append("=" * 100)
    lines.append("ASSET %s exists=%s" % (A, EAL.does_asset_exist(A)))
    if not EAL.does_asset_exist(A):
        continue
    bp = EAL.load_asset(A)
    root = B.get_root_canvas(bp)
    walk(root, 0, lines, 3)
with open(OUT, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))
unreal.log("[зонд] записано %d строк в %s" % (len(lines), OUT))
