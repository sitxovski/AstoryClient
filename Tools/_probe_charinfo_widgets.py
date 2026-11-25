# Copyright (c) 2026 @sitxovski (Discord)
# Проба: дамп дерева виджетов WBP_CHARAINFO_WIN — сверка имён контролов окна характеристик
#   и вкладки эмоций с ожиданиями UAsdaCharaInfoSubsystem.
import unreal


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    path = "/Game/AsdaAssets/UI/Player_UI/Character/WBP_CHARAINFO_WIN"
    bp = unreal.EditorAssetLibrary.load_asset(path)
    print("=== WBP_CHARAINFO_WIN: %s" % ("OK" if bp else "НЕ ЗАГРУЗИЛСЯ"))
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
                extra = " off=(%.0f,%.0f,%.0f,%.0f)" % (off.left, off.top, off.right, off.bottom)
        except Exception:
            pass
        try:
            vis = w.get_editor_property("visibility")
            extra += " vis=%s" % str(vis).split(".")[-1]
        except Exception:
            pass
        print("  " * d + "%s [%s]%s" % (nm, cls, extra))
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                dump(w.get_child_at(i), d + 1)

    dump(root)
    print("ВСЕГО ВИДЖЕТОВ: %d" % count[0])


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
