# Copyright (c) 2026 @sitxovski (Discord)
import unreal
def L(m): unreal.log("[WBP] "+str(m))
allw=[]
def collect(w):
    if not w: return
    allw.append(w)
    try:
        n=w.get_children_count()
        for i in range(n): collect(w.get_child_at(i))
    except Exception: pass


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global w
    bp=unreal.load_object(None,"/Game/AsdaAssets/UI/Login/WBP_CARAMAKE_WIN.WBP_CARAMAKE_WIN")
    wt=bp.get_editor_property("widget_tree") if bp else None
    root=wt.get_editor_property("root_widget") if wt else None
    collect(root)
    L("всего=%d"%len(allw))
    for w in allw:
        nm=w.get_name()
        if any(k in nm for k in ("CARAMAKE_cb","CARAMAKE_tt","CARAMAKE_SEXBUT","CARAMAKE_TXT0","CARAMAKE_ARROW")):
            vis=w.get_editor_property("visibility")
            slot=w.slot; pos=""
            try:
                o=slot.get_editor_property("offsets"); pos="(%.0f,%.0f,%.0f,%.0f)"%(o.left,o.top,o.right,o.bottom)
            except Exception: pass
            L("%-26s [%s] vis=%s slot=%s"%(nm, w.get_class().get_name(), vis, pos))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
