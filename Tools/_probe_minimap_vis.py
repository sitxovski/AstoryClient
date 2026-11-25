# Copyright (c) 2026 @sitxovski (Discord)
# _probe_minimap_vis.py — дамп ВИДИМОСТИ виджетов окон мини-карты (ничего не меняет).
#   Нужен для сверки «контрол → условие показа» с эталоном (JUI Visible + рантайм-условия).
import unreal

PATHS = [
    "/Game/AsdaAssets/UI/Player_UI/Map/WBP_MINIMAP_WIN",
    "/Game/AsdaAssets/UI/Player_UI/Map/WBP_MINIMAPMIN_WIN",
]


def main():
    for path in PATHS:
        bp = unreal.EditorAssetLibrary.load_asset(path)
        if not bp:
            continue
        root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
        unreal.log("[vis] ===== %s root=%s" % (path, root.get_editor_property("visibility")))

        def walk(w, d=1):
            try:
                kids = w.get_all_children()
            except Exception:
                return
            for c in kids:
                unreal.log("[vis] %s%-30s %-22s %s" % ("  " * d, c.get_name(),
                           c.get_class().get_name(), c.get_editor_property("visibility")))
                walk(c, d + 1)
        walk(root)
    unreal.log("[vis] ГОТОВО")


if __name__ == "__main__":
    main()
