# Copyright (c) 2026 @sitxovski (Discord)
# _fix_charinfo_rank_width.py — ТОЧЕЧНАЯ правка WBP_CHARAINFO_WIN (окно 900): поле «Звание» армии 926 шире.
#
# ПРИЧИНА (пачка 15, баг 9, 08.11.2026; самотест asda.CharInfo.HonorCraftSelfTest, кадр ru_honor_rank_widest):
#   926 `CHARAINFO_CAMPWAR_T3_S` теперь показывает ИМЯ звания (как оригинал `CFactionUI_UpdateInfo@0xB932C0`).
#   Ширина узла — эталонные 80 px (gamemain_jui.ini, корейские имена), а 10 из 20 русских имён шире
#   (до 106 px «Сержант 1-го класса») — текст по центру расползался под значок 927. Английские ≤ 80 px.
#   Решение владельца 08.11 (через координатора): «Расширить поле». [ИСПРАВЛЕНИЕ] адаптации под длинную локаль;
#   эталонный JUI не трогаем, адаптация живёт в собранном ассете (как _fix_card_overlap_8400.py).
#
# ЧТО МЕНЯЕМ (было → стало, координаты в родителе; дамп до правки — _probe_charinfo_rank_slots.py):
#   CHARAINFO_CAMPWAR_T3_grp    (3,42) 188×24 → (3,42) 200×24   — как соседняя строка армии T1_grp (200)
#   CHARAINFO_CAMPWAR_T3_S_grp  (82,0) 106×24 → (66,0) 134×24   — группа значок+имя растёт влево
#   CHARAINFO_CAMPWAR_Mark_78_0 (0,0)   24×24 → (0,0)   24×24   — значок вместе с группой: x 82 → 66 (влево на 16)
#   CHARAINFO_CAMPWAR_T3_S      (26,6.5) 80×15 → (24,6.5) 110×15 — имя 90…200 в строке, центр 145 (было 148, как «Очки славы»)
#   Остальные узлы окна не трогаются. Побочных эффектов при импорте нет (закон tools-no-import-side-effects).
import unreal

ASSET = "/Game/AsdaAssets/UI/Player_UI/Character/WBP_CHARAINFO_WIN"   # окно 900

# имя → (ожидаемые смещения ДО (x, y, w, h), новые (x, y, w, h)); сверка «до» защищает от правки чужой разметки
PLAN = {
    "CHARAINFO_CAMPWAR_T3_grp":   ((3.0, 42.0, 188.0, 24.0), (3.0, 42.0, 200.0, 24.0)),
    "CHARAINFO_CAMPWAR_T3_S_grp": ((82.0, 0.0, 106.0, 24.0), (66.0, 0.0, 134.0, 24.0)),
    "CHARAINFO_CAMPWAR_T3_S":     ((26.0, 6.5, 80.0, 15.0),  (24.0, 6.5, 110.0, 15.0)),
}


def walk(w, out):
    """Собрать узлы плана по имени (обход всего дерева)."""
    if w.get_name() in PLAN:
        out[w.get_name()] = w
    if isinstance(w, unreal.PanelWidget):
        for i in range(w.get_children_count()):
            c = w.get_child_at(i)
            if c:
                walk(c, out)


def main():
    bp = unreal.EditorAssetLibrary.load_asset(ASSET)
    if not bp:
        print("RANKFIX НЕТ АССЕТА"); print("ЗАВЕРШЕНО"); return
    found = {}
    walk(unreal.AsdaUmgBuilder.get_root_canvas(bp), found)
    # Сначала сверка всех «до» — правим только если разметка ровно та, что в дампе.
    for nm, (before, _) in PLAN.items():
        w = found.get(nm)
        s = w.get_editor_property("slot") if w else None
        if not isinstance(s, unreal.CanvasPanelSlot):
            print("RANKFIX ОТКАЗ: нет узла/слота %s" % nm); print("ЗАВЕРШЕНО"); return
        o = s.get_offsets()
        cur = (o.left, o.top, o.right, o.bottom)
        if any(abs(a - b) > 0.01 for a, b in zip(cur, before)):
            print("RANKFIX ОТКАЗ: %s сейчас %s, ожидалось %s" % (nm, cur, before)); print("ЗАВЕРШЕНО"); return
    for nm, (before, after) in PLAN.items():
        s = found[nm].get_editor_property("slot")
        s.set_offsets(unreal.Margin(after[0], after[1], after[2], after[3]))
        o = s.get_offsets()
        print("RANKFIX %s: %s → (%.1f,%.1f,%.1f,%.1f)" % (nm, before, o.left, o.top, o.right, o.bottom))
    ok = unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False)
    print("RANKFIX сохранение=%s" % ok)
    print("ЗАВЕРШЕНО")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
