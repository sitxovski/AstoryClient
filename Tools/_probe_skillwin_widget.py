# Copyright (c) 2026 @sitxovski (Discord)
# Проба: полный дамп дерева виджетов WBP_SKILL_WIN (окно навыков, JUI IDNum 12000).
#   Нужен для точного биндинга контролов в UAsdaSkillWindowSubsystem (закон full-structural-analysis-always).
import unreal

PATHS = [
    "/Game/AsdaAssets/UI/Player_UI/Character/WBP_SKILL_WIN",
    "/Game/AsdaAssets/UI/Player_UI/Character/WBP_Skill_OneTree_Reset_Win",
]


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for path in PATHS:
        bp = unreal.EditorAssetLibrary.load_asset(path)
        print("=== %s: %s" % (path, "OK" if bp else "NOT LOADED"))
        if not bp:
            continue
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
                    extra = " off=(%.0f,%.0f,%.0f,%.0f) anch=(%.3f,%.3f..%.3f,%.3f) z=%d" % (
                        off.left, off.top, off.right, off.bottom,
                        an.minimum.x, an.minimum.y, an.maximum.x, an.maximum.y,
                        slot.get_editor_property("z_order"))
            except Exception:
                pass
            vis = ""
            try:
                vis = " vis=%s" % str(w.get_editor_property("visibility"))
            except Exception:
                pass
            print("  " * d + "%s [%s]%s%s" % (nm, cls, extra, vis))
            if isinstance(w, unreal.PanelWidget):
                for i in range(w.get_children_count()):
                    dump(w.get_child_at(i), d + 1)

        dump(root)
        print("=== TOTAL NODES: %d" % total[0])


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
