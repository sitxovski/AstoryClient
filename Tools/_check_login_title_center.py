# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""09.11.2026: ТОЛЬКО ЧТЕНИЕ — слоты заголовков окна 300 (центровка `asda_ui_login_title_center`) после правки швов рамки.
Запуск: python Tools/run_headless_safe.py Tools/_check_login_title_center.py --marker "ПРОВЕРКА-ТИТУЛ: ГОТОВО"
"""
import unreal


def main():
    """Печать слотов SERVER_TitleTxt / CHANNEL_TitleTxt."""
    bp = unreal.EditorAssetLibrary.load_asset("/Game/AsdaAssets/UI/Login/WBP_SERVER_SELECT_WIN")
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    stack = [root]
    while stack:
        w = stack.pop()
        if w.get_name() in ("SERVER_TitleTxt", "CHANNEL_TitleTxt"):
            s = w.get_editor_property("slot")
            lay = s.get_editor_property("layout_data")
            o = lay.get_editor_property("offsets")
            a = lay.get_editor_property("alignment")
            unreal.log("[ПРОВЕРКА-ТИТУЛ] %s off(%.1f %.1f %.1f %.1f) align(%.2f %.2f) auto=%s" % (
                w.get_name(), o.left, o.top, o.right, o.bottom, a.x, a.y, s.get_editor_property("auto_size")))
        if isinstance(w, unreal.PanelWidget):
            stack += [w.get_child_at(i) for i in range(w.get_children_count()) if w.get_child_at(i)]
    unreal.log("[ПРОВЕРКА-ТИТУЛ] ПРОВЕРКА-ТИТУЛ: ГОТОВО")


if __name__ == "__main__":
    main()
