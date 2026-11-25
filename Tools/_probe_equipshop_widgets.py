# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_equipshop_widgets.py — РАЗОВЫЙ ДИАГНОСТИЧЕСКИЙ ДАМП (не часть пайплайна).
#   Печатает имена ВСЕХ виджетов WBP_SHELLITEM_STORE2_WIN (6800) и WBP_HONOR_itemmall_Win (67200)
#   плюс записи этих окон в DA_JuiWindowMeta (ручки перетаскивания).
import unreal

EAL = unreal.EditorAssetLibrary
TARGETS = [
    ("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_SHELLITEM_STORE2_WIN", "SHELLITEM_STORE2_WIN"),
    ("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_HONOR_itemmall_Win", "HONOR_itemmall_Win"),
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
        if wid in ("SHELLITEM_STORE2_WIN", "HONOR_itemmall_Win"):
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
