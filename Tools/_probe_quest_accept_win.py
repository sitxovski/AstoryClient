# Copyright (c) 2026 @sitxovski (Discord)
# _probe_quest_accept_win.py — ПОЛНЫЙ структурный дамп WBP окна приёма задания (JUI 750 QUEST_Confirm_Win).
#   Окно 750: рамка 9 планов, значок «!», строка 751, заголовки 760/770, плитки 752…759,
#   подписи 761…767 / 771…777, иконки 762…768 / 772…778, кнопка «ОК» 780.
#   Запуск: Tools\run_headless.bat Tools\_probe_quest_accept_win.py
# Для КАЖДОГО узла: имя, класс, видимость, слот (offsets/anchors/alignment), кисть (текстура+UV), цвет текста.
import unreal

def L(m):
    unreal.log("[QACC] " + str(m))

PATHS = [
    "/Game/AsdaAssets/UI/Player_UI/Quest/WBP_QUEST_Confirm_Win.WBP_QUEST_Confirm_Win",
]

def slot_info(w):
    s = w.slot
    if not s:
        return "нет слота"
    out = []
    try:
        ld = s.get_editor_property("layout_data")
        o = ld.offsets
        a = ld.anchors
        al = ld.alignment
        out.append("off=(%.1f,%.1f,%.1f,%.1f)" % (o.left, o.top, o.right, o.bottom))
        out.append("anch=(%s)-(%s)" % (a.minimum, a.maximum))
        out.append("align=%s" % (al,))
    except Exception as e:
        out.append("layout?%s" % e)
    try:
        a = s.get_editor_property("anchors")
        out.append("anch=(%.3f,%.3f)-(%.3f,%.3f)" % (a.minimum.x, a.minimum.y, a.maximum.x, a.maximum.y))
    except Exception:
        pass
    try:
        al = s.get_editor_property("alignment")
        out.append("align=(%.2f,%.2f)" % (al.x, al.y))
    except Exception:
        pass
    try:
        out.append("autosize=%s" % s.get_editor_property("size_to_content"))
    except Exception:
        pass
    try:
        out.append("z=%d" % s.get_editor_property("z_order"))
    except Exception:
        pass
    return " ".join(out) if out else "слот без полей"

def extra_info(w):
    cls = w.get_class().get_name()
    out = []
    if cls == "Image":
        try:
            b = w.get_editor_property("brush")
            res = b.get_editor_property("resource_object")
            out.append("tex=%s" % (res.get_name() if res else "нет"))
            out.append("size=%s" % (b.image_size,))
            out.append("draw=%s" % b.draw_as)
            m = b.get_editor_property("margin")
            out.append("margin=(%.3f,%.3f,%.3f,%.3f)" % (m.left, m.top, m.right, m.bottom))
            t = b.get_editor_property("tint")
            out.append("tint=%s" % (t,))
        except Exception as e:
            out.append("кисть?%s" % e)
    if cls in ("TextBlock", "RichTextBlock"):
        try:
            out.append("text='%s'" % w.get_editor_property("text"))
        except Exception:
            pass
        try:
            c = w.get_editor_property("color_and_opacity").specified_color
            out.append("col=(%.3f,%.3f,%.3f,%.3f)" % (c.r, c.g, c.b, c.a))
        except Exception:
            pass
        try:
            f = w.get_editor_property("font")
            out.append("font=%s/%d" % (f.typeface_font_name, f.size))
        except Exception:
            pass
        try:
            out.append("wrap=%s/%.0f" % (w.get_editor_property("auto_wrap_text"),
                                         w.get_editor_property("wrap_text_at")))
        except Exception:
            pass
    return " ".join(out)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for p in PATHS:
        L("=" * 100)
        L("АССЕТ: " + p)
        bp = unreal.load_object(None, p)
        if not bp:
            L("  НЕ НАЙДЕН")
            continue
        root = unreal.AsdaUmgBuilder.get_root_canvas(bp)   # widget_tree не экспонирован — канвас через билдер

        def walk(w, d):
            if not w:
                return
            L("  " + "  " * d + "%-34s [%-16s] vis=%-22s %s | %s" % (
                w.get_name(), w.get_class().get_name(),
                str(w.get_editor_property("visibility")), slot_info(w), extra_info(w)))
            try:
                n = w.get_children_count()
            except Exception:
                n = 0
            for i in range(n):
                walk(w.get_child_at(i), d + 1)

        walk(root, 0)
    L("ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
