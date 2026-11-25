# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""P101_CashGift (01.11.2026): ПОЛНЫЙ СТРУКТУРНЫЙ ДАМП узлов подарка (закон full-structural-analysis-always). Только чтение.

Окно 6000 `WBP_SHELLITEM_STORE_WIN`: панели 6296 `SHELLITEM_EQUIP_WIN` и 6333 `SHELLITEM_FUNC_WIN` целиком (все узлы
`SHELLITEM_EQUIP*`/`SHELL_EQUIP*`/`Shell_Equip*`/`SHELLITEM_FUNC*`/`SHELL_FUNC*`/`ShellItem_Func*`) и кнопки карточек `_BUT2`.
Окно 15300 `WBP_BOOSTER_SHOP_WIN`: кнопки `BOOSTER_ITEM_PRESENT*` и узлы панели покупки `BOOSTER_PUR_*`.
Для каждого узла: имя, класс, родитель, слот (left/top/w/h, z), видимость, текст (TextBlock/EditableText).
Маркер для run_headless_safe.py: «ЗАВЕРШЕНО: дамп подарка».
Запуск: python Tools/run_headless_safe.py Tools/_dump_cashgift_wbp.py --marker "ЗАВЕРШЕНО: дамп подарка"
"""
import re
import unreal

# Ассеты и префиксы узлов, которые дампим.
PLAN = [
    ("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_SHELLITEM_STORE_WIN",
     re.compile(r"^(?:__)?(SHELLITEM_EQUIP|SHELL_EQUIP|Shell_Equip|SHELLITEM_FUNC|SHELL_FUNC|ShellItem_Func|SHELLITEM_POP\d\d_BUT2)")),
    ("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_BOOSTER_SHOP_WIN",
     re.compile(r"^(BOOSTER_ITEM_PRESENT|BOOSTER_PUR_)")),
]


def L(m):
    """Строка лога с меткой дампа."""
    unreal.log("[CGDUMP] " + str(m))


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
    L("ЗАВЕРШЕНО: дамп подарка")


if __name__ == "__main__":
    main()
