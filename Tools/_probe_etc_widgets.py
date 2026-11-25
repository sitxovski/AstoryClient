# Copyright (c) 2026 @sitxovski (Discord)
# Проба: дамп дерева виджетов WBP_ETC_NEW_WIN — окно «Прочее» (JUI IDNum 74600).
#   Нужен, чтобы подключать подсистему к РЕАЛЬНЫМ именам виджетов ассета,
#   а не к придуманным (закон [[no-invented-ui]], [[jui-flattened-hierarchy-geometry]]).
import unreal


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    path = "/Game/AsdaAssets/UI/Player_UI/System/WBP_ETC_NEW_WIN"
    bp = unreal.EditorAssetLibrary.load_asset(path)
    print("=== WBP_ETC_NEW_WIN: %s" % ("OK" if bp else "НЕ ЗАГРУЗИЛСЯ"))
    if not bp:
        print("ЗАВЕРШЕНО")
        return
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    count = [0]

    def dump(w, d=0):
        count[0] += 1
        cls = w.get_class().get_name()
        nm = w.get_name()
        extra = ""
        try:
            slot = w.get_editor_property("slot")
            if isinstance(slot, unreal.CanvasPanelSlot):
                off = slot.get_offsets()
                extra = " off=(%.0f,%.0f,%.0f,%.0f) z=%d" % (
                    off.left, off.top, off.right, off.bottom, slot.get_z_order())
        except Exception:
            pass
        try:
            extra += " vis=%s" % str(w.get_editor_property("visibility")).split(".")[-1]
        except Exception:
            pass
        try:
            if isinstance(w, unreal.TextBlock):
                extra += " TXT=%r" % str(w.get_editor_property("text"))
        except Exception:
            pass
        print("  " * d + "%s [%s]%s" % (nm, cls, extra))
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                dump(w.get_child_at(i), d + 1)

    dump(root)
    print("ВСЕГО ВИДЖЕТОВ: %d" % count[0])
    print("ЗАВЕРШЕНО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
