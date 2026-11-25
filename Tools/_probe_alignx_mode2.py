# Copyright (c) 2026 @sitxovski (Discord)
# _probe_alignx_mode2.py — ground-truth проверка раскладки mode-2 в ЗАПЕЧЁННЫХ WBP.
# Рекурсивно обходит дерево виджетов, накапливая CanvasPanelSlot.offsets (left/top) от корня,
# и печатает АБСОЛЮТНУЮ экранную позицию ключевых полей. Так видно, разъехались ли поля
# «уровень/пол/класс» (окно 8400) — или стоят в ряд 365/445/525, как требует эталон JUI.
# Побочных эффектов при импорте нет (закон [[tools-no-import-side-effects]]): вся работа в main().
import unreal

# <подпапка>/<ассет>  +  список интересующих имён виджетов (подстрока)
TARGETS = [
    ("Soulmate/WBP_SMate_ReqAnsr_Win",
     ["SMate_ReqAnsr_Level", "SMate_ReqAnsr_Sex", "SMate_ReqAnsr_Job",
      "SMate_ReqAnsr_Name", "SMate_ReqAnsr_Intro"]),
    ("Social/WBP_CLAN_MAKING_WIN",
     ["CLAN_FEE_VALUE", "CLAN_GOLD1", "CLAN_MONEY_VALUE", "CLAN_GOLD2"]),
    ("Trade/WBP_NPCSHOP_WIN",
     ["NPCSHOP_TEXT00", "NpcShop_Text00_GIcon", "NPCSHOP_TEXT01", "NpcShop_Text01_GIcon"]),
]
PKG = "/Game/AsdaAssets/UI/Player_UI"


def get_tree(bp):
    try:
        t = bp.widget_tree
        if t:
            return t
    except Exception:
        pass
    try:
        return bp.get_editor_property("WidgetTree")
    except Exception:
        return None


def slot_off(w):
    """(left, top, width, height) слота, если это CanvasPanelSlot, иначе None."""
    try:
        slot = w.slot
    except Exception:
        slot = None
    if slot and isinstance(slot, unreal.CanvasPanelSlot):
        off = slot.get_editor_property("layout_data").get_editor_property("offsets")
        return (float(off.left), float(off.top), float(off.right), float(off.bottom))
    return None


def walk(w, ax, ay, keys, out):
    """Обход сверху-вниз: ax/ay — абсолют левого-верха родителя; накапливаем CanvasPanelSlot.offsets."""
    off = slot_off(w)
    cx = ax + off[0] if off is not None else ax
    cy = ay + off[1] if off is not None else ay
    wn = w.get_name()
    if any(k in wn for k in keys):
        out.append((wn, cx, cy, off))
    try:
        n = w.get_children_count()
    except Exception:
        n = 0
    for i in range(n):
        try:
            c = w.get_child_at(i)
        except Exception:
            continue
        if c:
            walk(c, cx, cy, keys, out)


def main():
    for name, keys in TARGETS:
        path = "%s/%s" % (PKG, name)
        bp = unreal.load_asset(path)
        if not bp:
            unreal.log_warning("RESULT НЕТ %s" % path)
            continue
        root = None
        try:
            root = unreal.AsdaUmgBuilder.get_root_canvas(bp)   # надёжный доступ к корневому канвасу
        except Exception as e:
            unreal.log_warning("RESULT get_root_canvas упал у %s: %s" % (name, e))
        if not root:
            wt = get_tree(bp)
            root = wt.get_editor_property("root_widget") if wt else None
        if not root:
            unreal.log_warning("RESULT нет корня у %s" % name)
            continue
        out = []
        walk(root, 0.0, 0.0, keys, out)
        unreal.log("RESULT === %s (найдено полей=%d) ===" % (name, len(out)))
        for wn, cx, cy, off in out:
            unreal.log("RESULT   %-34s absX=%7.1f absY=%7.1f ownSlot=%s"
                       % (wn, cx, cy, ("(%.0f,%.0f,%.0f,%.0f)" % off) if off else "—"))
    unreal.log("ЗАВЕРШЕНО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
