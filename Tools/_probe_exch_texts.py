# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# ★ Живой дамп ТЕКСТОВ и ЦВЕТОВ окна аукциона (WBP_EXCHANGE_WIN) — проверка жалоб владельца
#   п.4/п.5/п.7 (нет надписей «Цена», «Золото», «Цена продажи», «Время (Дней)», «Налог»;
#   подпись кнопки `EXCH_SELDEL`).
#   Файл: @@ASTORY_DEV_ROOT@@\Reports\exchange_texts.tsv
import unreal

ASSET = "/Game/AsdaAssets/UI/Player_UI/Trade/WBP_EXCHANGE_WIN"
OUT = r"@@ASTORY_DEV_ROOT@@\Reports\exchange_texts.tsv"


def main():
    bp = unreal.EditorAssetLibrary.load_asset(ASSET)
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    rows = []

    def col(c):
        try:
            return "%.3f,%.3f,%.3f,%.3f" % (c.r, c.g, c.b, c.a)
        except Exception:                                    # noqa: BLE001
            return "?"

    def scol(sc):
        try:
            return col(sc.get_editor_property("specified_color"))
        except Exception:                                    # noqa: BLE001
            return "?"

    def brush(b):
        try:
            ro = b.get_editor_property("resource_object")
            uv = b.get_editor_property("uv_region")
            tint = b.get_editor_property("tint_color")
            return "%s|uv=%.3f,%.3f-%.3f,%.3f|tint=%s|valid=%s" % (
                ro.get_name() if ro else "-",
                uv.min.x, uv.min.y, uv.max.x, uv.max.y, scol(tint),
                b.get_editor_property("draw_as"))
        except Exception as e:                               # noqa: BLE001
            return "err:%s" % e

    def walk(w, d):
        n = w.get_name()
        cls = w.get_class().get_name()
        vis = str(w.get_editor_property("visibility")).split(".")[-1].split(":")[0]
        info = ""
        if isinstance(w, unreal.TextBlock):
            info = "TEXT=[%s]\tCOL=%s" % (w.get_text(), scol(w.get_editor_property("color_and_opacity")))
        elif isinstance(w, unreal.EditableText):
            info = "TEXT=[%s]\tCOL=%s" % (w.get_text(), scol(w.get_editor_property("widget_style").get_editor_property("color_and_opacity"))
                                          if False else "-")
        elif isinstance(w, unreal.Button):
            st = w.get_editor_property("widget_style")
            info = "FG n/h/p=%s | %s | %s\tBRn=%s\tBRp=%s" % (
                scol(st.get_editor_property("normal_foreground")),
                scol(st.get_editor_property("hovered_foreground")),
                scol(st.get_editor_property("pressed_foreground")),
                brush(st.get_editor_property("normal")),
                brush(st.get_editor_property("pressed")))
        elif isinstance(w, unreal.Image):
            info = brush(w.get_editor_property("brush"))
        rows.append("%s\t%s\t%d\t%s\t%s" % (n, cls, d, vis, info))
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                c = w.get_child_at(i)
                if c:
                    walk(c, d + 1)

    walk(root, 0)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("name\tclass\tdepth\tvis\tinfo\n")
        f.write("\n".join(rows))
    unreal.log("[тексты] строк %d -> %s" % (len(rows), OUT))


if __name__ == "__main__":
    main()
