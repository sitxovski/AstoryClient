# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""Пачка 18 (08.11.2026): дамп ПРЯМЫХ детей корневого канваса окон (индекс, имя, класс, слот, z, видимость). Только чтение.
Запуск: python Tools/run_headless_safe.py Tools/_dump_root_children_wbp.py --marker "ЗАВЕРШЕНО: дамп детей корня"
"""
import unreal

ASSETS = [
    "/Game/AsdaAssets/UI/Player_UI/Trade/WBP_BOOSTER_SHOP_WIN",
    "/Game/AsdaAssets/UI/Player_UI/Premium/WBP_SHELLITEM_STORE_WIN",
]


def L(m):
    """Строка лога с меткой."""
    unreal.log("[ROOTDUMP] " + str(m))


def main():
    """Точка входа."""
    for path in ASSETS:
        bp = unreal.EditorAssetLibrary.load_asset(path)
        L("=== %s %s" % (path, "OK" if bp else "НЕ ЗАГРУЗИЛСЯ"))
        if not bp:
            continue
        root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
        for i in range(root.get_children_count()):
            w = root.get_child_at(i)
            if not w:
                continue
            extra = ""
            try:
                s = w.get_editor_property("slot")
                o = s.get_offsets()
                extra = " off=(%.0f,%.0f %.0fx%.0f) z=%d" % (o.left, o.top, o.right, o.bottom, s.get_editor_property("z_order"))
            except Exception:                                    # noqa: BLE001
                pass
            vis = str(w.get_editor_property("visibility")).replace("SlateVisibility.", "")
            L("  #%d %s [%s]%s vis=%s" % (i, w.get_name(), w.get_class().get_name(), extra, vis))
    L("ЗАВЕРШЕНО: дамп детей корня")


if __name__ == "__main__":
    main()
