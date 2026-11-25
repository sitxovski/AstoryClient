# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_skillglow_metrics.py — ЧИСЛА ЯРКОСТИ ЭФФЕКТОВ ПО КАДРАМ asda_skillglow_shot.py (04.11.2026). [ПОРТ]
#
#   Кадр эффекта сравнивается с контрольным кадром того же пресета (та же камера, эффекта нет):
#     area     — доля пикселей кадра, которые эффект изменил (|ΔY| > 8 из 255): след эффекта вместе с ореолом;
#     clip     — доля пикселей следа, выбитых в белое (все три канала ≥ 245) — «пересвет»;
#     dY       — средний прирост яркости по следу (0..255);
#     halo     — доля следа со слабым приростом 8 < ΔY ≤ 40 — мягкий ореол bloom вокруг ядра;
#     light    — средний прирост яркости следа, который даёт эффект-свет (кадр со светом минус кадр без света).
#   Выход: <папка>/metrics.csv и сводка в консоль (среднее по эффектам на каждую пару «карта × пресет × тег»).
#   Запуск: python Tools\asda_skillglow_metrics.py <папка кадров> [<папка кадров> …]
#   ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
import collections
import csv
import glob
import os
import re
import sys


def lum(img):
    """Яркость Y (Rec.601) как массив 0..255 и маска «белый» (все каналы ≥ 245)."""
    import numpy as np
    a = np.asarray(img.convert("RGB"), dtype=np.float32)
    y = 0.299 * a[..., 0] + 0.587 * a[..., 1] + 0.114 * a[..., 2]
    white = (a >= 245).all(axis=-1)
    return y, white


def measure(fx, ctrl, nolight=None):
    """Числа одного кадра эффекта относительно контроля (и вклад света, если есть кадр без света)."""
    import numpy as np
    from PIL import Image
    yf, wf = lum(Image.open(fx))
    yc, _ = lum(Image.open(ctrl))
    d = yf - yc
    m = np.abs(d) > 8.0
    n = int(m.sum())
    res = {"area": n / m.size, "clip": float((wf & m).sum()) / max(n, 1), "dY": float(d[m].mean()) if n else 0.0,
           "halo": float(((d > 8.0) & (d <= 40.0)).sum()) / max(n, 1), "light": ""}
    if nolight and os.path.exists(nolight):
        yn, _ = lum(Image.open(nolight))
        res["light"] = float((yf - yn)[m].mean()) if n else 0.0
    return res


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    rx = re.compile(r"^(?P<map>[a-z]+)_p(?P<p>\d)_(?P<jeb>.+?)_(?P<kind>f\d|nolight)_(?P<tag>[a-z0-9]+)\.png$")
    for d in sys.argv[1:]:
        rows = []
        # Кадры самотеста asda.Skill.SelfTestAnimAudit (shot=<папка>): w<подтип>_<пол>_<навык>_<фаза>.png + w<подтип>_<пол>_control.png;
        #   карта/пресет/тег — в имени папки (<карта>_p<N>_<тег>).
        dm = re.match(r"^(?P<map>[a-z]+)_p(?P<p>\d)_(?P<tag>[a-z0-9]+)$", os.path.basename(os.path.normpath(d)))
        rxa = re.compile(r"^w(?P<w>\d+)_(?P<g>[mf]|self)_(?P<sk>\d+)_(?P<k>\d)\.png$")
        for f in sorted(glob.glob(os.path.join(d, "*.png"))):
            ma = rxa.match(os.path.basename(f))
            if ma and dm:
                ctrl = os.path.join(d, "w%s_%s_control.png" % (ma.group("w"), ma.group("g")))
                if os.path.exists(ctrl):
                    r = measure(f, ctrl)
                    rows.append(dict(map=dm.group("map"), preset=int(dm.group("p")), jeb="skill%s_w%s" % (ma.group("sk"), ma.group("w")),
                                     phase="f%s" % ma.group("k"), tag=dm.group("tag"), **r))
                continue
            m = rx.match(os.path.basename(f))
            if not m or m.group("kind") == "nolight":
                continue
            ctrl = os.path.join(d, "%s_p%s_control_%s.png" % (m.group("map"), m.group("p"), m.group("tag")))
            if not os.path.exists(ctrl):
                continue
            nol = os.path.join(d, "%s_p%s_%s_nolight_%s.png" % (m.group("map"), m.group("p"), m.group("jeb"), m.group("tag")))
            r = measure(f, ctrl, nol if m.group("kind") == "f0" else None)
            rows.append(dict(map=m.group("map"), preset=int(m.group("p")), jeb=m.group("jeb"), phase=m.group("kind"),
                             tag=m.group("tag"), **r))
        with open(os.path.join(d, "metrics.csv"), "w", encoding="utf-8-sig", newline="") as fh:
            w = csv.DictWriter(fh, fieldnames=["map", "preset", "jeb", "phase", "tag", "area", "clip", "dY", "halo", "light"])
            w.writeheader()
            w.writerows(rows)
        agg = collections.defaultdict(list)
        for r in rows:
            agg[(r["map"], r["tag"], r["preset"])].append(r)
        print("== %s (%d кадров)" % (d, len(rows)))
        for k in sorted(agg):
            L = agg[k]
            lights = [r["light"] for r in L if r["light"] != ""]
            print("  %-8s %-7s пресет %d: след %.3f, белое %.1f%%, ΔY %.1f, ореол %.1f%%, свет %.1f" % (
                k[0], k[1], k[2], sum(r["area"] for r in L) / len(L), 100 * sum(r["clip"] for r in L) / len(L),
                sum(r["dY"] for r in L) / len(L), 100 * sum(r["halo"] for r in L) / len(L),
                sum(lights) / len(lights) if lights else 0.0))


if __name__ == "__main__":
    main()
