# Copyright (c) 2026 @sitxovski (Discord)
# Проба: дамп дерева виджетов WBP_EXPBar_WIn / WBP_NOTICE_WIN / WBP_HERO_NOTICE_WIN
#   (сверка имён контролов с ожиданиями UAsdaHudSubsystem::SetupExpControls).
import unreal


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for wbp in ("WBP_EXPBar_WIn", "WBP_NOTICE_WIN", "WBP_HERO_NOTICE_WIN"):
        path = "/Game/AsdaAssets/UI/Player_UI/HUD/%s" % wbp
        bp = unreal.EditorAssetLibrary.load_asset(path)
        print("=== %s: %s" % (wbp, "OK" if bp else "НЕ ЗАГРУЗИЛСЯ"))
        if not bp:
            continue
        root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
        def dump(w, d=0):
            cls = w.get_class().get_name()
            nm = w.get_name()
            extra = ""
            try:
                slot = w.get_editor_property("slot")
                if isinstance(slot, unreal.CanvasPanelSlot):
                    off = slot.get_offsets(); an = slot.get_anchors()
                    extra = " off=(%.0f,%.0f,%.0f,%.0f) anch=(%.3f,%.3f..%.3f,%.3f)" % (
                        off.left, off.top, off.right, off.bottom,
                        an.minimum.x, an.minimum.y, an.maximum.x, an.maximum.y)
            except Exception:
                pass
            print("  " * d + "%s [%s]%s" % (nm, cls, extra))
            if isinstance(w, unreal.PanelWidget):
                for i in range(w.get_children_count()):
                    dump(w.get_child_at(i), d + 1)
        dump(root)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
