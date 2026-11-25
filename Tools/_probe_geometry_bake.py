# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# ПРОБА запечённой геометрии (★ ВТОРОЙ МЕТОД — не тем кодом, что писал).
#   Скрипт НЕ импортирует `asda_ui_geometry_bake`: он заново читает ассеты и сверяет числа с
#   ОЖИДАНИЯМИ, выписанными из дампа ДО правки и из снятого C++-кода. Плюс прогоняет
#   `asda_ui_post_import.check_handmade()` — сторожа обязаны быть на месте.
# Запуск: Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\_probe_geometry_bake.py
import importlib.util
import os

import unreal

EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder
HERE = os.path.dirname(os.path.abspath(__file__))
EPS = 0.02
bad = []


def log(m):
    unreal.log("[проба-геом] %s" % m)


def fail(m):
    bad.append(m)
    unreal.log_warning("[проба-геом] РАСХОЖДЕНИЕ: %s" % m)


def walk(root):
    st, out = [root], []
    while st:
        w = st.pop()
        if not w:
            continue
        out.append(w)
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                st.append(w.get_child_at(i))
    return out


def nodes_of(path):
    bp = EAL.load_asset(path)
    if not bp:
        fail("ассет %s не грузится" % path)
        return []
    root = B.get_root_canvas(bp)
    return walk(root) if root else []


def by_name(nodes, n):
    for w in nodes:
        try:
            if w.get_name() == n:
                return w
        except Exception:
            pass
    return None


def geo(w):
    s = w.get_editor_property("slot")
    if not isinstance(s, unreal.CanvasPanelSlot):
        return None
    lay = s.get_editor_property("layout_data")
    a = lay.get_editor_property("anchors")
    o = lay.get_editor_property("offsets")
    return (a.minimum.x, a.minimum.y, a.maximum.x, a.maximum.y,
            o.left, o.top, o.right, o.bottom)


def expect(w, name, want):
    if not w:
        fail("узла %s нет" % name)
        return
    g = geo(w)
    if g is None:
        fail("%s: не канвас-слот" % name)
        return
    for i, (got, exp) in enumerate(zip(g, want)):
        if exp is None:
            continue
        if abs(got - exp) > EPS:
            fail("%s поле %d: %.3f, ждали %.3f" % (name, i, got, exp))
            return
    log("%s: ок %s" % (name, ["%.2f" % x for x in g]))


def main():
    # ── G1. EXP-бар ──────────────────────────────────────────────────────────
    n = nodes_of("/Game/AsdaAssets/UI/Player_UI/HUD/WBP_EXPBar_WIn")
    # Было (дамп до правки): подложка POINT 0,0 off(0, 2, 1034.2, 8); рамка off(0, 0, 1044.6, 2).
    expect(by_name(n, "ExpBar_Base_1_0"), "ExpBar_Base_1_0", (0, 0, 1, 0, 0, 2, 0, 8))
    expect(by_name(n, "ExpBar_TopLine_2_0"), "ExpBar_TopLine_2_0", (0, 0, 1, 0, 0, 0, 0, 2))
    for i in range(11):
        nm = "Box_DarkGrey_%d_0" % (5 + i)
        fx = i / 10.0
        left = -2.0 if i == 10 else (-1.0 if i else 0.0)
        expect(by_name(n, nm), nm, (fx, 0, fx, 0, left, 0, 2, 7))

    # ── G2. LOGIN_CHECK_HIT ──────────────────────────────────────────────────
    n = nodes_of("/Game/AsdaAssets/UI/Login/WBP_LOGIN_WIN")
    w = by_name(n, "LOGIN_CHECK_HIT")
    expect(w, "LOGIN_CHECK_HIT", (0, 0, 0, 0, 0, 4, 100, 21))   # слот плана LOGIN_CHECK_BASE_18_0
    if w and not isinstance(w, unreal.Button):
        fail("LOGIN_CHECK_HIT: класс %s, ждали Button" % type(w).__name__)
    if w and w.get_parent() and w.get_parent().get_name() != "LOGIN_CHECK_BASE_grp":
        fail("LOGIN_CHECK_HIT: родитель %s" % w.get_parent().get_name())

    # ── G3. титулы этапов ────────────────────────────────────────────────────
    n = nodes_of("/Game/AsdaAssets/UI/Login/WBP_SERVER_SELECT_WIN")
    for nm, par in (("SERVER_TitleTxt", "SERVER_LIST_grp"), ("CHANNEL_TitleTxt", "CHANNEL_LIST_grp")):
        w = by_name(n, nm)
        expect(w, nm, (0, 0, 0, 0, 5, 0, 210, 29))              # слот бара SERVER_LIST_1_0
        if w and not isinstance(w, unreal.TextBlock):
            fail("%s: класс %s, ждали TextBlock" % (nm, type(w).__name__))
        if w and w.get_parent() and w.get_parent().get_name() != par:
            fail("%s: родитель %s, ждали %s" % (nm, w.get_parent().get_name(), par))

    # ── G4. клик-реле ячеек журнала ──────────────────────────────────────────
    n = nodes_of("/Game/AsdaAssets/UI/Player_UI/Quest/WBP_QUEST_WIN")
    got = 0
    for i in range(12):
        nm = "QUEST_INVEN%d_HIT" % i
        w = by_name(n, nm)
        if not w:
            fail("нет %s" % nm)
            continue
        # Сетка 6×2 с шагом 33 px — независимый пересчёт, а не копия из шага.
        expect(w, nm, (0, 0, 0, 0, 33.0 * (i % 6), 33.0 * (i // 6), 32, 32))
        if not isinstance(w, unreal.Button):
            fail("%s: класс %s" % (nm, type(w).__name__))
        got += 1
    log("G4: реле найдено %d/12" % got)

    # ── G5. подписи имён коробки ─────────────────────────────────────────────
    n = nodes_of("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_Package_Contents_Win")
    labels = 0
    for w in n:
        if not isinstance(w, unreal.TextBlock):
            continue
        g = geo(w)
        if g and abs(g[6] - 276.0) < 1.0:
            labels += 1
            if abs(g[5] - 68.0) > EPS or abs(g[7] - 20.0) > EPS:
                fail("%s: off T=%.1f B=%.1f, ждали 68/20" % (w.get_name(), g[5], g[7]))
    log("G5: подписей шириной 276 = %d (ждём 20 = 19 запечённых + образец Package_BagName0_Text)" % labels)
    if labels != 20:
        fail("подписей коробки %d, ждали 20" % labels)

    # ── сторожа пост-импорта ─────────────────────────────────────────────────
    spec = importlib.util.spec_from_file_location("pi_probe", os.path.join(HERE, "asda_ui_post_import.py"))
    pi = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pi)
    miss = pi.check_handmade()          # возвращает ЧИСЛО потерянных узлов
    if miss:
        fail("сторожа пост-импорта: потеряно узлов %d (подробности — строками LogPython Error выше)" % miss)
    else:
        log("сторожа HANDMADE: все на месте")

    log("=== ИТОГ: расхождений %d ===" % len(bad))


if __name__ == "__main__":
    main()
