# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_premcard_buttons.py — РАЗОВЫЙ ЗОНД: состав кнопок карточки витрины 6000 (скин/иконка/размер).
import unreal
EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder
A = "/Game/AsdaAssets/UI/Player_UI/Premium/WBP_SHELLITEM_STORE_WIN"
OUT = r"@@ASTORY_DEV_ROOT@@\Reports\_premcard_buttons.txt"

def walk(w, depth, lines, want):
    if w is None: return
    n = w.get_name()
    if any(k in n for k in want):
        s = w.get_editor_property("slot")
        geo = ""
        if isinstance(s, unreal.CanvasPanelSlot):
            o = s.get_editor_property("layout_data").get_editor_property("offsets")
            geo = "off(L%.0f T%.0f R%.0f B%.0f)" % (o.left, o.top, o.right, o.bottom)
        extra = ""
        if isinstance(w, unreal.Image):
            br = w.get_editor_property("brush")
            res = br.get_editor_property("resource_object")
            extra = " tex=%s size=%s" % (res.get_name() if res else "-",
                br.get_editor_property("image_size"))
        if isinstance(w, unreal.TextBlock):
            extra = " text=<%s>" % w.get_text()
        if isinstance(w, unreal.Button):
            extra = " (кнопка)"
        lines.append("%s%s [%s] vis=%s %s%s" % ("  "*depth, n, type(w).__name__, w.get_visibility(), geo, extra))
    if isinstance(w, unreal.PanelWidget):
        for i in range(w.get_children_count()):
            walk(w.get_child_at(i), depth+1, lines, want)

lines = []
bp = EAL.load_asset(A)
walk(B.get_root_canvas(bp), 0, lines, ["SHELLITEM_POP00", "SHELLITEM_POP01"])
open(OUT, "w", encoding="utf-8").write("\n".join(lines))
unreal.log("[зонд-кнопки] %d строк -> %s" % (len(lines), OUT))
