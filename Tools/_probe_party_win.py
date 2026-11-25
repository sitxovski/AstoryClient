# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# Пробник: (1) полное дерево WBP_PARTY_WIN (окно 2200) и WBP_PARTYDISPERSION (2256);
#          (2) свойства текстов 2251/2246 — ищем причину «дёрганья» надписи.
import unreal

OUT = r"@@ASTORY_DEV_ROOT@@\Reports\_party_win_dump.txt"
ASSETS = [
    "/Game/AsdaAssets/UI/Player_UI/Social/WBP_PARTY_WIN.WBP_PARTY_WIN",
    "/Game/AsdaAssets/UI/Player_UI/Social/WBP_PARTYDISPERSION.WBP_PARTYDISPERSION",
]
TEXT_PROBE = [
    ("/Game/AsdaAssets/UI/Player_UI/Social/WBP_PARTYTAKE_WIN.WBP_PARTYTAKE_WIN",
     ["PARTYTAKE_TXT", "PARTYTAKE_DIS", "PARTYTAKE_CHOICE_Text"]),
    ("/Game/AsdaAssets/UI/Player_UI/Social/WBP_PARTYBOX_WIN.WBP_PARTYBOX_WIN",
     ["PARTYBOX_TXT"]),
]

def slot_str(w):
    try:
        o = w.slot.get_editor_property("offsets")
        a = w.slot.get_editor_property("anchors")
        al = w.slot.get_editor_property("alignment")
        return "off=(%.0f,%.0f,%.0f,%.0f) anch=(%.2f,%.2f)-(%.2f,%.2f) align=(%.2f,%.2f) auto=%s" % (
            o.left, o.top, o.right, o.bottom,
            a.minimum.x, a.minimum.y, a.maximum.x, a.maximum.y,
            al.x, al.y, w.slot.get_editor_property("auto_size"))
    except Exception:
        return ""

def walk(w, d, out):
    if not w: return
    out.append("%s%-44s [%s] vis=%s %s" % ("  "*d, w.get_name(), w.get_class().get_name(),
               str(w.get_editor_property("visibility")).split(".")[-1], slot_str(w)))
    try: n = w.get_children_count()
    except Exception: n = 0
    for i in range(n): walk(w.get_child_at(i), d+1, out)

def root_of(bp):
    try:
        r = unreal.AsdaUmgBuilder.get_root_canvas(bp)
        if r: return r
    except Exception: pass
    wt = bp.get_editor_property("widget_tree")
    return wt.get_editor_property("root_widget") if wt else None

def collect(w, acc):
    if not w: return
    acc.append(w)
    try: n = w.get_children_count()
    except Exception: n = 0
    for i in range(n): collect(w.get_child_at(i), acc)

def main():
    out = []
    for p in ASSETS:
        out.append("="*104); out.append("АССЕТ: " + p)
        bp = unreal.load_object(None, p)
        if not bp: out.append("  НЕ ЗАГРУЗИЛСЯ"); continue
        r = root_of(bp)
        if not r: out.append("  КОРЕНЬ НЕ ПОЛУЧЕН"); continue
        walk(r, 1, out)

    out.append("="*104)
    out.append("СВОЙСТВА ТЕКСТОВ (диагностика дёрганья)")
    for p, names in TEXT_PROBE:
        bp = unreal.load_object(None, p)
        if not bp: continue
        r = root_of(bp)
        acc = []; collect(r, acc)
        for w in acc:
            if w.get_name() in names:
                try:
                    aw = w.get_editor_property("auto_wrap_text")
                    wa = w.get_editor_property("wrap_text_at")
                    ju = w.get_editor_property("justification")
                    mn = w.get_editor_property("min_desired_width")
                except Exception as e:
                    out.append("  %s: свойства недоступны (%s)" % (w.get_name(), e)); continue
                out.append("  %-26s auto_wrap=%s wrap_at=%s just=%s min_w=%s  %s" % (
                    w.get_name(), aw, wa, str(ju).split(".")[-1], mn, slot_str(w)))
    txt = "\n".join(out)
    with open(OUT, "w", encoding="utf-8") as f: f.write(txt)
    unreal.log("[PARTYWIN] дамп записан: %s (строк %d)" % (OUT, len(out)))

if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
