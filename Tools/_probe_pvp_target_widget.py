# Copyright (c) 2026 @sitxovski (Discord)
# Проба: полный дамп дерева виджета WBP_PVP_TARGET (окно цели, JUI PVP_TARGET id 40).
#   Нужна ДО написания кода подсистемы (закон [[full-structural-analysis-always]]):
#   без неё имена узлов пришлось бы угадывать, а закон [[no-invented-ui]] это запрещает.
#   Печатает: имя узла, класс, оффсеты/анкоры слота канваса, видимость, для картинок — размер
#   браша и ресурс, для текстов — строку и цвет.
import unreal


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    path = "/Game/AsdaAssets/UI/Player_UI/HUD/WBP_PVP_TARGET"
    bp = unreal.EditorAssetLibrary.load_asset(path)
    print("=== WBP_PVP_TARGET: %s" % ("OK" if bp else "НЕ ЗАГРУЗИЛСЯ"))
    if not bp:
        print("ЗАВЕРШЕНО")
        return
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    total = [0]

    def dump(w, d=0):
        total[0] += 1
        cls = w.get_class().get_name()
        nm = w.get_name()
        extra = ""
        try:
            slot = w.get_editor_property("slot")
            if isinstance(slot, unreal.CanvasPanelSlot):
                off = slot.get_offsets()
                an = slot.get_anchors()
                extra += " off=(%.0f,%.0f,%.0f,%.0f) anch=(%.3f,%.3f..%.3f,%.3f)" % (
                    off.left, off.top, off.right, off.bottom,
                    an.minimum.x, an.minimum.y, an.maximum.x, an.maximum.y)
                extra += " z=%d" % slot.get_editor_property("z_order")
        except Exception:
            pass
        try:
            extra += " vis=%s" % str(w.get_editor_property("visibility"))
        except Exception:
            pass
        if isinstance(w, unreal.Image):
            try:
                b = w.get_editor_property("brush")
                res = b.get_editor_property("resource_object")
                extra += " img=%s size=(%.0f,%.0f)" % (
                    res.get_name() if res else "None",
                    b.get_editor_property("image_size").x,
                    b.get_editor_property("image_size").y)
            except Exception:
                pass
        if isinstance(w, unreal.TextBlock):
            try:
                extra += " text='%s'" % str(w.get_editor_property("text"))
            except Exception:
                pass
        print("  " * d + "%s [%s]%s" % (nm, cls, extra))
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                dump(w.get_child_at(i), d + 1)

    dump(root)
    print("ВСЕГО УЗЛОВ: %d" % total[0])
    print("ЗАВЕРШЕНО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
