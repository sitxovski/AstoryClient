# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ground_color.py — СРЕДНИЙ ЦВЕТ ЗЕМЛИ карты + нормированные цвета солнца/тумана для Sky-тинта
#   (Сет A «цвет света», roundtable_2026-09-04/FINAL_synthesis.md §4; формулы — developer_round2.md §1.1/§1.2).
#   СИСТЕМНЫЙ Python (PIL + numpy), БЕЗ UE. Ничего в проекте не меняет: пишет только
#   `export/maps/ground_colors.json` и (по --table) `export/maps/sky_tint_table.md`.
#
# Что считает на карту (все цвета — ЛИНЕЙНЫЙ RGB 0..1, как их видит UE-материал):
#   ground        — Σ_patch w_p · lerp( lerp(mean D0_p, mean D1_p, meanMaskA_p·HasD1_p), Colormap_p ) / Σ w_p,
#                   где второй lerp — попиксельно по альфе колормапа (в мастере M_AsdaTerrainLit:
#                   g1 = lerp(Detail0, Detail1, Mask.A·HasD1); g2 = lerp(g1, Colormap.RGB, Colormap.A) —
#                   asda_fix_terrainlit_vertexcolor.py:100-102). Detail-тайлы патча — из справки
#                   export/maps/ter_details.json (приоритет, см. asda_build_map_level._ter_patch), иначе из .mapraw.
#                   Веса w_p — площадь патча по XZ-габариту (все патчи 65×65 равны ⇒ ≈ равные веса; --equal — поровну).
#   ground_norm   — ground / max(ground)  (яркость снимаем: интенсивность полусферы задаёт LOWER_K·mx в map_lit).
#   desat50       — norm( lerp(ground_norm, luma(ground_norm), 0.5) ), luma = 0.2126R+0.7152G+0.0722B (линейный Rec.709);
#                   ПЕРЕНОРМИРОВАН по max-каналу (ревизия 05.09: без перенормировки яркость полусферы на красных картах
#                   падала на треть). Старое значение без перенормировки — `desat50_raw` (история).
#   ground_tinted — ground × vtx_tint_mean: цвет земли С вершинным тинтом террейна (мастер M_AsdaTerrainLit множит
#                   BaseColor на VertexColor.rgb/max — asda_fix_terrainlit_vertexcolor.py:112-113; оригинал fx:558
#                   `Temp2 * In.Diffuse`). Это и есть цвет, который видит камера; ревизия: тинт ≠ 1 на 7 картах.
#                   ground_tinted_norm / ground_tinted_desat50 — те же нормировки, что у ground.
#   sun           — ambient .SCE / max(ambient)  (ровно sun_col из asda_convert_map_lit.py).
#   fog_raw       — fog ARGB .SCE @0x08 → rgb 0..1 (asda_mapraw.argb_to_rgb; это sRGB-байты D3DCOLOR,
#                   в JSON лежат КАК ЕСТЬ, без перевода в линейное — так же их берёт купол asda_build_sky_dome).
#   fog           — max(fog_raw / max(fog_raw), FLOOR 0.15)  — нормировка по max-каналу + пол канала.
#   fog_floor_applied / fog_floor_channels — сработал ли пол и на каких каналах ("R","G","B") у `fog`;
#                   fog_floor_channels_linear — то же для fog_norm_linear.
#   fog_norm_linear — то же, но байты тумана переведены sRGB→linear ДО нормировки (справочно: вариант «туман как цвет
#                   поверхности»; формула Разработчика берёт fog_rgb как есть — это `fog`).
#   chroma_gap    — max_c |sun_c − fog_norm_c| (fog_norm БЕЗ пола) — мера Скептика (skeptic_round2 §1.1: ≥0.15 = пара расходится).
#   sky_tint      — {"0.5": …, "0.75": …}: norm(lerp(sun, fog, t)) — готовый LightColor Sky для A/B (туман = sRGB-байты).
#   sky_tint_linear — то же, но lerp с fog_norm_linear (туман переведён sRGB→linear ДО lerp): второй рычаг A/B —
#                   ревизия 05.09 §1.6: хрома alpen 0.16/0.28 против 0.11/0.12; ключ UE ASDA_SKYTINT_SPACE=srgb|linear.
#   Диагностика честности: colormap_alpha_mean, detail_hist, tiles_missing, vtx_tint_mean (средний тинт
#   вершинного цвета террейна col/max(col) — Разработчик считает его ≈1, проверяем), area_weights.
#
# Запуск: python Tools/asda_ground_color.py [--raw @@ASTORY_DEV_ROOT_FWD@@/Data/Export/maps] [--maps alpen,rein] [--table] [--equal]
#   Правило tools-no-import-side-effects: при импорте ничего не делает.
import os, sys, json, math, argparse, glob

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)
import asda_mapraw as _MR

RAW_DEFAULT = r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/maps"
RAWASSETS_MAP_TEX = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/RawAssets/Textures/map"
FLOOR = 0.15          # пол канала нормированного тумана (developer_round2 §1.1)
DESAT = 0.5           # −50 % насыщенности (ключ ASDA_LOWER_DESAT в UE-этапе)
SKYTINTS = (0.5, 0.75)
SCHEMA = 2            # версия схемы JSON: 2 = ground_tinted*, desat50 перенормирован, sky_tint_linear, fog_floor_* (05.09.2026)


def srgb_to_linear(a):
    """numpy-массив sRGB 0..1 → линейный (точная кусочная формула)."""
    import numpy as np
    a = np.asarray(a, dtype=np.float64)
    return np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(c):
    out = []
    for v in c:
        v = max(0.0, min(1.0, v))
        out.append(v * 12.92 if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055)
    return out


def hexcol(lin):
    if lin is None:
        return "—"
    return "#%02X%02X%02X" % tuple(int(round(v * 255)) for v in linear_to_srgb(lin))


def luma(c):
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]


def norm_max(c, eps=1e-3):
    m = max(max(c), eps)
    return [v / m for v in c]


def lerp3(a, b, t):
    return [a[i] + (b[i] - a[i]) * t for i in range(3)]


def color_words(lin):
    """Словесное описание цвета (по HSL от sRGB-кодированного значения) — для таблицы."""
    if lin is None:
        return "—"
    r, g, b = linear_to_srgb(lin)
    mx, mn = max(r, g, b), min(r, g, b)
    l = (mx + mn) / 2
    d = mx - mn
    s = 0.0 if d < 1e-6 else d / (1 - abs(2 * l - 1) + 1e-9)
    if d < 1e-6:
        h = 0.0
    elif mx == r:
        h = (60 * ((g - b) / d) + 360) % 360
    elif mx == g:
        h = 60 * ((b - r) / d) + 120
    else:
        h = 60 * ((r - g) / d) + 240
    # насыщенность словами — по хроме d = max−min (HSL-«s» у светлых оттенков врёт: бледный тон выходит «насыщенным»)
    if d < 0.08:
        hue = "серый"
    else:
        names = [(15, "красный"), (45, "оранжевый"), (70, "жёлтый"), (100, "жёлто-зелёный"),
                 (150, "зелёный"), (190, "бирюзовый"), (250, "голубой/синий"), (290, "фиолетовый"),
                 (345, "пурпурный"), (361, "красный")]
        hue = next(n for lim, n in names if h < lim)
    sat = "" if d < 0.08 else ("бледно-" if d < 0.25 else ("" if d < 0.55 else "насыщенный "))
    lt = "тёмный " if l < 0.3 else ("светлый " if l > 0.72 else "")
    return (lt + sat + hue).strip()


# ------------------------------------------------------------------ чтение террейна .mapraw
def read_terrain(path):
    """Сцена + патчи террейна (ровно та же последовательность, что asda_build_map_level.read_mapraw)."""
    r, ver, name = _MR.open_mapraw(path)
    sc = _MR.scene_fields(r, as_list=True, ver=ver)
    sc["ver"] = ver; sc["name"] = name; sc["fog_rgb"] = _MR.argb_to_rgb(sc["fog"])
    pc = r.u32(); ic = r.u32(); r.u16arr(ic)
    patches = []
    for _ in range(pc):
        vtx = r.u32()
        pos = col = ()
        if vtx > 0:
            pos = r.farr(vtx * 3); r.farr(vtx * 2); r.farr(vtx * 2); col = r.u8arr(vtx * 4)
        colorOff = r.u32(); alphaOff = r.u32(); r.f32(); r.f32()
        d0 = r.s(); s0 = r.f32(); d1 = r.s(); s1 = r.f32()
        patches.append({"vtx": vtx, "pos": pos, "col": col, "colorOff": colorOff, "alphaOff": alphaOff,
                        "detail0": d0, "scale0": s0, "detail1": d1, "scale1": s1})
    return sc, patches


def patch_area(p):
    """Площадь патча по XZ-габариту (игровые оси: Y вверх)."""
    if p["vtx"] <= 0:
        return 0.0
    xs = p["pos"][0::3]; zs = p["pos"][2::3]
    return (max(xs) - min(xs)) * (max(zs) - min(zs))


def vtx_tint(p):
    """Средний тинт вершинного цвета патча: mean(rgb/max(rgb)) (Д-4: в мастере тинт → BaseColor)."""
    import numpy as np
    if p["vtx"] <= 0:
        return None
    c = np.frombuffer(bytes(p["col"]), dtype=np.uint8).reshape(-1, 4)[:, :3].astype(np.float64) / 255.0
    m = np.maximum(c.max(axis=1, keepdims=True), 1e-6)
    return (c / m).mean(axis=0)


# ------------------------------------------------------------------ текстуры
class TexCache(object):
    def __init__(self, raw, map_name):
        self.dirs = [os.path.join(raw, map_name, "textures"), os.path.join(RAWASSETS_MAP_TEX, map_name)]
        self.terrain = os.path.join(raw, map_name, "terrain")
        self.cache = {}
        self.missing = set()

    def tile_mean_linear(self, tile):
        """Средний ЛИНЕЙНЫЙ RGB detail-тайла (PNG sRGB). None — файла нет."""
        if not tile:
            return None
        if tile in self.cache:
            return self.cache[tile]
        from PIL import Image
        import numpy as np
        res = None
        for d in self.dirs:
            p = os.path.join(d, tile + ".png")
            if os.path.exists(p):
                a = np.asarray(Image.open(p).convert("RGB"), dtype=np.float64) / 255.0
                res = srgb_to_linear(a).reshape(-1, 3).mean(axis=0).tolist()
                break
        if res is None:
            self.missing.add(tile)
        self.cache[tile] = res
        return res

    def png(self, prefix, off):
        from PIL import Image
        import numpy as np
        p = os.path.join(self.terrain, "%s_%d.png" % (prefix, off))
        if not os.path.exists(p):
            return None
        return np.asarray(Image.open(p).convert("RGBA"), dtype=np.float64) / 255.0


def ground_for_map(raw, map_name, ter_ref, equal_weights=False):
    import numpy as np
    sc, patches = read_terrain(os.path.join(raw, map_name + ".mapraw"))
    tex = TexCache(raw, map_name)
    ref = (ter_ref.get(map_name) or {}).get("patches") or []
    acc = np.zeros(3); acc_d0 = np.zeros(3); wsum = 0.0
    cm_alpha_sum = 0.0; mask_a_sum = 0.0; n_used = 0; n_d1 = 0
    hist0 = {}; hist1 = {}
    tints = []; areas = []
    skipped = 0
    for pi, p in enumerate(patches):
        if p["vtx"] <= 0:
            continue
        d0, d1 = p["detail0"], p["detail1"]
        if pi < len(ref):
            d0, d1 = ref[pi]["detail0"], ref[pi]["detail1"]
        hist0[d0] = hist0.get(d0, 0) + 1
        if d1:
            hist1[d1] = hist1.get(d1, 0) + 1
        m0 = tex.tile_mean_linear(d0)
        m1 = tex.tile_mean_linear(d1) if d1 else None
        if m0 is None and m1 is None:
            skipped += 1
            continue
        if m0 is None:
            m0 = m1
        mask = tex.png("alpha", p["alphaOff"])
        mask_a = float(mask[..., 3].mean()) if mask is not None else 0.0
        has_d1 = 1.0 if (d1 and m1 is not None) else 0.0
        w = mask_a * has_d1
        base = np.array(lerp3(m0, m1 if m1 is not None else m0, w))
        mask_a_sum += w; n_d1 += int(has_d1 > 0)
        cm = tex.png("color", p["colorOff"])
        if cm is not None:
            rgb = srgb_to_linear(cm[..., :3]); a = cm[..., 3:4]
            # попиксельно: lerp(base, cm.rgb, cm.a) → среднее
            col = (base[None, None, :] * (1 - a) + rgb * a).reshape(-1, 3).mean(axis=0)
            cm_alpha_sum += float(a.mean())
        else:
            col = base
        wp = 1.0 if equal_weights else patch_area(p)
        areas.append(wp)
        acc += col * wp; wsum += wp; n_used += 1
        acc_d0 += np.array(m0) * wp
        t = vtx_tint(p)
        if t is not None:
            tints.append(t)
    has_ground = wsum > 0
    ground = (acc / wsum).tolist() if has_ground else None
    ground_d0 = (acc_d0 / wsum).tolist() if has_ground else None       # только Detail0 (число Разработчика, но в линейном)
    ground_norm = norm_max(ground) if has_ground else None
    desat_raw = lerp3(ground_norm, [luma(ground_norm)] * 3, DESAT) if has_ground else None   # без перенормировки (история)
    desat = norm_max(desat_raw) if has_ground else None
    # вершинный тинт террейна: ground × mean(col/max(col)) — как мастер (BaseColor·тинт) и оригинал (Temp2·In.Diffuse)
    tint_mean = np.mean(tints, axis=0).tolist() if tints else None
    if has_ground and tint_mean is not None:
        ground_t = [ground[i] * tint_mean[i] for i in range(3)]
        ground_t_norm = norm_max(ground_t)
        desat_t = norm_max(lerp3(ground_t_norm, [luma(ground_t_norm)] * 3, DESAT))
    else:
        ground_t = ground_t_norm = desat_t = None
    amb = sc["ambient"]; mx = max(amb) or 1.0
    sun = [v / mx for v in amb]
    fog_raw = list(sc["fog_rgb"])
    fog_norm = norm_max(fog_raw)
    fog = [max(v, FLOOR) for v in fog_norm]
    floor_ch = [ch for ch, v in zip("RGB", fog_norm) if v < FLOOR]
    # вариант A/B: байты D3DCOLOR считаем sRGB-кодированными и переводим в линейное ДО нормировки/lerp
    fog_lin_norm = norm_max([float(x) for x in srgb_to_linear(fog_raw)])
    fog_lin = [max(v, FLOOR) for v in fog_lin_norm]
    floor_ch_lin = [ch for ch, v in zip("RGB", fog_lin_norm) if v < FLOOR]
    chroma_gap = max(abs(sun[i] - fog_norm[i]) for i in range(3))
    sky = {}; sky_lin = {}
    for t in SKYTINTS:
        sky["%g" % t] = norm_max(lerp3(sun, fog, t))
        sky_lin["%g" % t] = norm_max(lerp3(sun, fog_lin, t))
    return {
        "map": map_name, "mapraw_ver": sc["ver"],
        "ground": ground, "ground_norm": ground_norm, "desat50": desat, "desat50_raw": desat_raw,
        "ground_d0only": ground_d0,
        "ground_tinted": ground_t, "ground_tinted_norm": ground_t_norm, "ground_tinted_desat50": desat_t,
        "ground_hex": hexcol(ground), "desat50_hex": hexcol(desat),
        "ground_tinted_hex": hexcol(ground_t), "ground_tinted_desat50_hex": hexcol(desat_t),
        "ambient": list(amb), "mx": mx, "sun": sun, "sun_hex": hexcol(sun),
        "fog_raw": fog_raw, "fog_argb": "0x%08X" % sc["fog"], "fog_norm": fog_norm, "fog": fog,
        "fog_hex": hexcol(fog), "fog_norm_linear": fog_lin, "fogStart": sc["fogStart"], "fogEnd": sc["fogEnd"],
        "fog_floor_applied": bool(floor_ch), "fog_floor_channels": floor_ch, "fog_floor_channels_linear": floor_ch_lin,
        "chroma_gap": chroma_gap, "sky_tint": sky, "sky_tint_linear": sky_lin, "sunDir": sc["sunDir"],
        "diag": {
            "patches_total": len(patches), "patches_used": n_used, "patches_skipped_no_tiles": skipped,
            "ter_ref_patches": len(ref), "patches_with_detail1": n_d1,
            "mask_alpha_mean_hasD1": (mask_a_sum / n_d1) if n_d1 else 0.0,
            "colormap_alpha_mean": (cm_alpha_sum / n_used) if n_used else 0.0,
            "detail0_hist": hist0, "detail1_hist": hist1,
            "tiles_missing": sorted(tex.missing),
            "vtx_tint_mean": tint_mean,
            "area_weights": ("equal" if equal_weights else "bbox_xz"),
            "area_min_max": ([min(areas), max(areas)] if areas else None),
        },
    }


def fmt3(c):
    return "(%.2f, %.2f, %.2f)" % tuple(c) if c is not None else "—"


def write_table(res, path):
    lines = ["# Sky-тинт и цвет земли по картам (Сет A, офлайн-расчёт `Tools/asda_ground_color.py`)", "",
             "Все значения — линейный RGB 0..1, нормированы по max-каналу (яркость задают `SKY_K·mx` / `LOWER_K·mx`).",
             "`sun` = ambient/max; `fog_norm` = fog/max с полом 0.15; `lerp t` = norm(lerp(sun, fog_norm, t));",
             "`ground` = взвешенное среднее detail-тайлов с колормапом (норм.), `desat50` = −50 % насыщенности (перенормирован);",
             "`tinted_desat50` = то же от ground × вершинный тинт террейна (то, что видит камера).",
             "`lerp_lin 0.75` = norm(lerp(sun, fog_norm_linear, 0.75)) — туман переведён sRGB→linear ДО lerp (вариант A/B).",
             "`пол` = каналы `fog_norm`, поднятые полом 0.15 (— если не сработал).",
             "`gap` = max|sun − fog/max| без пола (мера Скептика; ≥0.15 — пара расходится).", "",
             "| карта | sun | fog_norm | lerp 0.5 | lerp 0.75 | lerp_lin 0.75 | ground | ground_desat50 | tinted_desat50 | пол | gap |",
             "|---|---|---|---|---|---|---|---|---|---|---|"]
    for m in sorted(res, key=lambda k: k.lower()):
        r = res[m]
        cell = lambda c: "%s `%s` %s" % (fmt3(c), hexcol(c), color_words(c))
        lines.append("| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %.2f |" % (
            m, cell(r["sun"]), cell(r["fog"]), cell(r["sky_tint"]["0.5"]), cell(r["sky_tint"]["0.75"]),
            cell(r["sky_tint_linear"]["0.75"]), cell(r["ground_norm"]), cell(r["desat50"]),
            cell(r["ground_tinted_desat50"]), ("".join(r["fog_floor_channels"]) or "—"), r["chroma_gap"]))
    lines += ["", "## Сырые данные сцены (.mapraw ver 3, `.SCE`)", "",
              "| карта | ambient (raw) | mx | fog ARGB | fog_raw | fogStart | fogEnd | α колормапа | Mask.A (патчи с D1) | vtx_tint | detail0 | detail1 |",
              "|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for m in sorted(res, key=lambda k: k.lower()):
        r = res[m]; d = r["diag"]
        h = lambda hh: ", ".join("%s×%d" % kv for kv in sorted(hh.items(), key=lambda kv: -kv[1]))
        lines.append("| %s | %s | %.2f | %s | %s | %g | %g | %.3f | %.3f | %s | %s | %s |" % (
            m, fmt3(r["ambient"]), r["mx"], r["fog_argb"], fmt3(r["fog_raw"]), r["fogStart"], r["fogEnd"],
            d["colormap_alpha_mean"], d["mask_alpha_mean_hasD1"], fmt3(d["vtx_tint_mean"]),
            h(d["detail0_hist"]), h(d["detail1_hist"]) or "—"))
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--raw", default=RAW_DEFAULT)
    ap.add_argument("--maps", default="")
    ap.add_argument("--out", default="")
    ap.add_argument("--table", action="store_true")
    ap.add_argument("--equal", action="store_true", help="веса патчей поровну вместо площади")
    a = ap.parse_args()
    raw = a.raw
    ter_ref = {}
    tr = os.path.join(raw, "ter_details.json")
    if os.path.exists(tr):
        ter_ref = json.load(open(tr, encoding="utf-8"))
    maps = [m for m in a.maps.split(",") if m] or \
           sorted(os.path.splitext(os.path.basename(p))[0] for p in glob.glob(os.path.join(raw, "*.mapraw")))
    res = {}
    for m in maps:
        try:
            r = ground_for_map(raw, m, ter_ref, a.equal)
        except Exception as e:
            print("ERR %s: %s" % (m, e)); continue
        res[m] = r
        d = r["diag"]
        print("%-18s ground=%s norm=%s desat=%s tinted_desat=%s | sun=%s fog=%s floor=%s gap=%.2f | acm=%.3f maskA=%.3f used=%d/%d miss=%s" % (
            m, fmt3(r["ground"]), fmt3(r["ground_norm"]), fmt3(r["desat50"]), fmt3(r["ground_tinted_desat50"]),
            fmt3(r["sun"]), fmt3(r["fog"]), "".join(r["fog_floor_channels"]) or "-",
            r["chroma_gap"], d["colormap_alpha_mean"], d["mask_alpha_mean_hasD1"], d["patches_used"],
            d["patches_total"], d["tiles_missing"] or ""))
    out = a.out or os.path.join(raw, "ground_colors.json")
    payload = {"_about": "Tools/asda_ground_color.py — линейный RGB; см. шапку скрипта. UE-этап: lower_hemisphere = "
                         "{desat50|ground_tinted_desat50}[ASDA_HEMI]·LOWER_K·mx; Sky LightColor = norm(lerp(sun, "
                         "{fog|fog_norm_linear}[ASDA_SKYTINT_SPACE], ASDA_SKYTINT)); ground==null → фолбэк 0.6·amb",
               "_schema": SCHEMA, "_floor": FLOOR, "_desat": DESAT, "maps": res}
    with open(out, "w", encoding="utf-8") as f:
        json.dump(payload, f, ensure_ascii=False, indent=1)
    print("-> %s (%d карт)" % (out, len(res)))
    if a.table:
        tp = os.path.join(raw, "sky_tint_table.md")
        write_table(res, tp)
        print("-> %s" % tp)


if __name__ == "__main__":
    main()
