# Copyright (c) 2026 @sitxovski (Discord)
# _probe_card_measure.py — read-only замер карточки окна 8400 (SMate_ReqAnsr_Win).
# Снимает у полей Level/Sex/Job/Intro: шрифт(размер), Clipping, AutoWrapText, Justification,
# текущий слот (left,top,right=width,bottom=height) и — если доступен FSlateApplication —
# ЖЕЛАЕМУЮ ширину реальных РУССКИХ строк (худший случай) в ЭТОМ шрифте.
# Побочных эффектов при импорте нет (закон tools-no-import-side-effects): всё в main().
import unreal

ASSET = "/Game/AsdaAssets/UI/Player_UI/Soulmate/WBP_SMate_ReqAnsr_Win"
KEYS  = ["SMate_ReqAnsr_Level", "SMate_ReqAnsr_Sex", "SMate_ReqAnsr_Job",
         "SMate_ReqAnsr_Name", "SMate_ReqAnsr_Intro"]

# Худшие по длине реальные строки из GameText.csv для каждого поля.
WORST = {
    "SMate_ReqAnsr_Level": "100: уровень персонажа",   # loc 20272
    "SMate_ReqAnsr_Sex":   "Мужчина",                  # loc 20273/20274
    "SMate_ReqAnsr_Job":   "Кровожадный боец",          # loc 12305 (самый длинный класс)
    "SMate_ReqAnsr_Intro": "Пример длинного текста о себе для проверки переноса по ширине карточки.",
}


def find_all(w, out):
    n = 0
    try:
        n = w.get_name()
    except Exception:
        pass
    name = w.get_name()
    if any(k in name for k in KEYS):
        out.append(w)
    try:
        cnt = w.get_children_count()
    except Exception:
        cnt = 0
    for i in range(cnt):
        c = w.get_child_at(i)
        if c:
            find_all(c, out)


def slot_tuple(w):
    try:
        slot = w.slot
        if slot and isinstance(slot, unreal.CanvasPanelSlot):
            off = slot.get_editor_property("layout_data").get_editor_property("offsets")
            return (float(off.left), float(off.top), float(off.right), float(off.bottom))
    except Exception:
        pass
    return None


def measure(text, font):
    try:
        # FSlateApplication -> font measure
        size = unreal.SlateBlueprintLibrary.get_desired_size  # noqa (не то)
    except Exception:
        pass
    return None


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
    out = []
    if root:
        find_all(root, out)
    unreal.log("RESULT === найдено виджетов=%d ===" % len(out))
    for w in out:
        nm = w.get_name()
        slot = slot_tuple(w)
        clip = None
        wrap = None
        just = None
        fsize = None
        ftype = None
        try:
            clip = str(w.get_editor_property("clipping"))
        except Exception:
            pass
        if isinstance(w, unreal.TextBlock):
            try:
                wrap = w.get_editor_property("auto_wrap_text")
            except Exception:
                pass
            try:
                just = str(w.get_editor_property("justification"))
            except Exception:
                pass
            try:
                f = w.get_editor_property("font")
                fsize = f.get_editor_property("size")
                ftype = str(f.get_editor_property("typeface"))
            except Exception:
                pass
        unreal.log("RESULT   %-22s slot=%s clip=%s wrap=%s just=%s fsize=%s typ=%s"
                   % (nm, slot, clip, wrap, just, fsize, ftype))
        # желаемый размер строки
        if isinstance(w, unreal.TextBlock):
            key = next((k for k in WORST if k in nm), None)
            if key:
                try:
                    f = w.get_editor_property("font")
                    ds = unreal.AsdaUmgBuilder.measure_text(WORST[key], f) if hasattr(unreal.AsdaUmgBuilder, "measure_text") else None
                except Exception:
                    ds = None
                unreal.log("RESULT   >>> %-22s desired=%s  для '%s'" % (nm, ds, WORST[key]))
    unreal.log("ЗАВЕРШЕНО")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
