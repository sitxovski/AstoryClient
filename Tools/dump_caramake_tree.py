# Copyright (c) 2026 @sitxovski (Discord)
# Дамп цепочек родителей SEL-рамок и ячеек ICONBOX в WBP_CARAMAKE_WIN (координатные системы).
import unreal

widgets = []

def slot_pos(w):
    s = w.slot
    if isinstance(s, unreal.CanvasPanelSlot):
        ld = s.get_editor_property("layout_data")
        offs = ld.get_editor_property("offsets")
        return (offs.left, offs.top, offs.right, offs.bottom)
    return None

def chain(w):
    parts = []
    cur = w
    while cur:
        p = slot_pos(cur)
        parts.append("%s(%s)%s" % (cur.get_name(), type(cur).__name__,
                     " pos=(%.0f,%.0f) sz=(%.0f,%.0f)" % p if p else ""))
        cur = cur.get_parent()
    return "  <-  ".join(parts)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global w
    W = unreal.load_object(None, "/Game/AsdaAssets/UI/Login/WBP_CARAMAKE_WIN.WBP_CARAMAKE_WIN")
    tree = unreal.find_object(None, W.get_path_name() + ":WidgetTree")
    for o in unreal.ObjectIterator():
        try:
            if isinstance(o, unreal.Widget) and o.get_outer() == tree:
                widgets.append(o)
        except Exception:
            pass
    for w in widgets:
        n = w.get_name()
        if n.startswith("CARAMAKE_ICONBOX_SEL") or n in ("CARAMAKE_ICONBOX00", "CARAMAKE_ICONBOX01",
                "CARAMAKE_ICONBOX05", "CARAMAKE_ICONBOX24") or n.startswith("CARAMAKE_ICONBOX00_") \
                or n.startswith("CARAMAKE_ICONBOX24_"):
            print("CHAIN:", chain(w))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
