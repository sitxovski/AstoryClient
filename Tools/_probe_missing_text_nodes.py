# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# Проверка результата шага `asda_ui_missing_text_nodes.py` (класс дефектов S1a).
#   Идёт по данным `.JUI`, для КАЖДОГО ожидаемого узла ищет его в собранном `WBP_*` и сверяет:
#     • узел есть и это `TextBlock`;
#     • он лежит в ТОЙ ЖЕ панели, что рассчитал шаг;
#     • числа слота совпадают с рассчитанными ИЗ ДАННЫХ (позиция/размер);
#     • текст непустой и равен `fix_text(EXS TextContent)`;
#     • цвет = `EXS TextColor`;
#     • узел НЕ ловит мышь (иначе съест клики по кнопке под ним).
# Запуск: run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\_probe_missing_text_nodes.py
import importlib.util
import os
import traceback

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("MT", os.path.join(HERE, "asda_ui_missing_text_nodes.py"))
MT = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(MT)          # безопасно: шаг ничего не делает при импорте
J = MT.J


def log(m):
    unreal.log("[проверка-S1a] %s" % m)


def main():
    amap = MT.asset_map()
    all_roots = {}
    for cls in MT.PRIORITY:
        ini = MT.ini_of(cls)
        if os.path.isfile(ini):
            all_roots[cls] = J.parse_jui(ini)
    coll = MT.collisions(all_roots)

    total = ok = 0
    miss, wrongtype, wrongpanel, wronggeom, emptytext, wrongcolor, hittable = [], [], [], [], [], [], []
    wins = 0
    for cls in MT.PRIORITY:
        for root in all_roots.get(cls, []):
            entries = J.missing_text_nodes(root)
            if not entries:
                continue
            wins += 1
            base = MT.san(root.idc or root.name)
            wname = "WBP_" + base + MT.wsuffix(base, cls, coll)
            paths = amap.get(wname) or []
            if not paths:
                miss.append("%s: НЕТ АССЕТА" % wname)
                total += len(entries)
                continue
            bp = unreal.EditorAssetLibrary.load_asset(paths[0])
            canvas = unreal.AsdaUmgBuilder.get_root_canvas(bp)
            J.compute_anchors(root)
            order, parent_of, hidden = MT.index_nodes(root)
            wid, par = MT.widget_index(canvas)
            for node, _p, name in entries:
                total += 1
                w = wid.get(name)
                if w is None:
                    miss.append("%s / %s" % (wname, name))
                    continue
                if not isinstance(w, unreal.TextBlock):
                    wrongtype.append("%s / %s = %s" % (wname, name, type(w).__name__))
                    continue
                panel, ox, oy = MT.container_of(node, parent_of, order, wid, par, canvas)
                got_panel = par.get(name)
                if panel is not None and got_panel is not None \
                        and panel.get_name() != got_panel.get_name():
                    wrongpanel.append("%s / %s: в %s, ожидалось %s"
                                      % (wname, name, got_panel.get_name(), panel.get_name()))
                pos, size, halign, _wrap = MT.text_geometry(node, ox, oy, root)   # ★ 07.10: + рамка окна (`J.label_slot`)
                off = MT.slot_offsets(w)
                # у листа верхнего уровня числа переписал анкор — сверяем только размер
                top_leaf = (not node.children) and parent_of[id(node)] is not None \
                    and parent_of[id(parent_of[id(node)])] is None
                if off is not None:
                    exp = (size.x, size.y) if top_leaf else (pos.x, pos.y, size.x, size.y)
                    got = (off.right, off.bottom) if top_leaf else (off.left, off.top, off.right, off.bottom)
                    if not all(abs(a - b) < 0.6 for a, b in zip(exp, got)):
                        wronggeom.append("%s / %s: %s вместо %s" % (wname, name, got, exp))
                txt = str(w.get_editor_property("text"))
                if not txt.strip():
                    emptytext.append("%s / %s" % (wname, name))
                elif txt != MT.fix_text(node.txt_content or ""):
                    emptytext.append("%s / %s: текст %r ≠ данных" % (wname, name, txt[:20]))
                c = MT.text_color(node)
                gc = w.get_editor_property("color_and_opacity").get_editor_property("specified_color")
                if max(abs(gc.r - c.r), abs(gc.g - c.g), abs(gc.b - c.b), abs(gc.a - c.a)) > 0.01:
                    wrongcolor.append("%s / %s" % (wname, name))
                vis = w.get_editor_property("visibility")
                if vis in (unreal.SlateVisibility.VISIBLE,):
                    hittable.append("%s / %s" % (wname, name))
                ok += 1

    for tag, lst in (("НЕТ УЗЛА", miss), ("НЕ TextBlock", wrongtype), ("ЧУЖАЯ ПАНЕЛЬ", wrongpanel),
                     ("ГЕОМЕТРИЯ", wronggeom), ("ТЕКСТ", emptytext), ("ЦВЕТ", wrongcolor),
                     ("ЛОВИТ МЫШЬ", hittable)):
        for m in lst[:12]:
            unreal.log_warning("[проверка-S1a] %s: %s" % (tag, m))
        if len(lst) > 12:
            unreal.log_warning("[проверка-S1a] %s: ещё %d" % (tag, len(lst) - 12))
    log("окон %d, ожидалось узлов %d, найдено и проверено %d; нет узла %d, не TextBlock %d, "
        "чужая панель %d, геометрия %d, текст %d, цвет %d, ловит мышь %d"
        % (wins, total, ok, len(miss), len(wrongtype), len(wrongpanel), len(wronggeom),
           len(emptytext), len(wrongcolor), len(hittable)))


if __name__ == "__main__":
    try:
        main()
    except Exception:                                            # noqa: BLE001
        unreal.log_error("[проверка-S1a] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
