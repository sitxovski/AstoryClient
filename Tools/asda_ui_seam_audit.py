# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""Пачка 19, баг 4 (продолжение, 09.11.2026): АУДИТ ШВОВ И ТОНКИХ КУСКОВ ВО ВСЕХ ОКНАХ. Только чтение.

Для каждого WBP под /Game/AsdaAssets/UI раскладывает дерево канвасов по правилам UMG на нескольких экранах (без запуска игры):
точечная ось — позиция = родитель + анкер·размер + Left − alignment·ширина, ширина = Right; растянутая — поля от анкеров.
Находит:
  A) СОСТАВНЫЕ РАМКИ: картинки одного родителя `<база>_<k>` (k = 0…8 или 0…2) — их стыки; «шов» = общий край двух соседних
     кусков на дробном пикселе хотя бы на одном экране (Slate округляет куски отдельно) либо зазор > 0.01 px;
  B) ТОНКИЕ КУСКИ: видимая картинка уже/ниже 3 px на базе 1024×768;
  C) КУСКИ ЗА ОКНОМ: видимая картинка, вылезающая за прямоугольник рамки окна (наибольшая составная рамка корня).
Вывод: `Reports/batch19/seam_audit/audit.tsv` и сводка в журнал. ASDA_SEAMAUDIT_ONLY=<WBP_a,WBP_b> — только эти окна;
ASDA_SEAMAUDIT_ALLNODES=1 — печатать прямоугольники всех картинок выбранных окон.
Запуск: python Tools/run_headless_safe.py Tools/asda_ui_seam_audit.py --marker "АУДИТ-ШВОВ: ГОТОВО"
"""
import os
import re
import traceback

import unreal

VIEWS = [(1024, 768), (1280, 720), (1366, 768), (1600, 900), (1920, 1080), (1280, 1024), (2560, 1440), (3840, 2160), (1680, 1050)]
OUT_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Reports", "batch19", "seam_audit"))
PIECE = re.compile(r"^(.*)_([0-8])$")


def log(m):
    """Строка журнала."""
    unreal.log("[аудит-швов] " + str(m))


def slot_data(w):
    """(анкеры min/max, поля, alignment) слота канваса или None."""
    try:
        s = w.get_editor_property("slot")
        lay = s.get_editor_property("layout_data")
    except Exception:                                                     # noqa: BLE001
        return None
    a = lay.get_editor_property("anchors")
    mn, mx = a.get_editor_property("minimum"), a.get_editor_property("maximum")
    o = lay.get_editor_property("offsets")
    al = lay.get_editor_property("alignment")
    return (mn.x, mn.y, mx.x, mx.y), (o.left, o.top, o.right, o.bottom), (al.x, al.y)


def place(par, sd):
    """Прямоугольник ребёнка (x0, y0, x1, y1) в родителе par по данным слота."""
    (ax0, ay0, ax1, ay1), (l, t, r, b), (alx, aly) = sd
    pw, ph = par[2] - par[0], par[3] - par[1]
    if abs(ax0 - ax1) < 1e-6:
        x0 = par[0] + ax0 * pw + l - alx * r
        x1 = x0 + r
    else:
        x0 = par[0] + ax0 * pw + l
        x1 = par[0] + ax1 * pw - r
    if abs(ay0 - ay1) < 1e-6:
        y0 = par[1] + ay0 * ph + t - aly * b
        y1 = y0 + b
    else:
        y0 = par[1] + ay0 * ph + t
        y1 = par[1] + ay1 * ph - b
    return (x0, y0, x1, y1)


def visible(w):
    """Картинка показана в ассете (не Collapsed/Hidden)."""
    v = str(w.get_editor_property("visibility"))
    return "COLLAPSED" not in v and "HIDDEN" not in v


def layout(root, view):
    """Имя → (виджет, родитель-имя, прямоугольник, видим цепочкой) для всех узлов дерева на экране view."""
    out = {}

    def walk(w, rect, vis_chain, parent_name):
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                c = w.get_child_at(i)
                if not c:
                    continue
                sd = slot_data(c)
                cr = place(rect, sd) if sd else rect
                cv = vis_chain and visible(c)
                out[c.get_name()] = (c, w.get_name(), cr, cv, i)
                walk(c, cr, cv, w.get_name())

    walk(root, (0.0, 0.0, float(view[0]), float(view[1])), True, "-")
    return out


def frac(v):
    """Дробная часть числа (0 — целое) с допуском."""
    f = abs(v - round(v))
    return 0.0 if f < 0.01 else f


def audit_window(path, only_all):
    """Строки аудита одного окна."""
    bp = unreal.EditorAssetLibrary.load_asset(path)
    if not bp:
        return [], []
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    if not root:
        return [], []
    name = path.rsplit("/", 1)[-1]
    lays = {v: layout(root, v) for v in VIEWS}
    base = lays[VIEWS[0]]
    rows, nodes = [], []
    # A) составные рамки
    groups = {}
    for n, (w, par, r, vis, idx) in base.items():
        if not isinstance(w, unreal.Image):
            continue
        m = PIECE.match(n)
        if m:
            groups.setdefault((par, m.group(1)), {})[int(m.group(2))] = n
    frames = []
    for (par, b), pcs in groups.items():
        ks = sorted(pcs)
        if ks == list(range(9)):
            grid = [[0, 1, 2], [3, 4, 5], [6, 7, 8]]
        elif ks == list(range(3)):
            grid = None
        else:
            continue
        frames.append((par, b, pcs, grid))
    root_frame = None
    for par, b, pcs, grid in frames:
        if grid and par == root.get_name():
            r0 = base[pcs[0]][2]
            r8 = base[pcs[8]][2]
            area = (r8[2] - r0[0]) * (r8[3] - r0[1])
            if not root_frame or area > root_frame[0]:
                root_frame = (area, b, pcs)
    for par, b, pcs, grid in frames:
        seams = []
        pairs = []
        if grid:
            for rr in grid:
                pairs += [(rr[0], rr[1], "x"), (rr[1], rr[2], "x")]
            for c in range(3):
                pairs += [(grid[0][c], grid[1][c], "y"), (grid[1][c], grid[2][c], "y")]
        else:
            r0, r1 = base[pcs[0]][2], base[pcs[1]][2]
            ax = "x" if abs(r1[0] - r0[2]) <= abs(r1[1] - r0[3]) else "y"
            pairs = [(0, 1, ax), (1, 2, ax)]
        vis_all = all(base[pcs[k]][3] for k in pcs)
        for a, c, ax in pairs:
            for v in VIEWS:
                ra, rc = lays[v][pcs[a]][2], lays[v][pcs[c]][2]
                ea, sc = (ra[2], rc[0]) if ax == "x" else (ra[3], rc[1])
                if sc - ea > 0.01:
                    seams.append("%d|%d %s зазор %.2f @%dx%d" % (a, c, ax, sc - ea, v[0], v[1]))
                    break
                if abs(sc - ea) <= 0.01 and frac(ea) > 0:
                    seams.append("%d|%d %s дробь %.2f @%dx%d" % (a, c, ax, ea, v[0], v[1]))
                    break
        kinds = []
        for k in sorted(pcs):
            sd = slot_data(base[pcs[k]][0])
            (ax0, ay0, ax1, ay1) = sd[0]
            kinds.append(("S" if abs(ax0 - ax1) > 1e-6 else "P") + ("S" if abs(ay0 - ay1) > 1e-6 else "P"))
        rows.append((name, "рамка", par, b, len(pcs), "+".join(sorted(set(kinds))), "видима" if vis_all else "скрыта",
                     "; ".join(seams) if seams else "—"))
    # B/C) тонкие куски и куски за окном
    if root_frame:
        pcs = root_frame[2]
        fr = (base[pcs[0]][2][0], base[pcs[0]][2][1], base[pcs[8]][2][2], base[pcs[8]][2][3])
    else:
        fr = None
    for n, (w, par, r, vis, idx) in base.items():
        if not isinstance(w, unreal.Image) or not vis:
            continue
        wdt, hgt = r[2] - r[0], r[3] - r[1]
        if (0 < wdt <= 3.0 and hgt > 6) or (0 < hgt <= 3.0 and wdt > 6):
            rows.append((name, "тонкий", par, n, 1, "%.1fx%.1f" % (wdt, hgt), "@%.0f,%.0f" % (r[0], r[1]), "—"))
        if fr and (r[2] > fr[2] + 0.5 or r[0] < fr[0] - 0.5 or r[3] > fr[3] + 0.5 or r[1] < fr[1] - 0.5) and wdt > 0 and hgt > 0:
            rows.append((name, "за-окном", par, n, 1, "%.1f,%.1f-%.1f,%.1f" % r, "рамка %.1f,%.1f-%.1f,%.1f" % fr, "—"))
        if only_all:
            nodes.append("%s %s %s %.2f,%.2f-%.2f,%.2f vis=%s" % (name, n, par, r[0], r[1], r[2], r[3], vis))
    return rows, nodes


def main():
    """Точка входа."""
    only = [s.strip() for s in os.environ.get("ASDA_SEAMAUDIT_ONLY", "").split(",") if s.strip()]
    only_all = bool(os.environ.get("ASDA_SEAMAUDIT_ALLNODES"))
    paths = [p.split(".")[0] for p in unreal.EditorAssetLibrary.list_assets("/Game/AsdaAssets/UI", recursive=True, include_folder=False)]
    paths = sorted(set(p for p in paths if p.rsplit("/", 1)[-1].startswith("WBP_")))
    if only:
        paths = [p for p in paths if p.rsplit("/", 1)[-1] in only]
    os.makedirs(OUT_DIR, exist_ok=True)
    allrows, allnodes = [], []
    for p in paths:
        try:
            r, n = audit_window(p, only_all)
        except Exception:                                                 # noqa: BLE001
            log("ОШИБКА %s:\n%s" % (p, traceback.format_exc()))
            continue
        allrows += r
        allnodes += n
    suffix = ("_" + "_".join(only)) if only else ""
    with open(os.path.join(OUT_DIR, "audit%s.tsv" % suffix), "w", encoding="utf-8") as f:
        f.write("окно\tвид\tродитель\tузел\tкусков\tанкеры/размер\tвидимость/место\tшвы\n")
        for r in allrows:
            f.write("\t".join(str(x) for x in r) + "\n")
    if allnodes:
        with open(os.path.join(OUT_DIR, "nodes%s.txt" % suffix), "w", encoding="utf-8") as f:
            f.write("\n".join(allnodes))
    fr = [r for r in allrows if r[1] == "рамка"]
    log("окон %d, составных рамок %d (со швом %d), тонких кусков %d, кусков за окном %d" % (
        len(paths), len(fr), sum(1 for r in fr if r[7] != "—"), sum(1 for r in allrows if r[1] == "тонкий"),
        sum(1 for r in allrows if r[1] == "за-окном")))
    log("АУДИТ-ШВОВ: ГОТОВО")


if __name__ == "__main__":
    try:
        main()
    except Exception:
        unreal.log_error("[аудит-швов] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
