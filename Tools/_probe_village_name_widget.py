# Copyright (c) 2026 @sitxovski (Discord)
# _probe_village_name_widget.py — диагностика штатного виджета JUI-контрола 9000 «VILLAGE_NAME»:
#   печатает дерево WBP_VILLAGE_NAME (класс/имя/родитель), чтобы подтвердить, что рантайм
#   (UAsdaZoneSubsystem) находит UImage и верхний узел ветки. Ничего не меняет.
import unreal

path = "/Game/AsdaAssets/UI/Player_UI/HUD/WBP_VILLAGE_NAME"


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    bp = unreal.EditorAssetLibrary.load_asset(path)
    if not bp:
        unreal.log_warning("[probe] нет " + path)
    else:
        root = unreal.AsdaUmgBuilder.get_root_canvas(bp)   # WidgetTree из Python недоступен — берём билдер порта
        unreal.log("[probe] root = %s (%s)" % (root.get_name(), root.get_class().get_name()))

        def walk(w, d=1):
            try: kids = w.get_all_children()
            except Exception: kids = []
            for c in kids:
                slot = c.slot
                info = ""
                if slot and isinstance(slot, unreal.CanvasPanelSlot):
                    lay = slot.get_editor_property("layout_data")
                    info = " offsets=%s anchors=(%s..%s) align=%s" % (
                        lay.offsets, lay.anchors.minimum, lay.anchors.maximum, lay.alignment)
                unreal.log("[probe] %s%s : %s%s" % ("  " * d, c.get_name(), c.get_class().get_name(), info))
                walk(c, d + 1)

        walk(root)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
