# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""30.10.2026 («Битва армий», боевая фаза; решение координатора «вариант 1»): ТОЧЕЧНО добавить в ассет окна очков
`WBP_CAMPWAR_Point` (JUI 49600) две копии набора цифр `CAMPWAR_Point_countpack` (49604: 6 разрядов по 10 глифов + запятая)
под знаками армий mark2 (49602) и mark3 (49603).

ЗАЧЕМ. Эталон `CCampWar_InitControls` (G:306704-306737) клонирует набор `countpack` `CControl_InstantiateChild` ПОД КАЖДЫЙ знак
армии (родитель клона — сам знак 49601+f, позиция клона = позиция шаблона относительно mark1: JUI Origin "-11 -6"). Импорт JUI
дал только набор под mark1 — очки армий 2 и 3 показывать было нечем. Рантайм-построение виджетов запрещено — копии кладутся в ассет.

ЧТО ДЕЛАЕТ. Только добавление: `UAsdaUmgBuilder.duplicate_subtree` копирует поддерево `CAMPWAR_Point_countpack_grp` в
`CAMPWAR_Point_back2_grp` / `CAMPWAR_Point_back3_grp` с суффиксами `_m2` / `_m3`; позиция корня копии = позиция знака в его
группе + (−11, −6). Дамп дерева ДО и ПОСЛЕ (`countpack_tree_before.txt` / `_after.txt` в `Reports/faction_select`); сверка:
после удаления узлов с суффиксами дерево «после» совпадает с «до» строка в строку. Не совпало — ассет НЕ сохраняется.
Сохраняется только этот ассет. Запуск: `python Tools/run_headless_safe.py Tools/_campwar_point_add_countpack.py --marker ЗАВЕРШЕНО`."""
import os
import unreal

ASSET = "/Game/AsdaAssets/UI/Player_UI/CampWar/WBP_CAMPWAR_Point"
OUT = os.path.normpath(os.path.join(unreal.Paths.project_dir(), "..", "Reports", "faction_select"))
SRC = "CAMPWAR_Point_countpack_grp"
# (родитель копии, знак армии, суффикс)
TARGETS = [("CAMPWAR_Point_back2_grp", "CAMPWAR_Point_mark2_76_0", "_m2"),
           ("CAMPWAR_Point_back3_grp", "CAMPWAR_Point_mark3_81_0", "_m3")]
OFFSET = (-11.0, -6.0)   # JUI Origin `CAMPWAR_Point_countpack` относительно `CAMPWAR_Point_mark1`


def L(m):
    unreal.log("[CWPACK] " + str(m))


def tree_lines(bp):
    """Строки дерева: путь узла, класс, слот (смещения/анкоры/выравнивание/z), видимость, текст, кисть."""
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    out = []

    def walk(w, d):
        s = "%s%s [%s]" % ("  " * d, w.get_name(), w.get_class().get_name())
        try:
            slot = w.get_editor_property("slot")
            if isinstance(slot, unreal.CanvasPanelSlot):
                o = slot.get_offsets()
                a = slot.get_anchors()
                al = slot.get_alignment()
                s += " off=(%.1f,%.1f %.1fx%.1f) anc=(%.2f,%.2f,%.2f,%.2f) al=(%.2f,%.2f) z=%d" % (
                    o.left, o.top, o.right, o.bottom, a.minimum.x, a.minimum.y, a.maximum.x, a.maximum.y, al.x, al.y,
                    slot.get_editor_property("z_order"))
        except Exception:
            pass
        try:
            s += " vis=%s" % str(w.get_editor_property("visibility"))
        except Exception:
            pass
        try:
            if isinstance(w, unreal.TextBlock):
                s += " text=«%s»" % str(w.get_editor_property("text"))
            if isinstance(w, unreal.Image):
                br = w.get_editor_property("brush")
                res = br.get_editor_property("resource_object")
                uv = br.get_editor_property("uv_region")
                s += " res=%s uv=(%.4f,%.4f..%.4f,%.4f)" % (res.get_name() if res else "-", uv.min.x, uv.min.y, uv.max.x, uv.max.y)
        except Exception:
            pass
        out.append(s)
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                walk(w.get_child_at(i), d + 1)

    walk(root, 0)
    return out


def strip_copies(lines):
    """Строки дерева без узлов-копий (`_m2`/`_m3`) и их поддеревьев."""
    out = []
    skip_depth = None
    for l in lines:
        depth = (len(l) - len(l.lstrip(" "))) // 2
        name = l.strip().split(" ")[0]
        if skip_depth is not None and depth > skip_depth:
            continue
        skip_depth = None
        if name.endswith(("_m2", "_m3")):
            skip_depth = depth
            continue
        out.append(l)
    return out


def slots_owned(bp):
    """Каждый слот дерева принадлежит пакету ассета и родителю узла (иначе после записи узел оторвётся от родителя)."""
    pkg = bp.get_outermost()
    bad = []
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp)

    def walk(w):
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                c = w.get_child_at(i)
                sl = c.get_editor_property("slot")
                if not sl or sl.get_outermost() != pkg or sl.get_outer() != w:
                    bad.append(c.get_name())
                walk(c)

    walk(root)
    return bad


def write(name, lines):
    with open(os.path.join(OUT, name), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def slot_pos(bp, name):
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    found = [None]

    def walk(w):
        if found[0] is not None:
            return
        if w.get_name() == name:
            found[0] = w
            return
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                walk(w.get_child_at(i))

    walk(root)
    w = found[0]
    if not w:
        return None
    o = w.get_editor_property("slot").get_offsets()
    return (o.left, o.top)


def main():
    bp = unreal.EditorAssetLibrary.load_asset(ASSET)
    if not bp:
        L("ОШИБКА: ассет не загрузился")
        L("ЗАВЕРШЕНО")
        return
    before = tree_lines(bp)
    if any(l.strip().split(" ")[0].endswith(("_m2", "_m3")) for l in before):
        # Второй запуск — сверка ассета, ПЕРЕЧИТАННОГО с диска: без копий дерево == дамп «до» первого запуска.
        write("countpack_tree_reloaded.txt", before)
        try:
            with open(os.path.join(OUT, "countpack_tree_before.txt"), encoding="utf-8") as f:
                ref = f.read().splitlines()
        except Exception:
            ref = []
        bad = slots_owned(bp)
        L("копии уже есть — сверка с диска: прочая разметка без изменений: %s; оторванных слотов: %d %s" % (
            "ДА" if strip_copies(before) == ref else "НЕТ", len(bad), bad[:5]))
        L("ЗАВЕРШЕНО")
        return
    write("countpack_tree_before.txt", before)
    total = 0
    for parent, mark, suffix in TARGETS:
        mp = slot_pos(bp, mark)
        if mp is None:
            L("ОШИБКА: нет знака %s" % mark)
            L("ЗАВЕРШЕНО")
            return
        pos = unreal.Vector2D(mp[0] + OFFSET[0], mp[1] + OFFSET[1])
        n = unreal.AsdaUmgBuilder.duplicate_subtree(bp, SRC, parent, suffix, pos)
        L("копия %s → %s: %d узлов, позиция (%.0f, %.0f)" % (suffix, parent, n, pos.x, pos.y))
        total += n
    after = tree_lines(bp)
    write("countpack_tree_after.txt", after)
    # Сверка: «после» без новых узлов (и их поддеревьев) == «до»; все слоты — в пакете ассета и у своих родителей.
    same = strip_copies(after) == before
    bad = slots_owned(bp)
    if bad:
        L("ОШИБКА: слоты не у своих родителей: %s" % bad[:10])
        same = False
    added = len(after) - len(before)
    L("узлов до %d, после %d, добавлено %d (функция вернула %d); прочая разметка без изменений: %s" % (
        len(before), len(after), added, total, "ДА" if same else "НЕТ"))
    if not same or added != total or total == 0:
        L("ОШИБКА: сверка не сошлась — ассет НЕ сохраняю")
        L("ЗАВЕРШЕНО")
        return
    unreal.AsdaUmgBuilder.compile_and_save(bp)
    ok = unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
    L("сохранение %s: %s" % (ASSET, "OK" if ok else "ОШИБКА"))
    L("ЗАВЕРШЕНО")


if __name__ == '__main__':
    main()
