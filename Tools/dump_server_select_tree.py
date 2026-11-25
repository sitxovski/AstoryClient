# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# ПОЛНЫЙ структурный дамп WBP_SERVER_SELECT_WIN: каждый виджет — класс, тип слота, родитель,
# видимость, позиция/размер, z-order. Нужен, чтобы чинить UI по фактическому дереву UMG,
# а не по догадкам об именах (правило full-structural-analysis).
import unreal, os

OUT = r"@@ASTORY_DEV_ROOT@@\Reports\umg_server_select_tree.txt"
WBP = "/Game/AsdaAssets/UI/Login/WBP_SERVER_SELECT_WIN"

def log(m): unreal.log("[dump-tree] " + str(m))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    bp = unreal.EditorAssetLibrary.load_asset(WBP)
    if not bp:
        log("НЕ загружен: %s" % WBP)
    else:
        tree = bp.get_editor_property("widget_tree") if hasattr(bp, "get_editor_property") else None
        lines = []
        try:
            widgets = unreal.WidgetBlueprintLibrary.get_all_widgets_of_class if False else None
        except Exception:
            pass
        # обходим дерево через WidgetTree ассета
        wt = None
        try:
            wt = bp.get_editor_property("WidgetTree")
        except Exception as e:
            log("WidgetTree недоступен: %s" % e)
        all_w = []
        if wt:
            try:
                all_w = wt.get_editor_property("all_widgets")
            except Exception:
                all_w = []
        if not all_w:
            # запасной путь: перечислить объекты внутри пакета
            pkg = bp.get_outermost()
            all_w = [o for o in unreal.EditorAssetLibrary.list_assets(WBP.rsplit("/",1)[0], False, False)]
            log("all_widgets пуст; собрано иначе: %d" % len(all_w))
        log("виджетов в дереве: %d" % len(all_w))
        for w in all_w:
            try:
                name = w.get_name()
                cls = w.get_class().get_name()
                parent = w.get_parent().get_name() if w.get_parent() else "-"
                slot = w.slot.get_class().get_name() if w.slot else "-"
                vis = str(w.get_editor_property("visibility"))
                pos = sz = zo = align = "-"
                if w.slot and isinstance(w.slot, unreal.CanvasPanelSlot):
                    s = w.slot
                    pos = str(s.get_position()); sz = str(s.get_size())
                    zo = str(s.get_z_order()); align = str(s.get_alignment())
                lines.append("%-34s cls=%-22s slot=%-18s parent=%-26s vis=%-28s pos=%s size=%s z=%s align=%s"
                             % (name, cls, slot, parent, vis, pos, sz, zo, align))
            except Exception as e:
                lines.append("ОШИБКА на виджете: %s" % e)
        os.makedirs(os.path.dirname(OUT), exist_ok=True)
        with open(OUT, "w", encoding="utf-8") as f:
            f.write("# ПОЛНОЕ дерево виджетов %s (всего %d)\n" % (WBP, len(lines)))
            f.write("\n".join(sorted(lines)))
        log("записано: %s (строк %d)" % (OUT, len(lines)))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
