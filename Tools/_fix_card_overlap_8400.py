# Copyright (c) 2026 @sitxovski (Discord)
# _fix_card_overlap_8400.py — ТОЧЕЧНАЯ правка запечённого WBP_SMate_ReqAnsr_Win (окно 8400).
#
# ПРИЧИНА (доказана дампом headless_alignx_mode2 + _probe_card_measure):
#   Поля Level/Sex/Job стоят В РЯД (+80/+80) — X верны. Но поле Level 80px при шрифте 8pt
#   не вмещает РУССКУЮ строку loc 20272 «40: уровень персонажа» (~95-100px), а Clipping=INHERIT
#   (клипа нет) → текст вылезает вправо и перекрывает соседей. Оригинал (корейский) вмещался.
#   Это дефект ШИРИНЫ/КЛИПА в ассете (адаптация под длинную локаль), не код и не X.
#
# ЧТО ДЕЛАЕМ (только окно 8400, точечно):
#   1) расширяем Level 80->110, переразносим Sex_grp 80->115, Job 80->70(лок)/120->135 — ряд с зазором;
#   2) включаем CLIP_TO_BOUNDS на Level/Sex/Job — гарантия «текст не вылезет за свою рамку»
#      даже для самых длинных строк (класс «Кровожадный боец» и т.п.);
#   3) Intro: перенос уже включён (wrap=True) — добавляем CLIP_TO_BOUNDS, чтобы длинный чужой
#      текст не спускался ниже 60px карточки.
#   Оригинальный JUI (gamemain_jui.ini, корейские 80px) НЕ трогаем — это эталон; адаптация
#   ширины под русскую локаль живёт в собранном ассете (закон fix-original-defects: помечено здесь).
#
# Побочных эффектов при импорте нет (закон tools-no-import-side-effects): всё в main().
import unreal

ASSET = "/Game/AsdaAssets/UI/Player_UI/Soulmate/WBP_SMate_ReqAnsr_Win"

# имя_виджета -> (new_pos | None, new_size | None, clip)
PLAN = {
    "SMate_ReqAnsr_Level":   (None,          (110.0, 15.0), True),   # шире под «40: уровень персонажа»
    "SMate_ReqAnsr_Sex_grp": ((115.0, 0.0),  None,          False),  # сдвиг вправо за Level (+зазор)
    "SMate_ReqAnsr_Sex":     (None,          (65.0, 15.0),  True),
    "SMate_ReqAnsr_Job":     ((70.0, 0.5),   (135.0, 15.0), True),   # лок. сдвиг в Sex_grp + шире
    "SMate_ReqAnsr_Intro":   (None,          None,          True),   # клип по высоте (wrap уже есть)
}

CLIP = unreal.WidgetClipping.CLIP_TO_BOUNDS


def walk(w, out):
    nm = w.get_name()
    if nm in PLAN:
        out[nm] = w
    try:
        cnt = w.get_children_count()
    except Exception:
        cnt = 0
    for i in range(cnt):
        c = w.get_child_at(i)
        if c:
            walk(c, out)


def main():
    bp = unreal.load_asset(ASSET)
    if not bp:
        unreal.log_warning("RESULT НЕТ ассета")
        unreal.log("ЗАВЕРШЕНО")
        return
    root = None
    try:
        root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    except Exception:
        pass
    if not root:
        wt = bp.get_editor_property("WidgetTree")
        root = wt.get_editor_property("root_widget") if wt else None
    found = {}
    if root:
        walk(root, found)

    changed = 0
    for nm, (pos, size, clip) in PLAN.items():
        w = found.get(nm)
        if not w:
            unreal.log_warning("RESULT пропуск (не найден): %s" % nm)
            continue
        # слот
        try:
            slot = w.slot
        except Exception:
            slot = None
        if slot and isinstance(slot, unreal.CanvasPanelSlot):
            if pos is not None:
                slot.set_position(unreal.Vector2D(pos[0], pos[1]))
            if size is not None:
                slot.set_size(unreal.Vector2D(size[0], size[1]))
        # клиппинг (свойство виджета)
        if clip:
            w.set_editor_property("clipping", CLIP)
        changed += 1
        unreal.log("RESULT изменён %-22s pos=%s size=%s clip=%s" % (nm, pos, size, clip))

    # сохранить пакет
    ok = unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False)
    unreal.log("RESULT сохранение=%s изменено_виджетов=%d" % (ok, changed))
    unreal.log("ЗАВЕРШЕНО")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
