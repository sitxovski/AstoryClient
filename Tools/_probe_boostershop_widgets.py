# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_boostershop_widgets.py — РАЗОВЫЙ ДИАГНОСТИЧЕСКИЙ ДАМП (не часть пайплайна).
#   Печатает дерево виджетов WBP_BOOSTER_SHOP_WIN (окно 15300) и WBP_SHELLITEM_MsgBase (460),
#   а также запись 15300 в DA_JuiWindowMeta (ручки перетаскивания).
#   Нужен, чтобы ДО правок знать точный состав ассета (закон full-structural-analysis-always).
import unreal

EAL = unreal.EditorAssetLibrary
TARGETS = [
    ("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_BOOSTER_SHOP_WIN", "BOOSTER_SHOP_WIN"),
    ("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_SHELLITEM_MsgBase", "SHELLITEM_MsgBase"),
]


def walk(w, depth, out):
    try:
        kids = w.get_all_children()
    except Exception:
        return
    for c in kids:
        out.append((depth, c.get_name(), c.get_class().get_name()))
        walk(c, depth + 1, out)


for path, win in TARGETS:
    bp = EAL.load_asset(path)
    if bp is None:
        unreal.log_error("НЕТ АССЕТА %s" % path)
        continue
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    out = []
    if root is not None:
        out.append((0, root.get_name(), root.get_class().get_name()))
        walk(root, 1, out)
    unreal.log("=== %s: виджетов %d ===" % (win, len(out)))
    for d, n, c in out:
        unreal.log("W| %s%s  [%s]" % ("  " * d, n, c))

meta = EAL.load_asset("/Game/AsdaAssets/UI/DA_JuiWindowMeta")
if meta:
    for m in meta.get_editor_property("windows"):
        wid = str(m.get_editor_property("window_id"))
        if wid in ("BOOSTER_SHOP_WIN", "SHELLITEM_MsgBase"):
            hs = list(m.get_editor_property("drag_handles"))
            unreal.log("META| %s: ручек %d" % (wid, len(hs)))
            for h in hs:
                unreal.log("META|   handle=%s target=%s idnum=%s root=%s fixed=%s" % (
                    h.get_editor_property("handle_widget"),
                    h.get_editor_property("target_control"),
                    h.get_editor_property("target_id_num"),
                    h.get_editor_property("target_is_window_root"),
                    h.get_editor_property("original_bug_fixed")))
unreal.log("=== ПРОБА ЗАВЕРШЕНА ===")
