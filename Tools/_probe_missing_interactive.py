# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_missing_interactive.py — НЕЗАВИСИМАЯ ПРОВЕРКА шага `asda_ui_missing_interactive`.
#
# Запускается ОТДЕЛЬНЫМ процессом и НИЧЕГО НЕ ПРАВИТ. Проверяет пять вещей:
#   1. каждый узел класса S2 из общего правила `asda_jui_parse.missing_interactive_nodes`
#      присутствует в ассете и имеет ПРАВИЛЬНЫЙ КЛАСС (UButton / UCheckBox / UComboBoxString /
#      UEditableText, у радио — по кнопке на план);
#   2. ★ ВТОРОЙ МЕТОД ПО ГЕОМЕТРИИ: абсолютный прямоугольник узла, СОБРАННЫЙ ИЗ АССЕТА (подъём
#      по родителям с учётом анкоров, база 1024×768), совпадает с прямоугольником из данных `.JUI`.
#      Совпадение имени ничего не доказывает — доказывает совпадение чисел;
#   3. ★★★ НЕТ ПЕРЕХВАТА МЫШИ: ни один созданный узел не пересекается с ДРУГИМ интерактивным
#      виджетом того же окна, если оба реально видимы (ни у кого в цепочке предков нет
#      `Collapsed`/`Hidden`);
#   4. план-картинка контрола, которую теперь рисует сам виджет (кнопка с одним планом, чекбокс),
#      СВЁРНУТА — иначе под виджетом остаётся второй контур;
#   5. у 23 ОТСЕЯННЫХ контролов узла НЕТ (в т.ч. `_OPT0` у радио) — отказ действительно исполнен.
#
# Запуск: run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\_probe_missing_interactive.py
import importlib.util
import os

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
SKIN_INI = r"@@ASTORY_DEV_ROOT@@\RE\docs\JuiSkin.ini"
BASE_W, BASE_H = 1024.0, 768.0

EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder

_s = importlib.util.spec_from_file_location("J_probe", os.path.join(HERE, "asda_jui_parse.py"))
J = importlib.util.module_from_spec(_s); _s.loader.exec_module(J)
_s2 = importlib.util.spec_from_file_location("M_probe", os.path.join(HERE, "asda_ui_missing_text_nodes.py"))
M = importlib.util.module_from_spec(_s2); _s2.loader.exec_module(M)
_s3 = importlib.util.spec_from_file_location("S_probe", os.path.join(HERE, "asda_ui_missing_interactive.py"))
S = importlib.util.module_from_spec(_s3); _s3.loader.exec_module(S)

IACT_CLASSES = (unreal.Button, unreal.CheckBox, unreal.ComboBoxString, unreal.EditableText)
EXPECT = {"PushButton": unreal.Button, "CheckButton": unreal.CheckBox,
          "RadioButton": unreal.Button, "ComboBox": unreal.ComboBoxString,
          "EditBox": unreal.EditableText}


def log(m):
    unreal.log("[S2-проба] %s" % m)


def slot_rect(w):
    """Прямоугольник виджета в базе 1024×768 ОТНОСИТЕЛЬНО его канваса — с учётом анкоров.

    `_apply_anchor` импортёра хранит позицию двумя способами: у фикс-узла `offL = x − fx·1024`
    (значит `x = offL + fx·1024`), у тянущегося — `offsets = 0`, а границы задаёт сам анкор.
    Обратное преобразование одно и то же в обоих случаях, поэтому ветвления не нужно.
    """
    try:
        sl = w.get_editor_property("slot")
    except Exception:                                            # noqa: BLE001
        return None
    if not isinstance(sl, unreal.CanvasPanelSlot):
        return None
    ld = sl.get_editor_property("layout_data")
    off = ld.get_editor_property("offsets")
    an = ld.get_editor_property("anchors")
    ax0, ay0 = an.minimum.x, an.minimum.y
    ax1, ay1 = an.maximum.x, an.maximum.y
    x = off.left + ax0 * BASE_W
    y = off.top + ay0 * BASE_H
    w_ = (ax1 - ax0) * BASE_W + (off.right - off.left) if ax1 > ax0 else off.right
    h_ = (ay1 - ay0) * BASE_H + (off.bottom - off.top) if ay1 > ay0 else off.bottom
    return (x, y, w_, h_)


def _zorder(w):
    """z-порядок виджета в его канвасе (у не-канвас-слота считаем 0)."""
    try:
        sl = w.get_editor_property("slot")
        if isinstance(sl, unreal.CanvasPanelSlot):
            return int(sl.get_editor_property("z_order"))
    except Exception:                                            # noqa: BLE001
        pass
    return 0


def build_index(canvas):
    """имя → (виджет, абсолютный rect в базе 1024×768, реально ли ловит мышь)."""
    out = {}
    HID = (unreal.SlateVisibility.COLLAPSED, unreal.SlateVisibility.HIDDEN)

    def walk(w, ox, oy, alive):
        r = slot_rect(w)
        x, y, ww, hh = (ox + r[0], oy + r[1], r[2], r[3]) if r else (ox, oy, 0.0, 0.0)
        try:
            vis = w.get_editor_property("visibility")
        except Exception:                                        # noqa: BLE001
            vis = unreal.SlateVisibility.VISIBLE
        mine = alive and vis not in HID
        out[w.get_name()] = (w, (x, y, ww, hh), mine and vis == unreal.SlateVisibility.VISIBLE)
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                c = w.get_child_at(i)
                if c:
                    walk(c, x, y, mine)
    for i in range(canvas.get_children_count()):
        c = canvas.get_child_at(i)
        if c:
            walk(c, 0.0, 0.0, True)
    return out


def main():
    skins = J.parse_skins_full(SKIN_INI)
    amap = M.asset_map()
    all_roots = {}
    for cls in M.PRIORITY:
        ini = M.ini_of(cls)
        if os.path.isfile(ini):
            all_roots[cls] = J.parse_jui(ini)
    coll = M.collisions(all_roots)

    n_ok = n_miss = n_class = n_geom = n_plane = n_refused_ok = n_refused_bad = 0
    n_cover = 0                     # подпись контрола закрыта своей же кнопкой
    grabs = []
    for cls in M.PRIORITY:
        for root in all_roots.get(cls, []):
            base = M.san(root.idc or root.name)
            J.compute_anchors(root)
            entries, skipped = J.missing_interactive_nodes(root, skins, base)
            if not entries and not skipped:
                continue
            wname = "WBP_" + base + M.wsuffix(base, cls, coll)
            paths = amap.get(wname) or []
            if not paths:
                unreal.log_error("[S2-проба] нет ассета %s" % wname)
                continue
            bp = EAL.load_asset(paths[0])
            canvas = B.get_root_canvas(bp) if bp else None
            if canvas is None:
                unreal.log_error("[S2-проба] нет канваса %s" % paths[0])
                continue
            idx = build_index(canvas)
            order, parent_of, hidden = M.index_nodes(root)

            # 5. отсеянные — узла быть НЕ ДОЛЖНО
            for node, nm, st, _why in skipped:
                bad = [x for x in S.widget_names_of(nm, st, J._iact_planes(node, skins)[1]) if x in idx]
                if bad:
                    n_refused_bad += 1
                    unreal.log_error("[S2-проба] %s: ОТСЕЯННЫЙ контрол %s всё же получил узел %s"
                                     % (wname, nm, ", ".join(bad)))
                else:
                    n_refused_ok += 1

            for node, _p, nm, st, planes in entries:
                want = S.widget_names_of(nm, st, planes)
                l, t, w, h = node.abs
                for k, wn2 in enumerate(want):
                    rec = idx.get(wn2)
                    if rec is None:
                        n_miss += 1
                        unreal.log_error("[S2-проба] %s: НЕТ узла %s (%s)" % (wname, wn2, st))
                        continue
                    wgt, rect, live = rec
                    if not isinstance(wgt, EXPECT[st]):
                        n_class += 1
                        unreal.log_error("[S2-проба] %s / %s: класс %s, ожидался %s"
                                         % (wname, wn2, type(wgt).__name__, EXPECT[st].__name__))
                        continue
                    # 2. геометрия: у радио сверяем только КОНТУР контрола (планы делят его rect)
                    exp = (l, t, w, h)
                    if st == "RadioButton":
                        rx, ry, rw, rh, _p2 = J.plane_screen_rects(planes, w, h)[k]
                        exp = (l + rx, t + ry, rw, rh)
                    if max(abs(a - b) for a, b in zip(exp, rect)) > 0.6:
                        n_geom += 1
                        unreal.log_error("[S2-проба] %s / %s: rect ассета %s ≠ данных %s"
                                         % (wname, wn2, tuple(round(x, 1) for x in rect),
                                            tuple(round(x, 1) for x in exp)))
                        continue
                    # 3. перехват мыши — только если узел реально ловит мышь
                    if live:
                        for on, (ow, orect, olive) in idx.items():
                            if on in want or not olive or not isinstance(ow, IACT_CLASSES):
                                continue
                            a = J._rect_overlap(rect, orect)
                            if a > 4.0:
                                grabs.append("%s: %s накрывает %s (%d px²)"
                                             % (wname, wn2, on, int(a)))
                    n_ok += 1
                # 4. план-картинка контрола должна быть свёрнута
                if (st == "CheckButton" and planes) or (st == "PushButton" and len(planes) == 1):
                    pn = "%s_%d_0" % (M.san(node.name), order[id(node)])
                    rec = idx.get(pn)
                    if rec is not None and rec[2]:
                        n_plane += 1
                        unreal.log_error("[S2-проба] %s: план %s НЕ свёрнут — двойной контур"
                                         % (wname, pn))
                    # 6. ★★★ ПОДПИСЬ КОНТРОЛА ОБЯЗАНА ОСТАТЬСЯ ВИДНА.
                    #   Кисть styled-кнопки непрозрачна, а шаг кладёт кнопку ПОСЛЕДНЕЙ в панель,
                    #   то есть выше подписи. Правило импортёра обратное — «текст рисуется ПОВЕРХ
                    #   кнопки» (`asda_jui_generic_umg.py`, комментарий у цикла `for wgt in made`).
                    #   Значит z-порядок кнопки обязан быть МЕНЬШЕ z-порядка подписи.
                    cn = "%s_%d_txt" % (M.san(node.name), order[id(node)])
                    crec = idx.get(cn)
                    brec = idx.get(want[0]) if want else None
                    if crec is not None and brec is not None:
                        zb, zc = _zorder(brec[0]), _zorder(crec[0])
                        if zb >= zc:
                            n_cover += 1
                            unreal.log_error("[S2-проба] %s / %s: кнопка (z=%d) НЕ НИЖЕ подписи "
                                             "%s (z=%d) — текст был бы закрыт"
                                             % (wname, want[0], zb, cn, zc))

    for g in grabs:
        unreal.log_error("[S2-проба] ★★★ ПЕРЕХВАТ МЫШИ: %s" % g)
    log("ИТОГ: узлов проверено %d, отсутствуют %d, неверный класс %d, расхождение геометрии %d, "
        "не свёрнут план %d, подпись закрыта кнопкой %d, отказ исполнен %d, отказ НАРУШЕН %d, "
        "перехватов мыши %d"
        % (n_ok, n_miss, n_class, n_geom, n_plane, n_cover, n_refused_ok, n_refused_bad, len(grabs)))
    if n_miss or n_class or n_geom or n_plane or n_cover or n_refused_bad or grabs:
        raise RuntimeError("ПРОБА S2 НЕ ПРОШЛА")
    log("ПРОБА S2 ПРОШЛА ПОЛНОСТЬЮ")


if __name__ == "__main__":
    main()
