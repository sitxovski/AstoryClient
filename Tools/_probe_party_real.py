# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# ПРАВИЛЬНЫЙ пробник: снимает НАСТОЯЩИЕ значения из СОХРАНЁННОГО ассета WBP_PARTY_WIN.
# ⚠ Прошлый пробник читал slot.get_editor_property("offsets") — такого свойства у
#   UCanvasPanelSlot в UE5 НЕТ, исключение глоталось try/except, и геометрия печаталась пустой.
#   Правильный путь — layout_data.offsets (FAnchorData).
import unreal

OUT = r"@@ASTORY_DEV_ROOT@@\Reports\_party_real_dump.txt"
ASSET = "/Game/AsdaAssets/UI/Player_UI/Social/WBP_PARTY_WIN.WBP_PARTY_WIN"
WANT_PREFIX = ("RootCanvas", "Party_CBase", "Party_CBg", "Party_LBase", "Party_RBase",
               "Party_RbaseW", "Party_LBg", "Party_RBg", "PARTY_WIN00", "Partyer_LBase00",
               "Partyer_CBg00", "Partyer_CBase00", "Partyer_RBase00",
               "PARTY_HP00", "PARTY_MP00", "PARTY_HP01", "Party_MemSelect00",
               "Party_TargetShare00", "PARTY_ICON00", "Party_WinH_grp", "Party_WinN_grp",
               "Party_LBaseW", "PARTY_WIN_0_HIT", "PARTY_TOOLBUT00_Text", "PARTY_EXITBUT_Text",
               "Party_Txt")


def collect(w, acc):
    if not w:
        return
    acc.append(w)
    try:
        n = w.get_children_count()
    except Exception:
        n = 0
    for i in range(n):
        collect(w.get_child_at(i), acc)


def geom(w):
    try:
        ld = w.slot.get_editor_property("layout_data")
        o = ld.offsets
        a = ld.anchors
        return "off L=%.1f T=%.1f R(шир)=%.1f B(выс)=%.1f anch=(%.2f,%.2f)-(%.2f,%.2f)" % (
            o.left, o.top, o.right, o.bottom,
            a.minimum.x, a.minimum.y, a.maximum.x, a.maximum.y)
    except Exception as e:
        return "геометрия недоступна: %s" % e


def main():
    bp = unreal.load_object(None, ASSET)
    root = None
    try:
        root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    except Exception:
        pass
    if not root:
        wt = bp.get_editor_property("widget_tree")
        root = wt.get_editor_property("root_widget") if wt else None
    acc = []
    collect(root, acc)
    lines = ["АССЕТ (сохранённый на диске): " + ASSET,
             "всего узлов: %d" % len(acc), ""]
    for w in acc:
        nm = w.get_name()
        if not any(nm.startswith(p) for p in WANT_PREFIX):
            continue
        vis = str(w.get_editor_property("visibility")).split(".")[-1]
        extra = ""
        try:
            ro = w.get_editor_property("render_opacity")
            if abs(float(ro) - 1.0) > 0.001:
                extra += " RenderOpacity=%.3f" % ro
        except Exception:
            pass
        try:
            c = w.get_editor_property("color_and_opacity")
            if abs(c.r-1)>0.001 or abs(c.g-1)>0.001 or abs(c.b-1)>0.001 or abs(c.a-1)>0.001:
                extra += " ColorAndOpacity=(%.2f,%.2f,%.2f,%.2f)" % (c.r,c.g,c.b,c.a)
        except Exception:
            pass
        try:
            b = w.get_editor_property("brush")
            t = b.tint_color.specified_color
            if abs(t.r-1)>0.001 or abs(t.g-1)>0.001 or abs(t.b-1)>0.001 or abs(t.a-1)>0.001:
                extra += " BrushTint=(%.2f,%.2f,%.2f,%.2f)" % (t.r,t.g,t.b,t.a)
        except Exception:
            pass
        if w.get_class().get_name() == "TextBlock":
            try:
                extra = " кегль=%s" % w.get_editor_property("font").size
            except Exception:
                pass
        lines.append("%-30s [%-14s] vis=%-28s %s%s" % (nm, w.get_class().get_name(), vis, geom(w), extra))
    txt = "\n".join(lines)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(txt)
    unreal.log("[реальный-дамп] записан %s (строк %d)" % (OUT, len(lines)))


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
