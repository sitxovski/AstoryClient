# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_booster_part2.py — РАЗОВЫЙ ДИАГНОСТИЧЕСКИЙ ДАМП (не часть пайплайна).
#   Печатает поддерево подложки 15599 (Booster_Shop_Part2) окна 15300 с геометрией слотов,
#   чтобы понять, из чего собрана «белая рамка» и что именно надо растягивать.
import unreal

EAL = unreal.EditorAssetLibrary
PATH = "/Game/AsdaAssets/UI/Player_UI/Trade/WBP_BOOSTER_SHOP_WIN"


def slot_info(w):
    try:
        s = w.slot
    except Exception:
        return ""
    if s is None:
        return ""
    try:
        lay = s.get_editor_property("layout_data")
        off = lay.offsets
        anc = lay.anchors
        return "off=(%.0f,%.0f,%.0f,%.0f) anc=(%.2f,%.2f)-(%.2f,%.2f) autosize=%s zorder=%s" % (
            off.left, off.top, off.right, off.bottom,
            anc.minimum.x, anc.minimum.y, anc.maximum.x, anc.maximum.y,
            s.get_editor_property("auto_size"), s.get_editor_property("z_order"))
    except Exception as e:
        return "slot=%s" % s.get_class().get_name()


def walk(w, depth, out):
    try:
        kids = w.get_all_children()
    except Exception:
        return
    for c in kids:
        out.append((depth, c.get_name(), c.get_class().get_name(), slot_info(c)))
        walk(c, depth + 1, out)


bp = EAL.load_asset(PATH)
root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
allw = []
allw.append((0, root.get_name(), root.get_class().get_name(), ""))
walk(root, 1, allw)
unreal.log("=== ВСЕГО виджетов %d ===" % len(allw))
# 1) всё, что содержит Part2 в имени, и всё, что лежит НЕПОСРЕДСТВЕННО после него
for d, n, c, s in allw:
    ln = n.lower()
    if "part2" in ln or "part1" in ln or "part3" in ln:
        unreal.log("P2| %s%s  [%s]  %s" % ("  " * d, n, c, s))
unreal.log("=== ПОЛНОЕ ДЕРЕВО ===")
for d, n, c, s in allw:
    unreal.log("W| %s%s  [%s]  %s" % ("  " * d, n, c, s))
unreal.log("=== ПРОБА ЗАВЕРШЕНА ===")
