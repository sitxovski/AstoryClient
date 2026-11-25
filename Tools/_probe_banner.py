# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""_probe_banner.py — состав виджетов баннера повышения уровня."""
import unreal

def log(m): unreal.log("[banner] " + str(m))

def main():
    EAL = unreal.EditorAssetLibrary
    for nm in ("WBP_ChangeJob_Banner", "WBP_Con_Banner2"):
        p = "/Game/AsdaAssets/UI/Player_UI/Dungeon/%s.%s" % (nm, nm)
        if not EAL.does_asset_exist(p):
            log("НЕТ " + p); continue
        bp = EAL.load_asset(p)
        tree = bp.get_editor_property("WidgetTree")
        root = tree.get_editor_property("RootWidget")
        log("=== %s ===" % nm)
        def walk(w, d=0):
            if not w: return
            try: cname = w.get_name()
            except Exception: cname = "?"
            log("  " * (d + 1) + "%s : %s" % (cname, w.get_class().get_name()))
            try:
                for ch in w.get_all_children():
                    walk(ch, d + 1)
            except Exception:
                pass
        walk(root)
    log("ЗАВЕРШЕНО")

# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
