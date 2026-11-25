# Copyright (c) 2026 @sitxovski (Discord)
# _probe_worldmap_widget.py — ПОЛНЫЙ структурный дамп окна карты мира (WBP_MAXMAP_WIN).
#   Печатает КАЖДЫЙ узел: индекс в родителе, класс, имя, видимость, слот (offsets/anchors/alignment),
#   для Image — текстура и UV, для TextBlock — текст/размер/цвет/выравнивание.
#   Нужен ПЕРЕД любой правкой (закон «полный структурный разбор всегда»).
import unreal

B = unreal.AsdaUmgBuilder
EAL = unreal.EditorAssetLibrary

PATH = "/Game/AsdaAssets/UI/Player_UI/Map/WBP_MAXMAP_WIN"


def slot_info(w):
    s = getattr(w, "slot", None)
    if not isinstance(s, unreal.CanvasPanelSlot):
        return "slot=%s" % (type(s).__name__ if s else "None")
    off = s.get_editor_property("layout_data").offsets
    anc = s.get_editor_property("layout_data").anchors
    al = s.get_editor_property("layout_data").alignment
    return ("off=(%.0f,%.0f,%.0f,%.0f) anc=(%.2f,%.2f..%.2f,%.2f) al=(%.2f,%.2f) z=%d auto=%s" %
            (off.left, off.top, off.right, off.bottom,
             anc.minimum.x, anc.minimum.y, anc.maximum.x, anc.maximum.y,
             al.x, al.y, s.get_editor_property("z_order"), s.get_editor_property("auto_size")))


def extra(w):
    try:
        if isinstance(w, unreal.Image):
            br = w.get_editor_property("brush")
            ro = br.get_editor_property("resource_object")
            uv = br.get_editor_property("uv_region")
            return "tex=%s uv=(%.4f,%.4f..%.4f,%.4f) size=(%.0f,%.0f) tint=%s draw=%s" % (
                ro.get_name() if ro else "None", uv.min.x, uv.min.y, uv.max.x, uv.max.y,
                br.image_size.x, br.image_size.y, br.get_editor_property("tint_color").specified_color,
                br.get_editor_property("draw_as"))
        if isinstance(w, unreal.TextBlock):
            f = w.get_editor_property("font")
            return "text=%r size=%s just=%s wrap=%s color=%s" % (
                str(w.get_text()), f.size, w.get_editor_property("justification"),
                w.get_editor_property("auto_wrap_text"),
                w.get_editor_property("color_and_opacity").specified_color)
        if isinstance(w, unreal.ComboBoxString):
            return "options=%s" % (w.get_editor_property("default_options"),)
    except Exception as e:
        return "extra-err %s" % e
    return ""


def walk(w, depth, idx):
    unreal.log("[maxmap] %s[%d] %s '%s' vis=%s | %s | %s" %
               ("  " * depth, idx, w.get_class().get_name(), w.get_name(),
                w.get_editor_property("visibility"), slot_info(w), extra(w)))
    try:
        kids = w.get_all_children()
    except Exception:
        kids = []
    for i, c in enumerate(kids):
        walk(c, depth + 1, i)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    bp = EAL.load_asset(PATH)
    root = B.get_root_canvas(bp)
    unreal.log("[maxmap] ==== ДЕРЕВО WBP_MAXMAP_WIN ====")
    walk(root, 0, 0)
    unreal.log("[maxmap] ==== КОНЕЦ ====")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
