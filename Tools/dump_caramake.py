# Copyright (c) 2026 @sitxovski (Discord)
import unreal
def L(m): unreal.log("[DUMP] "+str(m))
def walk(w, d=0):
    if not w: return
    nm = w.get_name(); cls = w.get_class().get_name()
    slot = w.slot
    info=""
    if slot and hasattr(slot,'get_editor_property'):
        try:
            off = slot.get_editor_property('offsets')
            info = "pos=(%.0f,%.0f) sz=(%.0f,%.0f)" % (off.left, off.top, off.right, off.bottom)
        except Exception: pass
    if any(k in nm.upper() for k in ("SEXBUT","ICONBOX00","ICONBOX01","_WIN","BASE","BG","TXT00")):
        L("%s [%s] %s" % (nm, cls, info))
# перечислить все виджеты
allw = []


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global info, nm, off, slot, w
    bp = unreal.load_object(None, "/Game/AsdaAssets/UI/Login/WBP_CARAMAKE_WIN.WBP_CARAMAKE_WIN")
    if not bp: L("нет WBP"); 
    wt = bp.get_editor_property("widget_tree") if bp else None
    root = wt.get_editor_property("root_widget") if wt else None
    if wt:
        wt.for_each_widget(lambda w: allw.append(w)) if hasattr(wt,'for_each_widget') else None
    if not allw and root:
        def collect(w):
            allw.append(w)
            try:
                for i in range(w.get_children_count()): collect(w.get_child_at(i))
            except Exception: pass
        collect(root)
    L("всего виджетов=%d" % len(allw))
    for w in allw:
        nm=w.get_name()
        if any(k in nm.upper() for k in ("SEXBUT","ICONBOX00","ICONBOX01")):
            slot=w.slot; info=""
            try:
                off=slot.get_editor_property('offsets'); info="pos=(%.0f,%.0f) sz=(%.0f,%.0f)"%(off.left,off.top,off.right,off.bottom)
            except Exception: pass
            L("  %s [%s] %s"%(nm, w.get_class().get_name(), info))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
