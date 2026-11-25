# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""Пачка 19, баг 4: дамп 9 планов рамки окна (анкеры, офсеты, выравнивание). Только чтение.
Запуск: python Tools/run_headless_safe.py Tools/_dump_frame9_wbp.py --marker "ЗАВЕРШЕНО: дамп рамки"
"""
import unreal

ASSETS = [("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_SHELLITEM_STORE_WIN", "SHELLITEM_STORE_WIN_0_"),
          ("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_BOOSTER_SHOP_WIN", "BOOSTER_SHOP_WIN_0_")]


def L(m):
    """Строка лога."""
    unreal.log("[FRAME9] " + str(m))


def main():
    """Точка входа."""
    for path, pref in ASSETS:
        bp = unreal.EditorAssetLibrary.load_asset(path)
        root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
        L("=== " + path)
        for i in range(root.get_children_count()):
            w = root.get_child_at(i)
            if not w or not w.get_name().startswith(pref):
                continue
            s = w.get_editor_property("slot")
            lay = s.get_editor_property("layout_data")
            a = lay.get_editor_property("anchors")
            mn, mx = a.get_editor_property("minimum"), a.get_editor_property("maximum")
            o = lay.get_editor_property("offsets")
            al = lay.get_editor_property("alignment")
            L("  %s anchors=(%.5f,%.5f)-(%.5f,%.5f) off=(L%.2f T%.2f R%.2f B%.2f) align=(%.2f,%.2f)" % (
                w.get_name(), mn.x, mn.y, mx.x, mx.y, o.left, o.top, o.right, o.bottom, al.x, al.y))
    L("ЗАВЕРШЕНО: дамп рамки")


if __name__ == "__main__":
    main()
