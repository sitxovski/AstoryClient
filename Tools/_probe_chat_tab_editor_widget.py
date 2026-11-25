# Copyright (c) 2026 @sitxovski (Discord)
# Headless-дамп дерева WBP_CHAT_TAB_WIN и WBP_CHAT_Color_Select_Win: имя/класс/видимость/слот.
#   Запуск: run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\_probe_chat_tab_editor_widget.py
#   Нужен ПЕРЕД правкой списка вкладок 2597: закон «полный структурный разбор» — увидеть КАЖДЫЙ
#   контрол импортированного окна (как именно импортёр назвал ячейку 2597, её план и подпись).
import traceback
import unreal

PATHS = [
    "/Game/AsdaAssets/UI/Player_UI/Chat/WBP_CHAT_TAB_WIN",
    "/Game/AsdaAssets/UI/Player_UI/Chat/WBP_CHAT_Color_Select_Win",
]


def slot_info(w):
    try:
        slot = w.get_editor_property("slot")
    except Exception:
        return "слот: нет"
    if not slot:
        return "слот: нет"
    if not isinstance(slot, unreal.CanvasPanelSlot):
        return "слот: %s" % slot.get_class().get_name()
    try:
        lay = slot.get_editor_property("layout_data")
        a = lay.get_editor_property("anchors")
        off = lay.get_editor_property("offsets")
        mn = a.get_editor_property("minimum")
        mx = a.get_editor_property("maximum")
        kind = "SPAN" if (abs(mn.x - mx.x) > 1e-4 or abs(mn.y - mx.y) > 1e-4) else "POINT"
        z = slot.get_editor_property("z_order")
        return ("анкор [%.4f,%.4f]-[%.4f,%.4f] %s off(L%.1f T%.1f R%.1f B%.1f) z=%d"
                % (mn.x, mn.y, mx.x, mx.y, kind, off.left, off.top, off.right, off.bottom, z))
    except Exception as e:
        return "слот: ошибка %s" % e


def walk(w, depth, parent_name):
    if not w:
        return
    try:
        nm = w.get_name()
    except Exception:
        nm = "?"
    cls = w.get_class().get_name()
    try:
        vis = str(w.get_editor_property("visibility"))
    except Exception:
        vis = "?"
    unreal.log("[TABDUMP] %s%-44s %-24s vis=%-28s род=%-28s %s"
               % ("  " * depth, nm, cls, vis, parent_name, slot_info(w)))
    try:
        if isinstance(w, unreal.PanelWidget):
            n = w.get_children_count()
            for i in range(n):
                walk(w.get_child_at(i), depth + 1, nm)
    except Exception as e:
        unreal.log_warning("[TABDUMP] обход детей %s: %s" % (nm, e))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for path in PATHS:
        unreal.log("[TABDUMP] ================= %s =================" % path)
        try:
            bp = unreal.EditorAssetLibrary.load_asset(path)
            if not bp:
                unreal.log_warning("[TABDUMP] не загрузился: %s" % path)
                continue
            tree = bp.get_editor_property("widget_tree")
            root = tree.get_editor_property("root_widget")
            walk(root, 0, "-")
        except Exception:
            unreal.log_warning("[TABDUMP] ОШИБКА %s:\n%s" % (path, traceback.format_exc()))
    unreal.log("[TABDUMP] ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
