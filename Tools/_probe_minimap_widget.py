# Copyright (c) 2026 @sitxovski (Discord)
# _probe_minimap_widget.py — дамп деревьев ИМПОРТИРОВАННЫХ виджетов миникарты
#   (WBP_MINIMAP_WIN 1702, WBP_MINIMAPMIN_WIN 1700, WBP_MAXMAP_WIN 1710).
#   Нужен, чтобы C++ находил контролы по ТОЧНЫМ именам импортёра (интерактив/Text = idc,
#   планы = {idc}_{ctrlIdx}_{planeJ}). Ничего не меняет.
import unreal

PATHS = [
    "/Game/AsdaAssets/UI/Player_UI/Map/WBP_MINIMAP_WIN",
    "/Game/AsdaAssets/UI/Player_UI/Map/WBP_MINIMAPMIN_WIN",
    "/Game/AsdaAssets/UI/Player_UI/Map/WBP_MAXMAP_WIN",
]


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for path in PATHS:
        bp = unreal.EditorAssetLibrary.load_asset(path)
        if not bp:
            unreal.log_warning("[probe] НЕТ " + path)
            continue
        root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
        unreal.log("[probe] ===== %s : root=%s (%s)" % (path, root.get_name(), root.get_class().get_name()))

        def walk(w, d=1):
            try:
                kids = w.get_all_children()
            except Exception:
                kids = []
            for c in kids:
                slot = c.slot
                info = ""
                if slot and isinstance(slot, unreal.CanvasPanelSlot):
                    lay = slot.get_editor_property("layout_data")
                    info = " off=%s anc=(%s..%s)" % (lay.offsets, lay.anchors.minimum, lay.anchors.maximum)
                unreal.log("[probe] %s%s : %s%s" % ("  " * d, c.get_name(), c.get_class().get_name(), info))
                walk(c, d + 1)

        walk(root)
    unreal.log("[probe] ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
