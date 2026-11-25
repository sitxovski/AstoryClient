# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# Крестик окна отряда `PARTY_MINIBUT` (UCheckBox, скин CloseBtn_18_Party).
#   Скин: SS0 «обычный»  UV (0.414,0.469)-(0.449,0.504)
#         SS2 «нажатый»  UV (0.916,0.451)-(0.951,0.486)
#   Печатаем UV каждой кисти стиля, чтобы понять, какой кадр импортёр положил в «обычное».
import unreal

OUT = r"@@ASTORY_DEV_ROOT@@\Reports\_party_minibut.txt"
ASSET = "/Game/AsdaAssets/UI/Player_UI/Social/WBP_PARTY_WIN.WBP_PARTY_WIN"


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


def brush_info(b):
    try:
        return str(b).replace("\n", " ")[:400]
    except Exception as e:
        return "нечитаемо: %s" % e


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
    lines = []
    for w in acc:
        if w.get_name() not in ("PARTY_MINIBUT", "PARTY_MINIBUT_H"):
            continue
        lines.append("%s [%s]" % (w.get_name(), w.get_class().get_name()))
        try:
            st = w.get_editor_property("widget_style")
            for prop in ("unchecked_image", "unchecked_hovered_image", "unchecked_pressed_image",
                         "checked_image", "checked_hovered_image", "checked_pressed_image"):
                try:
                    lines.append("   %-26s %s" % (prop, brush_info(st.get_editor_property(prop))))
                except Exception as e:
                    lines.append("   %-26s нет свойства (%s)" % (prop, e))
        except Exception as e:
            lines.append("   widget_style недоступен: %s" % e)
        try:
            st2 = w.get_editor_property("style")
            for prop in ("normal", "hovered", "pressed"):
                try:
                    lines.append("   button.%-18s %s" % (prop, brush_info(st2.get_editor_property(prop))))
                except Exception:
                    pass
        except Exception:
            pass
    txt = "\n".join(lines) if lines else "узлы не найдены"
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(txt)
    unreal.log("[крестик] записан %s" % OUT)


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
