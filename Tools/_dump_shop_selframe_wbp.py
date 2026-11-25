# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""Баг 6 пачки 16 (08.11.2026): ПОЛНЫЙ СТРУКТУРНЫЙ ДАМП рамок выбора магазинов (6273 окна 6000, 15496 окна 15300) и опорных ячеек. Только чтение.
Запуск: python Tools/run_headless_safe.py Tools/_dump_shop_selframe_wbp.py --marker "ЗАВЕРШЕНО: дамп рамок выбора"
"""
import re
import unreal

# Ассеты и префиксы узлов, которые дампим (рамки выбора магазинов и опорные ячейки).
PLAN = [
    ("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_SHELLITEM_STORE_WIN",
     re.compile(r"^(?:__)?(SHELLITEM_POP00_SEL|SHELLITEM_POP\d\d$|SHELLITEM_POP\d\d_grp$|SHELLITEM_ITEMLIST|Package_\d+_SEL|SHELLITEM_STORE_Packageshop_grp$)")),
    ("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_BOOSTER_SHOP_WIN",
     re.compile(r"^(BOOSTER_ITEM_SEL|BOOSTER_ITEM_?0?0?_grp$|BOOSTER_ITEM00)")),
]


def L(m):
    """Строка лога с меткой дампа."""
    unreal.log("[SELDUMP] " + str(m))


def find_asset(path):
    """Ассет по пути; если не найден — поиск по имени в /Game (у 15300 путь в коде мог отличаться)."""
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.EditorAssetLibrary.load_asset(path), path
    name = path.rsplit("/", 1)[-1]
    for p in unreal.EditorAssetLibrary.list_assets("/Game/AsdaAssets/UI", recursive=True, include_folder=False):
        if p.split(".")[0].endswith("/" + name):
            q = p.split(".")[0]
            return unreal.EditorAssetLibrary.load_asset(q), q
    return None, path


def describe(w, parent):
    """Одна строка описания узла."""
    extra = ""
    try:
        slot = w.get_editor_property("slot")
        if isinstance(slot, unreal.CanvasPanelSlot):
            off = slot.get_offsets()
            extra = " off=(%.0f,%.0f %.0fx%.0f) z=%d" % (off.left, off.top, off.right, off.bottom,
                                                        slot.get_editor_property("z_order"))
    except Exception:                                                # noqa: BLE001
        pass
    vis = ""
    try:
        vis = " vis=%s" % str(w.get_editor_property("visibility")).replace("SlateVisibility.", "")
    except Exception:                                                # noqa: BLE001
        pass
    txt = ""
    try:
        if isinstance(w, (unreal.TextBlock, unreal.EditableText)):
            txt = " text=«%s»" % str(w.get_editor_property("text"))
        if isinstance(w, unreal.EditableText):
            txt += " hint=«%s» readonly=%s" % (str(w.get_editor_property("hint_text")), w.get_editor_property("is_read_only"))
    except Exception:                                                # noqa: BLE001
        pass
    en = ""
    try:
        en = "" if w.get_editor_property("is_enabled") else " DISABLED"
    except Exception:                                                # noqa: BLE001
        pass
    return "%s [%s] parent=%s%s%s%s%s" % (w.get_name(), w.get_class().get_name(), parent, extra, vis, txt, en)


def dump(path, rx):
    """Обойти дерево и напечатать узлы по префиксу."""
    bp, real = find_asset(path)
    L("=== %s: %s" % (real, "OK" if bp else "НЕ ЗАГРУЗИЛСЯ"))
    if not bp:
        return
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    cnt = [0, 0]

    def walk(w, parent):
        cnt[0] += 1
        if rx.match(w.get_name()):
            cnt[1] += 1
            L("  " + describe(w, parent))
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                c = w.get_child_at(i)
                if c:
                    walk(c, w.get_name())

    walk(root, "-")
    L("=== узлов всего %d, по префиксу %d" % (cnt[0], cnt[1]))


def main():
    """Точка входа."""
    for p, rx in PLAN:
        dump(p, rx)
    L("ЗАВЕРШЕНО: дамп рамок выбора")


if __name__ == "__main__":
    main()
