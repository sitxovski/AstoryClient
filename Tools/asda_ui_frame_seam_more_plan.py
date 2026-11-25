# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""Пачка 19, баг 4 (продолжение, 09.11.2026): ПЛАНИРОВЩИК второго плана швов составных рамок. Только чтение ассетов.

ВХОД: таблица сканера рендером `asda.UI.FrameSeamScanSelfTest` (`Reports/batch19/seam_scan/<tag>_ru.tsv`): экран, окно,
рамка (`родитель|база`), пара кусков, ось — там, где между кусками НА САМОМ ДЕЛЕ виден просвет (пурпур подложки).
ВЫХОД: план `Tools/data/ui_frame_seam_more.json` в формате плана шага 24.10 (`asda_ui_frame_seam_overlap`): для каждой стороны с
просветом средний кусок уходит под соседа на 1 (точечный анкер: Left −1 / ширина +1 и т. п.), ZOrder −1 у края середины, −2 у тела;
«было» — ТЕКУЩИЙ вид куска (у рамок, частично поправленных 24.10, это его «стало»). Не в план: рамки с растянутыми анкерами (это
шаг `asda_ui_frame_seam_span`), рамки, перед которыми в родителе нарисован чужой узел в их области или есть узел с z<0 (понижение
z поменяло бы порядок) — они в отчёт «осмотреть». Отчёт: `Reports/batch19/seam_scan/plan_more.txt`.
Запуск: ASDA_SEAMPLAN_SCAN=<tsv> python Tools/run_headless_safe.py Tools/asda_ui_frame_seam_more_plan.py --marker "ПЛАН-ШВОВ: ГОТОВО"
"""
import json
import os
import traceback

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
PLAN_OUT = os.path.join(HERE, "data", "ui_frame_seam_more.json")
REPORT = os.path.normpath(os.path.join(HERE, "..", "..", "Reports", "batch19", "seam_scan", "plan_more.txt"))


def log(m):
    """Строка журнала."""
    unreal.log("[план-швов] " + str(m))


def slot_of(w):
    """(анкеры, поля, alignment, z) слота канваса."""
    s = w.get_editor_property("slot")
    lay = s.get_editor_property("layout_data")
    a = lay.get_editor_property("anchors")
    mn, mx = a.get_editor_property("minimum"), a.get_editor_property("maximum")
    o = lay.get_editor_property("offsets")
    al = lay.get_editor_property("alignment")
    return (mn.x, mn.y, mx.x, mx.y), [o.left, o.top, o.right, o.bottom], (al.x, al.y), int(s.get_editor_property("z_order"))


def side_of(pair, axis, n):
    """Пара соседей → (кусок середины, сторона)."""
    a, b = pair
    if n == 9:
        if axis == "x":
            return (b, "L") if b % 3 == 1 else (a, "R")
        return (b, "T") if b // 3 == 1 else (a, "B")
    if axis == "x":
        return (1, "L") if b == 1 else (1, "R")
    return (1, "T") if b == 1 else (1, "B")


def find(root, name):
    """Узел по имени в дереве."""
    stack = [root]
    while stack:
        w = stack.pop()
        if w.get_name() == name:
            return w
        if isinstance(w, unreal.PanelWidget):
            stack += [w.get_child_at(i) for i in range(w.get_children_count()) if w.get_child_at(i)]
    return None


def main():
    """Точка входа."""
    scan = os.environ.get("ASDA_SEAMPLAN_SCAN")
    rows = [l.rstrip("\n").split("\t") for l in open(scan, encoding="utf-8")][1:]
    want = {}   # (окно, родитель, база) → {(кусок, сторона): [экраны]}
    for r in rows:
        if len(r) < 7:
            continue
        res, win, frame, pair, axis = r[0], r[1].split("@")[0], r[2], r[3], r[4]
        par, base = frame.split("|", 1)
        a, b = (int(x) for x in pair.split("|"))
        want.setdefault((win, par, base), {}).setdefault((a, b, axis), set()).add(res + ("" if "@" not in r[1] else " сдвиг"))
    EAL = unreal.EditorAssetLibrary
    paths = {p.split(".")[0].rsplit("/", 1)[-1]: p.split(".")[0]
             for p in EAL.list_assets("/Game/AsdaAssets/UI", recursive=True, include_folder=False)}
    plan, report = [], []
    for (win, par, base), pairs in sorted(want.items()):
        path = paths.get(win)
        bp = EAL.load_asset(path) if path else None
        root = unreal.AsdaUmgBuilder.get_root_canvas(bp) if bp else None
        parent = root if (root and par == root.get_name()) else (find(root, par) if root else None)
        if not parent:
            report.append("%s %s|%s: родитель не найден — осмотреть" % (win, par, base))
            continue
        kids = [parent.get_child_at(i) for i in range(parent.get_children_count())]
        pieces = {}
        for i, w in enumerate(kids):
            if w and w.get_name().startswith(base + "_") and w.get_name()[len(base) + 1:].isdigit():
                pieces[int(w.get_name()[len(base) + 1:])] = (w, i)
        n = len(pieces)
        if n not in (3, 9):
            report.append("%s %s: кусков %d — осмотреть" % (win, base, n))
            continue
        sl = {k: slot_of(w) for k, (w, i) in pieces.items()}
        if any(abs(s[0][0] - s[0][2]) > 1e-6 or abs(s[0][1] - s[0][3]) > 1e-6 for s in sl.values()):
            report.append("%s %s: растянутые анкеры — шаг asda_ui_frame_seam_span" % (win, base))
            continue
        # Порядок: чужой узел раньше рамки в её области или узел с z<0 — понижение z поменяло бы порядок.
        first = min(i for (w, i) in pieces.values())
        x0 = min(sl[k][1][0] for k in sl) - 1
        y0 = min(sl[k][1][1] for k in sl) - 1
        x1 = max(sl[k][1][0] + sl[k][1][2] for k in sl) + 1
        y1 = max(sl[k][1][1] + sl[k][1][3] for k in sl) + 1
        anc0 = sl[0][0]
        early, neg = [], []
        for i, w in enumerate(kids):
            if not w or any(w is p[0] for p in pieces.values()):
                continue
            try:
                a2, o2, _al, z2 = slot_of(w)
            except Exception:                                             # noqa: BLE001
                continue
            if z2 < 0:
                neg.append(w.get_name())
            if i < first and (abs(a2[0] - a2[2]) > 1e-6 or abs(a2[1] - a2[3]) > 1e-6 or a2 != anc0
                              or not (o2[0] >= x1 or o2[0] + o2[2] <= x0 or o2[1] >= y1 or o2[1] + o2[3] <= y0)):
                early.append(w.get_name())
        if early or neg:
            report.append("%s %s: осмотреть — раньше рамки нарисованы %s; z<0 у %s" % (win, base, early[:4], neg[:4]))
            continue
        sides = {}
        for (a, b, axis), where in pairs.items():
            k, sd = side_of((a, b), axis, n)
            sides.setdefault(k, set()).add(sd)
        pcs = {}
        for k, sds in sides.items():
            offs = list(sl[k][1])
            z_old = sl[k][3]
            if "L" in sds:
                offs[0] -= 1
                offs[2] += 1
            if "R" in sds:
                offs[2] += 1
            if "T" in sds:
                offs[1] -= 1
                offs[3] += 1
            if "B" in sds:
                offs[3] += 1
            if n == 9:
                z_new = -2 if k == 4 else -1
            else:
                z_new = -1
            z_new = min(z_new, z_old)
            pcs[base + "_" + str(k)] = dict(old=sl[k][1], z_old=z_old, new=offs, z_new=z_new)
        # Соседи-середины, которые сами не правятся, но теперь должны лечь ниже углов: z куска 4 / краёв — уже учтён выше.
        plan.append(dict(path=path, win=win, frame=base, kind=n, orient="", tex="", pieces=pcs))
        report.append("%s %s: в план — %s (экраны: %s)" % (win, base, ", ".join("%s:%s" % (k, "".join(sorted(v))) for k, v in sorted(sides.items())),
                                                        "; ".join(sorted({x for v in pairs.values() for x in v}))[:160]))
    json.dump(dict(frames=plan), open(PLAN_OUT, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    os.makedirs(os.path.dirname(REPORT), exist_ok=True)
    open(REPORT, "w", encoding="utf-8").write("\n".join(report) + "\n")
    log("рамок со швом %d: в план %d, осмотреть/иное %d" % (len(want), len(plan), len(want) - len(plan)))
    log("ПЛАН-ШВОВ: ГОТОВО")


if __name__ == "__main__":
    try:
        main()
    except Exception:
        unreal.log_error("[план-швов] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
