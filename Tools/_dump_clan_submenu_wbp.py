# Copyright (c) 2026 @sitxovski (Discord)
# _dump_clan_submenu_wbp.py — ДАМП (только чтение) полосы страниц панели «Предводитель» окна 6400 (WBP_COMMUNITY_WIN):
#   узлы `CmtyClan_SubMenuBtn*` и `CmtyClan_SubMenuTxt*` — имя, класс, родитель, слот (позиция/размер/z), видимость.
#   Нужен до точечной правки `_fix_clan_hide_applicants.py` (решение владельца 01.11.2026: убрать «Кандидаты»).
#   Побочных эффектов при импорте нет (закон tools-no-import-side-effects): всё в main().
import unreal

ASSET = "/Game/AsdaAssets/UI/Player_UI/Social/WBP_COMMUNITY_WIN"
PREFIXES = ("CmtyClan_SubMenuBtn", "CmtyClan_SubMenuTxt")


def walk(w, parent, out):
    nm = w.get_name()
    if nm.startswith(PREFIXES):
        out.append((w, parent))
    try:
        cnt = w.get_children_count()
    except Exception:
        cnt = 0
    for i in range(cnt):
        c = w.get_child_at(i)
        if c:
            walk(c, nm, out)


def main():
    bp = unreal.load_asset(ASSET)
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp) if bp else None   # WidgetTree у WidgetBlueprint закрыт для Python
    out = []
    if root:
        walk(root, "", out)
    for w, parent in out:
        slot = w.slot
        pos = size = z = None
        if slot and isinstance(slot, unreal.CanvasPanelSlot):
            p = slot.get_position(); s = slot.get_size(); z = slot.get_z_order()
            pos = (round(p.x, 2), round(p.y, 2)); size = (round(s.x, 2), round(s.y, 2))
        unreal.log("RESULT %-34s %-22s parent=%-30s pos=%s size=%s z=%s vis=%s" % (
            w.get_name(), w.get_class().get_name(), parent, pos, size, z, w.get_visibility()))
    unreal.log("RESULT всего узлов %d" % len(out))
    unreal.log("ЗАВЕРШЕНО")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
