# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""Полный структурный дамп WBP_SOWELL_SELBOX и WBP_SOWEL_Tooltip
(закон full-structural-analysis-always) — нужен для точного биндинга контролов окна выбора камня.
Запуск: Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\_dump_sowell_selbox.py"""
import unreal

PATHS = [
    "/Game/AsdaAssets/UI/Player_UI/Craft/WBP_SOWELL_SELBOX",
    "/Game/AsdaAssets/UI/Player_UI/Common/WBP_SOWEL_Tooltip",
]


def L(m):
    unreal.log("[SELBOX] " + str(m))


def dump(path):
    bp = unreal.EditorAssetLibrary.load_asset(path)
    L("=== %s: %s" % (path, "OK" if bp else "НЕ ЗАГРУЗИЛСЯ"))
    if not bp:
        return
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    total = [0]

    def walk(w, d=0):
        total[0] += 1
        extra = ""
        try:
            slot = w.get_editor_property("slot")
            if isinstance(slot, unreal.CanvasPanelSlot):
                off = slot.get_offsets()
                extra = " off=(%.0f,%.0f %.0fx%.0f) z=%d" % (
                    off.left, off.top, off.right, off.bottom, slot.get_editor_property("z_order"))
        except Exception:
            pass
        vis = ""
        try:
            vis = " vis=%s" % str(w.get_editor_property("visibility"))
        except Exception:
            pass
        L("  " * d + "%s [%s]%s%s" % (w.get_name(), w.get_class().get_name(), extra, vis))
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                walk(w.get_child_at(i), d + 1)

    walk(root)
    L("=== ВСЕГО УЗЛОВ: %d" % total[0])


def main():
    for p in PATHS:
        dump(p)


if __name__ == '__main__':
    main()
