# Copyright (c) 2026 @sitxovski (Discord)
# Проба: дамп дерева виджетов WBP_Title_Win и WBP_Title_Notice_BG —
#   сверка имён контролов окна званий (18700) с ожиданиями UAsdaTitleSubsystem.
import unreal


def dump_bp(path):
    """Печатает дерево одного WBP: имя, класс, смещения слота, видимость."""
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
                extra = " off=(%.0f,%.0f,%.0f,%.0f)" % (off.left, off.top, off.right, off.bottom)
        except Exception:
            pass
        try:
            extra += " vis=%s" % str(w.get_editor_property("visibility")).split(".")[-1]
        except Exception:
            pass
        print("  " * d + "%s [%s]%s" % (nm, cls, extra))
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                dump(w.get_child_at(i), d + 1)

    dump(root)
    print("ВСЕГО ВИДЖЕТОВ (%s): %d" % (path.rsplit("/", 1)[-1], count[0]))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    dump_bp("/Game/AsdaAssets/UI/Player_UI/Character/WBP_Title_Win")
    dump_bp("/Game/AsdaAssets/UI/Player_UI/HUD/WBP_Title_Notice_BG")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
