# Copyright (c) 2026 @sitxovski (Discord)
# verify_talkbox_classes.py — проверка, что три WBP облачка чата грузятся КАК КЛАССЫ (путь с «_C»,
#   ровно как их грузит UAsdaChatBalloonSubsystem через LoadClass) и что в дереве есть ВСЕ контролы,
#   имена которых зашиты в Templates подсистемы.
import unreal

def L(m):
    unreal.log("[TALKBOX-CHK] " + str(m))

CASES = [
    ("/Game/AsdaAssets/UI/Player_UI/Quest/WBP_TALKBOXW_WIN.WBP_TALKBOXW_WIN",     "TALKBOXW_WIN",    "TALKBOXW_TAB_1_0",    "TALKBOXW_TXT"),
    ("/Game/AsdaAssets/UI/Player_UI/Quest/WBP_TALKBOXB_WIN.WBP_TALKBOXB_WIN",     "TALKBOXB_WIN",    "TALKBOXB_TAB_1_0",    "TALKBOXB_TXT"),
    ("/Game/AsdaAssets/UI/Player_UI/Quest/WBP_TALKBOX_Mon_WIN.WBP_TALKBOX_Mon_WIN","TALKBOX_Mon_WIN","TALKBOX_Mon_Tag_1_0", "TALKBOX_Mon_Txt"),
]

bad = 0


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global bad
    for path, prefix, tail, text in CASES:
        bp = unreal.load_object(None, path)
        gen = unreal.load_object(None, path + "_C")
        L("%s | blueprint=%s | class(_C)=%s" % (prefix, "есть" if bp else "НЕТ", "есть" if gen else "НЕТ"))
        if not bp or not gen:
            bad += 1
            continue
        root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
        names = set()
        def walk(w):
            if not w:
                return
            names.add(w.get_name())
            try:
                n = w.get_children_count()
            except Exception:
                n = 0
            for i in range(n):
                walk(w.get_child_at(i))
        walk(root)
        want = ["%s_0_%d" % (prefix, i) for i in range(9)] + [tail, text]
        miss = [n for n in want if n not in names]
        if miss:
            bad += 1
            L("  ★ НЕТ КОНТРОЛОВ: %s" % ", ".join(miss))
        else:
            L("  все 11 контролов на месте")
        # Размеры углов/хвостика — их подсистема снимает как «шаблонные» (аналог полей +16..+28 эталона).
        for nm in ("%s_0_0" % prefix, "%s_0_8" % prefix, tail, text):
            for w in [root] :
                pass
        def find(w, nm):
            if not w:
                return None
            if w.get_name() == nm:
                return w
            try:
                n = w.get_children_count()
            except Exception:
                n = 0
            for i in range(n):
                r = find(w.get_child_at(i), nm)
                if r:
                    return r
            return None
        for nm in ("%s_0_0" % prefix, "%s_0_4" % prefix, "%s_0_8" % prefix, tail, text):
            w = find(root, nm)
            if not w or not w.slot:
                continue
            ld = w.slot.get_editor_property("layout_data")
            o = ld.offsets
            L("  %-24s off=(%.0f,%.0f) size=(%.0f,%.0f)" % (nm, o.left, o.top, o.right, o.bottom))
    L("ИТОГ: проблемных ассетов %d" % bad)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
