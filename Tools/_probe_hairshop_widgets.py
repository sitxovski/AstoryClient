# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_hairshop_widgets.py — РАЗОВЫЙ ЗОНД: полное дерево WBP_HAIRSHOP_WIN (окно 15000).
import os
import unreal
EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder
# ★ Ассет и файл вывода переопределяются переменными окружения — чтобы этим ЖЕ зондом
#   смотреть любое окно и не плодить второй скрипт.
A = os.environ.get("ASDA_PROBE_ASSET", "/Game/AsdaAssets/UI/Player_UI/Trade/WBP_HAIRSHOP_WIN")
OUT = r"@@ASTORY_DEV_ROOT@@\Reports\_hairshop_wbp_tree.txt"

def walk(w, depth, lines):
    if w is None: return
    s = w.get_editor_property("slot")
    geo = ""
    if isinstance(s, unreal.CanvasPanelSlot):
        ld = s.get_editor_property("layout_data")
        o = ld.get_editor_property("offsets"); a = ld.get_editor_property("anchors")
        geo = "off(L%.0f T%.0f R%.0f B%.0f) anc(%.2f %.2f) z%d" % (
            o.left, o.top, o.right, o.bottom, a.minimum.x, a.minimum.y, s.get_editor_property("z_order"))
    txt = " text=<%s>" % w.get_text() if isinstance(w, unreal.TextBlock) else ""
    lines.append("%s%s [%s] vis=%s %s%s" % ("  "*depth, w.get_name(), type(w).__name__, w.get_visibility(), geo, txt))
    if isinstance(w, unreal.PanelWidget):
        for i in range(w.get_children_count()):
            walk(w.get_child_at(i), depth+1, lines)

lines = ["ASSET %s" % A]
bp = EAL.load_asset(A)
walk(B.get_root_canvas(bp), 0, lines)
# сводка по классам
from collections import Counter
c = Counter()
for l in lines[1:]:
    k = l.split("[",1)[1].split("]",1)[0] if "[" in l else "?"
    c[k] += 1
lines.append("--- ИТОГО узлов %d: %s" % (len(lines)-1, dict(c)))
open(OUT, "w", encoding="utf-8").write("\n".join(lines))
unreal.log("[зонд-парикмахерская] %d строк -> %s" % (len(lines), OUT))
