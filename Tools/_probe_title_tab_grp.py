# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_title_tab_grp.py — ★ ТОЛЬКО ЧТЕНИЕ. Полный список ДЕТЕЙ канваса `Title_ClassSelectBtn_grp`
#   В ПОРЯДКЕ РИСОВАНИЯ. Живой дамп показал у канваса 11 детей, из которых узлов с префиксом
#   `Title_ClassSelectBtn` только 9 (индексы 0-6, 9, 10) — значит дети [7] и [8] названы ИНАЧЕ,
#   и именно они стоят в порядке рисования между подписью вкладки 0 и подписями вкладок 1 и 2.
#   Закон `jui-uneven-node-layout`: раскладка неоднородна, вычисленные имена лгут.
#
# Запуск: run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\_probe_title_tab_grp.py
import traceback
import unreal

EAL = unreal.EditorAssetLibrary
ASSET = "/Game/AsdaAssets/UI/Player_UI/Character/WBP_Title_Win"
GRP = "Title_ClassSelectBtn_grp"


def log(m):
    unreal.log("[проба-grp] %s" % m)


def walk(root):
    """Все виджеты поддерева (тот же обход, что у импортёра)."""
    st, out = [root], []
    while st:
        w = st.pop()
        if not w:
            continue
        out.append(w)
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                st.append(w.get_child_at(i))
    return out


def slot_of(w):
    """Слот в канвасе: смещения и якоря (кто где лежит)."""
    s = w.get_editor_property("slot")
    if not isinstance(s, unreal.CanvasPanelSlot):
        return "<не в канвасе>"
    lay = s.get_editor_property("layout_data")
    off = lay.get_editor_property("offsets")
    return ("off=(L%.1f T%.1f W%.1f H%.1f) Z=%s"
            % (off.left, off.top, off.right, off.bottom, s.get_editor_property("z_order")))


def brush_of(w):
    """Кисть картинки: текстура и цвет (UV из Python недоступен — поле native-only)."""
    if not isinstance(w, unreal.Image):
        return ""
    br = w.get_editor_property("brush")
    res = br.get_editor_property("resource_object")
    return (" кисть[%s тинт %s] цвет=%s"
            % (res.get_name() if res else "<нет текстуры>",
               br.get_editor_property("tint_color"),
               w.get_editor_property("color_and_opacity")))


def main():
    bp = EAL.load_asset(ASSET)
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    nodes = {w.get_name(): w for w in walk(root)}
    grp = nodes.get(GRP)
    if grp is None:
        log("канваса %s НЕТ" % GRP)
        return
    n = grp.get_children_count()
    log("=== %s: детей %d (в ПОРЯДКЕ РИСОВАНИЯ) ===" % (GRP, n))
    for i in range(n):
        c = grp.get_child_at(i)
        log("  [%2d] %-36s %-14s %s vis=%s%s"
            % (i, c.get_name(), type(c).__name__, slot_of(c),
               str(c.get_editor_property("visibility")).replace("SlateVisibility.", ""),
               brush_of(c)))


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    try:
        main()
    except Exception:
        unreal.log_error("[проба-grp] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
