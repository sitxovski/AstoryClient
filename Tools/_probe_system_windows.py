# Copyright (c) 2026 @sitxovski (Discord)
# Проба: дамп деревьев виджетов WBP_SYSTEM_MenuWIN_NEW (ESC-меню, JUI IDNum 69400) и
#   WBP_SYSTEM_Option_NEW (окно опций, 69500). Нужен, чтобы подсистемы подключались к РЕАЛЬНЫМ
#   именам виджетов ассета, а не к придуманным (законы [[no-invented-ui]], [[full-structural-analysis-always]]).
import unreal


def dump_wbp(path):
    bp = unreal.EditorAssetLibrary.load_asset(path)
    print("=== %s: %s" % (path, "OK" if bp else "НЕ ЗАГРУЗИЛСЯ"))
    if not bp:
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
    print("ВСЕГО ВИДЖЕТОВ %s: %d" % (path.rsplit("/", 1)[-1], count[0]))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    dump_wbp("/Game/AsdaAssets/UI/Player_UI/System/WBP_SYSTEM_MenuWIN_NEW")
    dump_wbp("/Game/AsdaAssets/UI/Player_UI/System/WBP_SYSTEM_Option_NEW")
    print("ЗАВЕРШЕНО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
