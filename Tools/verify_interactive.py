# Copyright (c) 2026 @sitxovski (Discord)
import unreal
out=[]
def rec(w):
    if w is None: return
    out.append(w)
    if isinstance(w,unreal.PanelWidget):
        for c in w.get_all_children(): rec(c)
inter=[w for w in out if isinstance(w,(unreal.Button,unreal.EditableText,unreal.CheckBox))]


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global w
    bp=unreal.load_asset("/Game/AsdaAssets/UI/Login/WBP_LOGIN_WIN")
    tree=getattr(bp,"widget_tree",None)
    unreal.log("TREE=%s"%(tree is not None))
    root=None
    if tree:
        try: root=tree.get_editor_property("root_widget")
        except Exception as e: unreal.log("root err %s"%e)
    unreal.log("ROOT=%s"%(root.get_name() if root else None))
    rec(root)
    unreal.log("ENUM всего=%d интерактив=%d"%(len(out),len(inter)))
    for w in inter: unreal.log("  INT %-28s %s"%(w.get_name(),type(w).__name__))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
