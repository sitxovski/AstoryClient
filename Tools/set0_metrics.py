# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# set0_metrics.py — СЕТ 0 рендера, офлайн-метрики по кадрам и логам (без UE):
#   * ProfileGPU из логов -game (game_set0_<key>.log): Frame Time, ShadowDepths/VSM, CapsuleShadows,
#     GTAO(AO), Lights, BasePass, PostProcessing — по маркеру «[set0] PROFILE <key> <n> <cam>»;
#   * Э6: гистограмма яркости кадра (доля пикселей < 0.05 в sRGB и в линейной яркости), пипетка
#     «у ног» / «горизонт» по экранным точкам из set0_<key>_cams.json;
#   * Э4: средняя |Δ| двух соседних AO-кадров (ShowFlag.VisualizeSSAO и vis ScreenSpaceAO UV0).
# Выход: set0_metrics.json + печать таблиц Markdown (их вставляет отчёт).
import glob, json, os, re, sys

import numpy as np
from PIL import Image

OUTD = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/render_sets_2026-09-05"
KEYS = ["alpen", "nightvalley", "flamion", "chess"]
ROWS = ["ShadowDepths", "VirtualShadowMaps", "Shadows", "ShadowProjectionOnOpaque", "CapsuleShadows", "GTAO", "LightFunctionAtlasGeneration",
        "Lights", "BasePass", "PrePass", "PostProcessing", "TemporalSuperResolution", "Translucency",
        "ExponentialHeightFog", "VolumetricFog", "LightFunction",
        # ★ Сет E-4 (07.09.2026): локальные света — прямой свет Lumen surface cache и локальные VSM/тени точечных
        "LumenSceneLighting", "LumenSceneDirectLighting", "DirectLighting", "ShadowDepthsFromOpaque", "RenderVirtualShadowMaps", "LumenScene"]

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass


def parse_profiles(log_path):
    """{(n, cam): {"frame_ms":..., rows:{name: ms}}} по маркерам PROFILE."""
    res = {}
    if not os.path.isfile(log_path):
        return res
    lines = open(log_path, encoding="utf-8", errors="replace").read().splitlines()
    cur = None
    for ln in lines:
        m = re.search(r"\[set0\] PROFILE (\S+) (\d+) (\S+)", ln)
        if m:
            cur = (int(m.group(2)), m.group(3))
            res[cur] = {"frame_ms": None, "rows": {}, "draws": None, "res": None}
            continue
        if cur is None:
            continue
        m = re.search(r"Frame Time\s*:\s*([\d.]+)ms", ln)
        if m and res[cur]["frame_ms"] is None:
            res[cur]["frame_ms"] = float(m.group(1))
            continue
        if "LogRHI" not in ln or "┃" not in ln:
            continue
        parts = ln.split("┃")
        if len(parts) < 4:
            continue
        name_cell = parts[3]
        name = name_cell.strip()
        depth = len(name_cell) - len(name_cell.lstrip(" "))
        mm = re.search(r"([\d.]+) ms", parts[2])
        if not mm:
            continue
        ms = float(mm.group(1))
        if name == "<root>":
            d = re.findall(r"\s*(\d+) │", parts[2])
            res[cur]["draws"] = int(d[0]) if d else None
        mr = re.search(r"(\d{3,4})x(\d{3,4})", name)
        if mr and res[cur]["res"] is None and "TemporalSuperResolution" in name:
            res[cur]["res"] = mr.group(0)
        for key in ROWS:
            if name.startswith(key):
                slot = res[cur]["rows"].setdefault(key, {"depth": depth, "ms": 0.0, "n": 0})
                if depth < slot["depth"]:
                    slot.update({"depth": depth, "ms": ms, "n": 1})
                elif depth == slot["depth"]:
                    slot["ms"] += ms
                    slot["n"] += 1
    return res


def srgb_to_linear(c):
    c = c / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def frame_stats(png):
    im = np.asarray(Image.open(png).convert("RGB")).astype(np.float64)
    lum_srgb = 0.2126 * im[..., 0] + 0.7152 * im[..., 1] + 0.0722 * im[..., 2]
    lin = srgb_to_linear(im)
    lum_lin = 0.2126 * lin[..., 0] + 0.7152 * lin[..., 1] + 0.0722 * lin[..., 2]
    hist, _ = np.histogram(lum_srgb, bins=[0, 13, 26, 51, 102, 153, 204, 256])
    return {"size": [im.shape[1], im.shape[0]],
            "mean_srgb": round(float(lum_srgb.mean()), 2),
            "frac_below_0.05_srgb": round(float((lum_srgb < 0.05 * 255).mean()), 4),
            "frac_below_0.05_linear": round(float((lum_lin < 0.05).mean()), 4),
            "frac_below_0.10_linear": round(float((lum_lin < 0.10).mean()), 4),
            "hist_srgb_bins_0-13-26-51-102-153-204-256": [round(float(h) / lum_srgb.size, 4) for h in hist]}


def pipette(png, pt, vp, r=8, dy=0):
    im = np.asarray(Image.open(png).convert("RGB")).astype(np.float64)
    H, W = im.shape[:2]
    if not pt or not vp:
        return None
    x = int(pt[0] * W / vp[0]); y = int(pt[1] * H / vp[1]) + dy
    x = min(max(x, r), W - r - 1); y = min(max(y, r), H - r - 1)
    patch = im[y - r:y + r + 1, x - r:x + r + 1]
    m = patch.reshape(-1, 3).mean(axis=0)
    lin = srgb_to_linear(m)
    return {"px": [x, y], "rgb_srgb": [round(float(v), 1) for v in m],
            "lum_linear": round(float(0.2126 * lin[0] + 0.7152 * lin[1] + 0.0722 * lin[2]), 4)}


def ao_delta(f0, f1, crop_top_frac=0.0, channel=None):
    """channel=None — яркость L (финальный кадр); channel=0 — канал R (vis ScreenSpaceAO: PF_G8 рисуется в R)."""
    if channel is None:
        a = np.asarray(Image.open(f0).convert("L")).astype(np.float64)
        b = np.asarray(Image.open(f1).convert("L")).astype(np.float64)
    else:
        a = np.asarray(Image.open(f0).convert("RGB"))[..., channel].astype(np.float64)
        b = np.asarray(Image.open(f1).convert("RGB"))[..., channel].astype(np.float64)
    if a.shape != b.shape:
        return {"error": "разный размер %s vs %s" % (a.shape, b.shape)}
    y0 = int(a.shape[0] * crop_top_frac)
    d = np.abs(a[y0:] - b[y0:])
    return {"mean_abs_delta_255": round(float(d.mean()), 3), "mean_abs_delta_norm": round(float(d.mean() / 255.0), 5),
            "p95_abs_delta_255": round(float(np.percentile(d, 95)), 2), "frac_changed_gt2": round(float((d > 2).mean()), 4),
            "mean_a": round(float(a[y0:].mean()), 2), "mean_b": round(float(b[y0:].mean()), 2), "size": list(a.shape[::-1])}


def main():
    out = {"profiles": {}, "frames": {}, "ao": {}}
    md_prof = ["| Карта | Ракурс | Разрешение | GPU Frame мс | ShadowDepths (CSM) | ShadowProj (Shadows) | VSM | CapsuleShadows | GTAO (AO) | Lights | BasePass | PostProcess | TSR | Frame мс (avg60, CPU-кадр) |",
               "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    md_fr = ["| Кадр | Размер | Средняя яркость sRGB | доля <0.05 sRGB | доля <0.05 lin | доля <0.10 lin | пипетка у ног (sRGB / lin) | пипетка горизонт (sRGB / lin) |",
             "|---|---|---|---|---|---|---|---|"]
    for key in KEYS:
        cams_path = os.path.join(OUTD, "set0_%s_cams.json" % key)
        if not os.path.isfile(cams_path):
            continue
        cams = json.load(open(cams_path, encoding="utf-8"))
        prof = parse_profiles(os.path.join(OUTD, "game_set0_%s.log" % key))
        for extra in glob.glob(os.path.join(OUTD, "game_set0_%s_*.log" % key)):   # пересъёмка отдельных камер
            prof.update(parse_profiles(extra))
        out["profiles"][key] = {"%d %s" % k: v for k, v in prof.items()}
        for i, c in enumerate(cams["cams"]):
            p = prof.get((i + 1, c["name"]), {})
            rows = p.get("rows", {})

            def g(n):
                return ("%.2f" % rows[n]["ms"]) if n in rows else "—"
            md_prof.append("| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |" % (
                key, c["name"], p.get("res") or "—", ("%.2f" % p["frame_ms"]) if p.get("frame_ms") else "—",
                g("ShadowDepths"), g("Shadows"), g("VirtualShadowMaps"), g("CapsuleShadows"), g("GTAO"), g("Lights"),
                g("BasePass"), g("PostProcessing"), g("TemporalSuperResolution"), c.get("frame_ms_avg60", "—")))
            png = c.get("png")
            if png and os.path.isfile(png):
                fs = frame_stats(png)
                sc = c.get("screen") or {}
                vp = sc.get("viewport")
                fs["pipette_feet"] = pipette(png, sc.get("feet"), vp, dy=-6)
                fs["pipette_horizon"] = pipette(png, sc.get("horizon"), vp, dy=-4)
                out["frames"][os.path.basename(png)] = fs
                pf = fs["pipette_feet"]; ph = fs["pipette_horizon"]
                md_fr.append("| %s | %dx%d | %s | %s | %s | %s | %s | %s |" % (
                    os.path.basename(png), fs["size"][0], fs["size"][1], fs["mean_srgb"], fs["frac_below_0.05_srgb"],
                    fs["frac_below_0.05_linear"], fs["frac_below_0.10_linear"],
                    ("%s / %s" % (pf["rgb_srgb"], pf["lum_linear"])) if pf else "—",
                    ("%s / %s" % (ph["rgb_srgb"], ph["lum_linear"])) if ph else "—"))
    # ao_ssao: ShowFlag.VisualizeSSAO в -game НЕ действует — это два соседних ФИНАЛЬНЫХ кадра (шум TSR);
    # ao_vis: `vis ScreenSpaceAO UV0` — сам AO-буфер в канале R; верхние 30 % (текст VisualizeTexture) отрезаны.
    for tagname, crop, ch in (("ao_ssao", 0.0, None), ("ao_vis", 0.30, 0)):
        f0 = glob.glob(os.path.join(OUTD, "set0_alpen_1_*_%s_f0.png" % tagname))
        f1 = glob.glob(os.path.join(OUTD, "set0_alpen_1_*_%s_f1.png" % tagname))
        if f0 and f1:
            out["ao"][tagname] = ao_delta(f0[0], f1[0], crop, ch)
            out["ao"][tagname]["what"] = "финальный кадр (ShowFlag.VisualizeSSAO без эффекта в -game)" if ch is None else "AO-буфер (vis ScreenSpaceAO, канал R), без верхних 30 %"
            out["ao"][tagname]["files"] = [os.path.basename(f0[0]), os.path.basename(f1[0])]
    with open(os.path.join(OUTD, "set0_metrics.json"), "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=1)
    print("\n".join(md_prof))
    print()
    print("\n".join(md_fr))
    print()
    print(json.dumps(out["ao"], ensure_ascii=False, indent=1))


if __name__ == "__main__":
    main()
