# Copyright (c) 2026 @sitxovski (Discord)
# Читает реальные Canvas-позиции виджетов из запечённых WBP (ground-truth проверка раскладки).
import unreal

TARGETS = ["CampWar/WBP_CAMPWAR_L_MAP", "Premium/WBP_Primium_Item_post2_WIN"]   # <подпапка>/<ассет>
PKG = "/Game/AsdaAssets/UI/Player_UI"

def get_tree(bp):
    for acc in ("widget_tree",):
        try:
            t = getattr(bp, acc)
            if t: return t
        except Exception: pass
    try: return bp.get_editor_property("WidgetTree")
    except Exception: return None


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global bp
    for name in TARGETS:
        path = "%s/%s" % (PKG, name)
        bp = unreal.load_asset(path)
        if not bp:
            unreal.log_warning("НЕТ %s" % path); continue
        tree = get_tree(bp)
        widgets = []
        if tree:
            try: widgets = tree.all_widgets
            except Exception:
                try: widgets = tree.get_editor_property("all_widgets") or []
                except Exception: widgets = []
        minx = 1e9; maxx = -1e9; miny = 1e9; maxy = -1e9; n = 0; offs = []
        for w in widgets:
            try: slot = w.slot
            except Exception: slot = None
            if slot and isinstance(slot, unreal.CanvasPanelSlot):
                off = slot.get_editor_property("layout_data").get_editor_property("offsets")
                x = float(off.left); y = float(off.top)
                minx = min(minx, x); maxx = max(maxx, x); miny = min(miny, y); maxy = max(maxy, y); n += 1
                if x < -50 or y < -50 or x > 1300 or y > 760:
                    offs.append((w.get_name(), round(x), round(y)))
        unreal.log("RESULT %s canvas=%d Xrange=%.0f..%.0f Yrange=%.0f..%.0f offscreen=%d" % (name, n, minx, maxx, miny, maxy, len(offs)))
        for o in offs[:15]:
            unreal.log("RESULT   OFFSCREEN %s pos=(%d,%d)" % o)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
