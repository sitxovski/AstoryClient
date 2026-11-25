# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""30.10.2026 (StrengthPart целиком): полный структурный дамп окон StrengthPart
(47000/48099/49399/49400/49510/49530/49540/49600/49800/49900/49910/49940) — закон full-structural-analysis-always. Только чтение, ничего не сохраняет."""
import unreal

PATHS = ["/Game/AsdaAssets/UI/Player_UI/Craft/WBP_Strength_Part_Win"]


def L(m):
    unreal.log("[SPW] " + str(m))


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
            vis = " vis=%s" % str(w.get_editor_property("visibility")).replace("SlateVisibility.", "")
        except Exception:
            pass
        txt = ""
        try:
            if isinstance(w, unreal.TextBlock):
                txt = " text=«%s»" % str(w.get_editor_property("text"))
        except Exception:
            pass
        img = ""
        try:
            if isinstance(w, unreal.Image):
                br = w.get_editor_property("brush")
                res = br.get_editor_property("resource_object")
                uv = br.get_editor_property("uv_region")
                img = " res=%s uv=(%.3f,%.3f..%.3f,%.3f)" % (
                    res.get_name() if res else "-", uv.min.x, uv.min.y, uv.max.x, uv.max.y)
        except Exception:
            pass
        L("  " * d + "%s [%s]%s%s%s%s" % (w.get_name(), w.get_class().get_name(), extra, vis, txt, img))
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                walk(w.get_child_at(i), d + 1)

    walk(root)
    L("=== ВСЕГО УЗЛОВ: %d" % total[0])


def main():
    for p in PATHS:
        dump(p)
    L("ЗАВЕРШЕНО")


if __name__ == '__main__':
    main()
