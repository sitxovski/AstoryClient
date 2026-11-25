# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# Проверка окна «Взять рыболовную снасть» (JUI 15900) БЕЗ запуска игры:
#   1) все ли 71 узел поддерева `Inven_Fishrod_*` есть в WBP_INVEN_WIN и как они называются;
#   2) есть ли в DA_JuiWindowMeta ручка перетаскивания с целью `Inven_Fishrod_Win`
#      (от неё зависят и драг, и сохранение позиции окна в ini).
# ЗАПУСК: Tools\run_headless.bat Tools\_probe_fishrod_widgets.py
import unreal

EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder


def main():
    bp = EAL.load_asset("/Game/AsdaAssets/UI/Player_UI/Inventory/WBP_INVEN_WIN")
    if not bp:
        unreal.log_error("[снасть] WBP_INVEN_WIN не найден")
        return
    lines = [l for l in B.dump_widget_tree(bp).split("\n") if "Fishrod" in l]
    unreal.log("[снасть] узлов Inven_Fishrod_* в WBP_INVEN_WIN: %d" % len(lines))
    for l in lines:
        unreal.log("[снасть]   " + l.strip())

    meta = EAL.load_asset("/Game/AsdaAssets/UI/DA_JuiWindowMeta")
    if not meta:
        unreal.log_error("[снасть] DA_JuiWindowMeta не найден")
        return
    for w in meta.get_editor_property("windows"):
        if str(w.get_editor_property("window_id")) != "INVEN_WIN":
            continue
        for h in w.get_editor_property("drag_handles"):
            tgt = str(h.get_editor_property("target_control"))
            if "Fishrod" not in tgt:
                continue
            unreal.log("[снасть] РУЧКА: виджет=%s цель=%s idnum=%s корень=%s окно=%s виджеты цели=%s" % (
                h.get_editor_property("handle_widget"), tgt,
                h.get_editor_property("target_id_num"),
                h.get_editor_property("target_is_window_root"),
                h.get_editor_property("target_window_id"),
                [str(x) for x in h.get_editor_property("target_widgets")]))


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__":
    main()
