# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# ★ Цвета подписи ПО КАДРАМ СКИНА, положенные импортёром в FButtonStyle кнопок-вкладок аукциона.
#   Кадр 0 = покой, 1 = наведение, 2 = НАЖАТИЕ/ВЫБРАНО (`JuiSkin.ini`: 808080 / AAAAAA / FFFFFF).
import unreal

ASSET = "/Game/AsdaAssets/UI/Player_UI/Trade/WBP_EXCHANGE_WIN"


def log(m):
    unreal.log("[стиль-вкладок] %s" % m)


def walk(root):
    st, out = [root], []
    while st:
        w = st.pop()
        if not w:
            continue
        out.append(w)
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                st.append(w.get_child_at(i))
    return out


def col(sc):
    try:
        if sc.get_editor_property("color_use_rule") != unreal.SlateColorStylingMode.USE_COLOR_SPECIFIED:
            return "UseForeground"
        c = sc.get_editor_property("specified_color")
        return "%.3f,%.3f,%.3f,%.2f" % (c.r, c.g, c.b, c.a)
    except Exception as e:                                   # noqa: BLE001
        return "?%s" % e


def main():
    bp = unreal.EditorAssetLibrary.load_asset(ASSET)
    byname = {}
    for w in walk(unreal.AsdaUmgBuilder.get_root_canvas(bp)):
        byname.setdefault(w.get_name(), w)
    for idc, n in (("EXCHANGER_RADIOBTN1", 8), ("EXCHANGER_RADIOBTN2", 7), ("EXCHMY_CateSelBtn", 2)):
        for i in range(n):
            b = byname.get("%s_OPT%d" % (idc, i))
            if not isinstance(b, unreal.Button):
                log("%s_OPT%d — НЕ КНОПКА" % (idc, i))
                continue
            st = b.get_editor_property("widget_style")
            log("%s_OPT%d  покой=%s  наведение=%s  нажатие=%s" % (
                idc, i, col(st.get_editor_property("normal_foreground")),
                col(st.get_editor_property("hovered_foreground")),
                col(st.get_editor_property("pressed_foreground"))))
            if i >= 1:
                break


if __name__ == "__main__":
    main()
