# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# Проба (ТОЛЬКО ЧТЕНИЕ) окна «Hunt report» (JUI 56450): полное дерево WBP_Hunt_report_Win
#   и записи DA_JuiWindowMeta (ручка перетаскивания). Волна 2-Б, 06.10.2026.
# ЗАПУСК: python Tools/run_headless_safe.py Tools/_probe_hunt_report_widgets.py --marker "ПРОБА 56450 ЗАВЕРШЕНА"
import unreal

EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder


def main():
    bp = EAL.load_asset("/Game/AsdaAssets/UI/Player_UI/Character/WBP_Hunt_report_Win")
    if not bp:
        unreal.log_error("[56450] WBP_Hunt_report_Win не найден")
    else:
        for l in B.dump_widget_tree(bp).split("\n"):
            unreal.log("[56450] " + l.rstrip())
    meta = EAL.load_asset("/Game/AsdaAssets/UI/DA_JuiWindowMeta")
    if meta:
        for w in meta.get_editor_property("windows"):
            wid = str(w.get_editor_property("window_id"))
            if "Hunt" not in wid:
                continue
            unreal.log("[56450] META окно=%s" % wid)
            for h in w.get_editor_property("drag_handles"):
                unreal.log("[56450] META ручка=%s цель=%s" % (h.get_editor_property("handle_widget"),
                                                              h.get_editor_property("target_control")))
    unreal.log("ПРОБА 56450 ЗАВЕРШЕНА")


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__":
    main()
