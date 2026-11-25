# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_strength_part_owned_text.py — ТОЧЕЧНАЯ правка АССЕТА окна 50500 `WBP_Strength_Part_Win` (31.10.2026).
#
# ЗАЧЕМ. Контрол 50521 `Strength_Part_2_Text_2` (панель «Необходим пустой камень улучшения», верхняя строка) по `.JUI`
#   шириной 130 px — под корейский образец «11/12~». Код эталона (`uiItemInfoRefresh@0xB67C10`) пишет туда loc 108520:
#   RU «В наличии камень улучшений %d. Необходим камень улучшений %d» (~330 px), EN «Owned Upgrade stone %d Required
#   Upgrade stone %d». В 130 px строка обрезалась с обеих сторон (кадр самотеста asda.SelfTest.UpgradeSplit).
#   Шаг расширения окна 09.10 (`asda_ui_widen_windows_plan.py`) центрировал поле, считая текст коротким.
#
# ЧТО ДЕЛАЕМ: поле и его хит-зону — на всю ширину строки панели (ряд `Black_115_0` 310×23 внутри группы
#   `Strength_Part_2_Text_2_grp`): left 87 → 0, width 130 → 310 (верх и высота те же). Выравнивание текста — центр (как было).
#   RU-строка и в 310 px не входит (~334 px при кегле 8, кадр 31.10) — кегль ЭТОГО поля 8 → 7 (~292 px).
#
# ВТОРАЯ ПРАВКА (ревизия 31.10, Б4): предупреждение 50541 `Strength_Part_4_Text1` (108509, поле 656×78) — перенос строк
#   задаётся В АССЕТЕ явной шириной `WrapTextAt = 656` (код его больше не ставит — закон «без рантайм-фиксов виджетов»).
#   Не AutoWrap: в CanvasPanel он не получает ширину на замере (память `ui-autowrap-canvas-gotcha`).
#
# ★ ИДЕМПОТЕНТНОСТЬ: значения абсолютные; слот сверяется с ИСХОДНЫМ (87/130) или ЦЕЛЕВЫМ (0/310) состоянием, любое иное —
#   чужая правка → не трогаем и не сохраняем.
# ★ ПОСЛЕ ПОЛНОЙ ПЕРЕГЕНЕРАЦИИ UI ШАГ ПОВТОРИТЬ (после `asda_ui_widen_windows_plan.py`).
#
# Запуск: python Tools/run_headless_safe.py Tools/asda_fix_strength_part_owned_text.py --marker "SPW_FIX_DONE" --require "SPW_FIX_DONE"
import unreal

EAL = unreal.EditorAssetLibrary
WBP = "/Game/AsdaAssets/UI/Player_UI/Craft/WBP_Strength_Part_Win"
# Узел → (исходные left/width, целевые left/width).
PLAN = {
    "Strength_Part_2_Text_2":         (87.0, 130.0, 0.0, 310.0),
    "Strength_Part_2_Text_2_120_HIT": (87.0, 130.0, 0.0, 310.0),
}
FONT_FROM, FONT_TO = 8, 7   # кегль поля: исходный → целевой (абсолютно, повтор безопасен)
EPS = 0.6


def log(m):
    unreal.log("[spwfix] " + str(m))


def walk(w, out):
    try:
        out[w.get_name()] = w
    except Exception:                                            # noqa: BLE001
        return
    if isinstance(w, unreal.PanelWidget):
        for i in range(w.get_children_count()):
            c = w.get_child_at(i)
            if c:
                walk(c, out)


def main():
    bp = EAL.load_asset(WBP)
    if not bp:
        log("ОШИБКА: ассет не загрузился")
        return
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    nodes = {}
    walk(root, nodes)
    changed = 0
    for name, (l0, w0, l1, w1) in PLAN.items():
        w = nodes.get(name)
        slot = w.slot if w else None
        if not isinstance(slot, unreal.CanvasPanelSlot):
            log("ОШИБКА: нет узла/слота %s" % name)
            return
        o = slot.get_offsets()
        log("%s до: L%.1f T%.1f W%.1f H%.1f" % (name, o.left, o.top, o.right, o.bottom))
        if abs(o.left - l1) < EPS and abs(o.right - w1) < EPS:
            continue
        if not (abs(o.left - l0) < EPS and abs(o.right - w0) < EPS):
            log("ОШИБКА: %s в чужом состоянии — не трогаю" % name)
            return
        o.left = l1
        o.right = w1
        slot.set_offsets(o)
        changed += 1
    tb = nodes.get("Strength_Part_2_Text_2")
    if isinstance(tb, unreal.TextBlock):
        f = tb.get_editor_property("font")
        if int(round(f.size)) == FONT_FROM:
            f.size = FONT_TO
            tb.set_editor_property("font", f)
            changed += 1
            log("кегль поля %d → %d" % (FONT_FROM, FONT_TO))
        elif int(round(f.size)) != FONT_TO:
            log("ОШИБКА: кегль поля %s — чужое состояние, не трогаю" % f.size)
            return
    warn = nodes.get("Strength_Part_4_Text1")
    if isinstance(warn, unreal.TextBlock):
        cur = float(warn.get_editor_property("wrap_text_at"))
        if abs(cur - 656.0) > EPS:
            if abs(cur) > EPS:
                log("ОШИБКА: WrapTextAt 50541 = %s — чужое состояние, не трогаю" % cur)
                return
            warn.set_editor_property("wrap_text_at", 656.0)
            changed += 1
            log("50541 WrapTextAt 0 → 656")
        log("50541 auto_wrap=%s wrap_text_at=%s" % (warn.get_editor_property("auto_wrap_text"), warn.get_editor_property("wrap_text_at")))
    else:
        log("ОШИБКА: нет узла Strength_Part_4_Text1")
        return
    if isinstance(tb, unreal.TextBlock):
        log("выравнивание %s, кегль %s" % (tb.get_editor_property("justification"), tb.get_editor_property("font").size))
    if changed:
        EAL.save_asset(WBP, only_if_is_dirty=False)
        log("сохранено, правок %d" % changed)
    else:
        log("уже в целевом состоянии")
    log("SPW_FIX_DONE")


if __name__ == '__main__':
    main()
