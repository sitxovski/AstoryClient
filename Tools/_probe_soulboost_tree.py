# Copyright (c) 2026 @sitxovski (Discord)
# _probe_soulboost_tree.py — ДИАГНОСТИКА: полный дамп поддерева Soul_Boost в WBP_MINISTATE_WIN
#   (класс каждого узла, родитель, видимость, анкор/оффсеты слота, текстура браша, тинт).
#   Нужен, чтобы не гадать, почему окно 56000 не видно на экране (закон full-structural-analysis-always).
#   Запуск: Tools/run_headless.bat Tools\_probe_soulboost_tree.py
# ★ ПРАВИЛО tools-no-import-side-effects: только через main().
import unreal

WBP = "/Game/AsdaAssets/UI/Player_UI/HUD/WBP_MINISTATE_WIN"
OUT = r"@@ASTORY_DEV_ROOT@@\Reports\_soulboost_tree.txt"
PREFIXES = ("Soul_Boost", "soul_boost", "SOULBar", "SOUL_1", "SOUL_2", "SOUL_3",
            "Text_burst", "Text_gaurder", "BLACKCIRCLE", "BOLT")


def log(m):
    unreal.log_warning("[soul-probe] %s" % m)


def main():
    bp = unreal.EditorAssetLibrary.load_asset(WBP)
    if not bp:
        log("!! WBP не загрузился")
        return
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp)   # WidgetTree из Python недоступен — берём билдер порта
    if not root:
        log("!! корневой канвас не найден")
        return
    widgets = []
    parent_of = {}

    def walk(w, pname):
        widgets.append(w)
        parent_of[w.get_name()] = pname
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                walk(w.get_child_at(i), w.get_name())

    walk(root, "<root>")
    lines = []
    for w in widgets:
        n = w.get_name()
        if not any(n.startswith(p) for p in PREFIXES):
            continue
        cls = w.get_class().get_name()
        vis = str(w.get_editor_property("visibility"))
        op = w.get_editor_property("render_opacity")
        slot = w.slot
        geo = ""
        if isinstance(slot, unreal.CanvasPanelSlot):
            lay = slot.get_editor_property("layout_data")
            a = lay.anchors
            off = lay.offsets
            geo = "anchor=(%.3f,%.3f)-(%.3f,%.3f) off=(L%.0f T%.0f R%.0f B%.0f) align=(%.2f,%.2f) autosize=%s z=%d" % (
                a.minimum.x, a.minimum.y, a.maximum.x, a.maximum.y,
                off.left, off.top, off.right, off.bottom,
                lay.alignment.x, lay.alignment.y,
                slot.get_editor_property("auto_size"), slot.get_editor_property("z_order"))
        tex = ""
        if isinstance(w, unreal.Image):
            b = w.get_editor_property("brush")
            ro = b.get_editor_property("resource_object")
            tint = b.get_editor_property("tint_color").specified_color
            tex = "tex=%s size=%s%.0f%.0f tint=(%.2f,%.2f,%.2f,a%.2f) draw=%s" % (
                ro.get_name() if ro else "НЕТ", str(b.get_editor_property("image_size")),
                0.0, 0.0,
                tint.r, tint.g, tint.b, tint.a, b.get_editor_property("draw_as"))
        lines.append("%-34s %-20s parent=%-28s vis=%-28s op=%.2f %s %s"
                     % (n, cls, parent_of.get(n, "?"), vis, op, geo, tex))
    lines.sort()
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    log("узлов поддерева Soul_Boost: %d → %s" % (len(lines), OUT))


if __name__ == "__main__":
    main()
