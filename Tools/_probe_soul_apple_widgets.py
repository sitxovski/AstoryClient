# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# Пробник (волна 1-Б, 06.10.2026): полный структурный дамп деревьев окон SOULMATE_POP_WIN (6500, панель
#   «Яблоко Дружбы» 6689…6699) и GOLD_roullete_WIN (67000). Только ЧТЕНИЕ ассетов — ничего не сохраняет.
#   Закон [[full-structural-analysis-always]]: перед правкой кода — дамп КАЖДОГО контрола.
import unreal

OUT = r"@@ASTORY_DEV_ROOT@@\Reports\owner_1006\_wave1B_wbp_dump.txt"
PATHS = [
    "/Game/AsdaAssets/UI/Player_UI/Soulmate/WBP_SOULMATE_POP_WIN.WBP_SOULMATE_POP_WIN",
    "/Game/AsdaAssets/UI/Player_UI/Soulmate/WBP_GOLD_roullete_WIN.WBP_GOLD_roullete_WIN",
    "/Game/AsdaAssets/UI/Player_UI/Social/WBP_PARTYDISPERSION.WBP_PARTYDISPERSION",
]

def slot_str(w):
    try:
        o = w.slot.get_editor_property("offsets")
        return "off=(%.0f,%.0f,%.0f,%.0f) z=%s" % (o.left, o.top, o.right, o.bottom, w.slot.get_editor_property("z_order"))
    except Exception:
        return ""

def anim_str(w):
    try:
        if w.get_class().get_name() != "AsdaAnimatedPlane":
            return ""
        return " ANIM frames=%s interval=%s loop=%s" % (w.get_editor_property("total_frame"),
            w.get_editor_property("frame_interval_ms"), w.get_editor_property("b_loop"))
    except Exception as e:
        return " ANIM ?" + str(e)

def walk(w, depth, lines):
    if not w:
        return
    lines.append("%s%-42s [%s] vis=%s %s%s" % ("  " * depth, w.get_name(), w.get_class().get_name(),
        str(w.get_editor_property("visibility")).split(".")[-1], slot_str(w), anim_str(w)))
    try:
        n = w.get_children_count()
    except Exception:
        n = 0
    for i in range(n):
        walk(w.get_child_at(i), depth + 1, lines)

def main():
    lines = []
    for p in PATHS:
        lines.append("=" * 100)
        lines.append("АССЕТ: " + p)
        bp = unreal.load_object(None, p)
        if not bp:
            lines.append("  НЕ ЗАГРУЗИЛСЯ")
            continue
        root = None
        try:
            root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
        except Exception as e:
            lines.append("  get_root_canvas: " + str(e))
        if not root:
            wt = bp.get_editor_property("widget_tree")
            root = wt.get_editor_property("root_widget") if wt else None
        if not root:
            lines.append("  КОРЕНЬ НЕ ПОЛУЧЕН")
            continue
        walk(root, 1, lines)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    unreal.log("[WAVE1B] дамп записан: %s (строк %d) ЗАВЕРШЕНО" % (OUT, len(lines)))

if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
