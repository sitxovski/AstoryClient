# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# setA_metrics.py — СЕТ A «цвет света», офлайн-метрики по кадрам (без UE), поверх set0_metrics.py (импортируется
#   ради srgb_to_linear/frame_stats/pipette — у него нет побочных эффектов при импорте).
#   Э1: «пипетка тень↔свет по плоскому террейну». Маска тени/света строится ОДИН раз по кадру «до» (солнце в
#       вариантах не меняется → геометрия теней та же): внутри заданных прямоугольников плоского террейна пиксели
#       ниже 30-го перцентиля линейной яркости = тень, выше 70-го = свет. По каждому кадру «после» той же камеры:
#       средний линейный RGB тени/света → нормированный (÷max) цвет, хрома (max−min), разница нормированных цветов
#       тень↔свет (max|Δ|), яркость тени/света. Значения — из tonemapped PNG (sRGB→linear), т.е. ПОСЛЕ ACES; для
#       сравнения вариантов между собой этого достаточно, абсолюты со сценой не сравнивать.
#   Э6: гистограмма (доля <0.05 lin) и пипетка «у ног»/«горизонт» для nightvalley/chess «до» и «после».
# Выход: setA_metrics.json + печать Markdown-таблиц (их вставляет отчёт).
import glob, json, os, sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import set0_metrics as S0

OUTD = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/render_sets_2026-09-05"
# прямоугольники (x0,y0,x1,y1) плоского террейна с тенью и светом в одном материале — выбраны по кадрам «до»
RECTS = {
    ("alpen", 1): [(700, 650, 1500, 850)],       # плитка поляны, полоса тени центрального дерева
    ("alpen", 2): [(330, 760, 900, 960)],        # плитка, тень левого дерева
    ("alpen", 3): [(900, 650, 1400, 850)],       # плитка, тень фонаря
    ("alpen", 4): [(1000, 600, 1800, 1000)],     # плитка поляны
    ("alpen", 5): [(200, 470, 1200, 1000)],      # трава под кроной (тень) и освещённая полоса
    ("nightvalley", 3): [(0, 520, 1900, 1000)],  # склон долины
    ("flamion", 1): [(300, 560, 800, 1000), (1400, 560, 1900, 1000)],  # красная скала, тень пальмы (без ящиков)
    ("flamion", 2): [(500, 500, 1900, 1000)],    # скала, тёмная полоса тени
}
KEYS = ["alpen", "flamion", "nightvalley", "chess"]

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass


def load_lin(png):
    im = np.asarray(Image.open(png).convert("RGB")).astype(np.float64)
    return S0.srgb_to_linear(im)


def lum(lin):
    return 0.2126 * lin[..., 0] + 0.7152 * lin[..., 1] + 0.0722 * lin[..., 2]


# фильтр материала внутри прямоугольника (по нормированному цвету кадра «до»): чтобы в «тень» не попали кроны/трава/ящики
MATERIAL = {
    "alpen": lambda n: (n[..., 0] > 0.8) & (n[..., 1] > 0.8) & (n[..., 1] < n[..., 0] + 0.12),   # песочная плитка
    "alpen5": lambda n: (n[..., 1] >= 0.99) & (n[..., 0] < 0.8),                                # трава
    "flamion": lambda n: (n[..., 0] >= 0.99) & (n[..., 1] < 0.6),                               # красная скала
    "nightvalley": lambda n: (n[..., 0] >= 0.99) & (n[..., 1] < 0.9),                            # бурая земля
}


def masks_from_before(lin_before, rects, lo=30, hi=70, material=None):
    H, W = lin_before.shape[:2]
    sel = np.zeros((H, W), bool)
    for (x0, y0, x1, y1) in rects:
        sel[y0:y1, x0:x1] = True
    if material is not None:
        n = lin_before / np.maximum(lin_before.max(axis=2, keepdims=True), 1e-6)
        sel &= material(n) & (lum(lin_before) > 1e-4)
    L = lum(lin_before)
    v = L[sel]
    plo, phi = np.percentile(v, lo), np.percentile(v, hi)
    return sel & (L <= plo), sel & (L >= phi)


def patch_stats(lin, mask):
    m = lin[mask].mean(axis=0)
    mx = max(float(m.max()), 1e-9)
    n = m / mx
    return {"rgb_lin": [round(float(x), 5) for x in m], "norm": [round(float(x), 4) for x in n],
            "chroma": round(float(n.max() - n.min()), 4), "lum": round(float(0.2126 * m[0] + 0.7152 * m[1] + 0.0722 * m[2]), 5),
            "n_px": int(mask.sum())}


def e1_row(png, sh_mask, lt_mask):
    lin = load_lin(png)
    s = patch_stats(lin, sh_mask); l = patch_stats(lin, lt_mask)
    gap = max(abs(s["norm"][i] - l["norm"][i]) for i in range(3))
    ratio = s["lum"] / l["lum"] if l["lum"] > 0 else None
    return {"shadow": s, "light": l, "norm_gap": round(gap, 4), "lum_ratio": round(ratio, 4) if ratio is not None else None}


def find_variant_pngs(key, idx):
    out = {}
    for p in glob.glob(os.path.join(OUTD, "set0_%s_%d_after_*.png" % (key, idx))):
        b = os.path.basename(p)
        if b.endswith("_stats.png") or "_ao_" in b:
            continue
        tag = b[len("set0_%s_%d_after_" % (key, idx)):-4]
        out[tag] = p
    return out


def main():
    out = {"e1": {}, "e6": {}}
    md_e1 = ["| Карта/кадр | Вариант | тень: норм. RGB (хрома) | свет: норм. RGB (хрома) | Δнорм тень↔свет | яркость тени/света | PNG |",
             "|---|---|---|---|---|---|---|"]
    for (key, idx), rects in RECTS.items():
        before = os.path.join(OUTD, "set0_%s_%d_before.png" % (key, idx))
        if not os.path.isfile(before):
            continue
        lin_b = load_lin(before)
        mat = MATERIAL.get("alpen5" if (key, idx) == ("alpen", 5) else key)
        sh, lt = masks_from_before(lin_b, rects, material=mat)
        rows = {"before": e1_row(before, sh, lt)}
        for tag, p in sorted(find_variant_pngs(key, idx).items()):
            rows[tag] = e1_row(p, sh, lt)
        out["e1"]["%s_%d" % (key, idx)] = {"rects": rects, "n_shadow": int(sh.sum()), "n_light": int(lt.sum()), "rows": rows}
        for tag in ["before"] + sorted(t for t in rows if t != "before"):
            r = rows[tag]
            md_e1.append("| %s-%d | %s | (%s) χ %.2f | (%s) χ %.2f | **%.3f** | %.2f | set0_%s_%d_%s.png |" % (
                key, idx, tag, ", ".join("%.2f" % v for v in r["shadow"]["norm"]), r["shadow"]["chroma"],
                ", ".join("%.2f" % v for v in r["light"]["norm"]), r["light"]["chroma"], r["norm_gap"], r["lum_ratio"] or 0,
                key, idx, "before" if tag == "before" else "after_" + tag))
    # Э6: ночные карты «до» / «после»
    md_e6 = ["| Кадр | доля <0.05 lin | доля <0.10 lin | средняя яркость sRGB | пипетка у ног (sRGB / lin) | пипетка горизонт (sRGB / lin) |",
             "|---|---|---|---|---|---|"]
    for key in ("nightvalley", "chess"):
        cams = json.load(open(os.path.join(OUTD, "set0_%s_cams.json" % key), encoding="utf-8"))
        for i, c in enumerate(cams["cams"]):
            sc = c.get("screen") or {}
            for tag in ["before"] + sorted(find_variant_pngs(key, i + 1).keys()):
                p = os.path.join(OUTD, "set0_%s_%d_%s.png" % (key, i + 1, "before" if tag == "before" else "after_" + tag))
                if not os.path.isfile(p):
                    continue
                fs = S0.frame_stats(p)
                fs["pipette_feet"] = S0.pipette(p, sc.get("feet"), sc.get("viewport"), dy=-6)
                fs["pipette_horizon"] = S0.pipette(p, sc.get("horizon"), sc.get("viewport"), dy=-4)
                out["e6"][os.path.basename(p)] = fs
                pf = fs["pipette_feet"]; ph = fs["pipette_horizon"]
                md_e6.append("| %s | %s | %s | %s | %s | %s |" % (
                    os.path.basename(p), fs["frac_below_0.05_linear"], fs["frac_below_0.10_linear"], fs["mean_srgb"],
                    ("%s / %s" % (pf["rgb_srgb"], pf["lum_linear"])) if pf else "—",
                    ("%s / %s" % (ph["rgb_srgb"], ph["lum_linear"])) if ph else "—"))
    with open(os.path.join(OUTD, "setA_metrics.json"), "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=1)
    print("\n".join(md_e1)); print(); print("\n".join(md_e6))


if __name__ == "__main__":
    main()
