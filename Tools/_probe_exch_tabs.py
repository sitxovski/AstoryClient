# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# ★ Проба вкладок аукциона: все узлы полос 13408/13409/13260 (имя, класс, видимость, слот, текст,
#   цвет) + ТЕКСТУРА планов и её флаг never_stream (гипотеза «мигание = подкачка мипов»).
#   Плюс общий скан ВСЕХ окон порта на дефект «нулевая подпись полосы вкладок пуста».
import unreal

ASSET = "/Game/AsdaAssets/UI/Player_UI/Trade/WBP_EXCHANGE_WIN"
UI_ROOT = "/Game/AsdaAssets/UI"
EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder


def log(m):
    unreal.log("[вкладки-проба] %s" % m)


def walk(w, out):
    if not w:
        return
    out.append(w)
    if isinstance(w, unreal.PanelWidget):
        for i in range(w.get_children_count()):
            walk(w.get_child_at(i), out)


def slot_str(w):
    try:
        s = w.get_editor_property("slot")
        if isinstance(s, unreal.CanvasPanelSlot):
            o = s.get_editor_property("layout_data").get_editor_property("offsets")
            return "(%.0f,%.0f %.0fx%.0f)" % (o.left, o.top, o.right, o.bottom)
    except Exception:                                            # noqa: BLE001
        pass
    return "(нет слота)"


def brush_tex(w):
    try:
        b = w.get_editor_property("brush")
        ro = b.get_editor_property("resource_object")
        return ro
    except Exception:                                            # noqa: BLE001
        return None


def main():
    bp = EAL.load_asset(ASSET)
    root = B.get_root_canvas(bp)
    all_w = []
    walk(root, all_w)

    texes = {}
    for w in all_w:
        n = w.get_name()
        if not (n.startswith("EXCHANGER_RADIOBTN") or n.startswith("EXCHMY_CateSelBtn")):
            continue
        extra = ""
        if isinstance(w, unreal.TextBlock):
            c = w.get_editor_property("color_and_opacity").get_editor_property("specified_color")
            extra = " текст=[%s] цвет %.3f" % (w.get_text(), c.r)
        if isinstance(w, unreal.Image):
            t = brush_tex(w)
            if t:
                texes[t.get_path_name()] = texes.get(t.get_path_name(), 0) + 1
                extra = " тек=%s" % t.get_path_name()
        log("%-34s %-14s %-22s %s%s" % (n, w.get_class().get_name(),
            str(w.get_editor_property("visibility")).split(".")[-1].split(":")[0], slot_str(w), extra))

    for p, cnt in sorted(texes.items()):
        t = EAL.load_asset(p.split(".")[0])
        ns = None
        try:
            ns = t.get_editor_property("never_stream")
        except Exception as e:                                   # noqa: BLE001
            ns = "err:%s" % e
        log("ТЕКСТУРА %s  планов %d  never_stream=%s" % (p, cnt, ns))

    # ── общий скан: у скольких ПОЛОС ВКЛАДОК порта нулевая подпись пуста ──
    #   Идём по таблице шага `asda_ui_tabbar_captions.TABBARS` (42 контрола в 27 окнах) — это и
    #   есть полный перечень полос вкладок порта, собранный сканом всех четырёх `.JUI`.
    log("-- скан всех полос вкладок на пустую нулевую подпись --")
    import importlib.util
    import os
    here = os.path.dirname(os.path.abspath(__file__))
    spec = importlib.util.spec_from_file_location("tabcap", os.path.join(here, "asda_ui_tabbar_captions.py"))
    tc = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tc)

    bad = tot = dup = 0
    for path, idc, nbtn, _col in tc.TABBARS:
        if not EAL.does_asset_exist(path):
            continue
        w_bp = EAL.load_asset(path)
        r = B.get_root_canvas(w_bp) if w_bp else None
        if r is None:
            continue
        nodes = []
        walk(r, nodes)
        names = {}
        counts = {}
        for w in nodes:
            n = w.get_name()
            counts[n] = counts.get(n, 0) + 1
            names.setdefault(n, w)
        t0 = names.get(idc + "_Text")
        if not isinstance(t0, unreal.TextBlock):
            continue
        tot += 1
        s0 = str(t0.get_text()).strip()
        ndup = sum(1 for i in range(nbtn) if counts.get("%s_OPT%d" % (idc, i), 0) > 1)
        if ndup:
            dup += 1
            log("  ДУБЛИ ИМЁН: %-34s / %-28s кнопок с двойником: %d" % (path.rsplit("/", 1)[-1], idc, ndup))
        if not s0:
            bad += 1
            log("  ПУСТАЯ НУЛЕВАЯ: %-34s / %s" % (path.rsplit("/", 1)[-1], idc))
    log("ИТОГО полос вкладок проверено %d; нулевая подпись ПУСТА у %d; с дублями имён кнопок %d" % (tot, bad, dup))


if __name__ == "__main__":
    main()
