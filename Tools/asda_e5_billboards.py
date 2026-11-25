# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_e5_billboards.py — Э5(б) круглого стола (FINAL_synthesis.md §5): по .mapraw найти модели карты
#   (1) с НУЛЕВЫМИ нормалями (FVF без D3DFVF_NORMAL, экспортёр пишет нули — fix_mesh_normals_and_builder.md)
#       И >1 НЕСВЯЗНОЙ «карточки» (компоненты связности по индексам треугольников) — кандидаты в билборды/
#       крестовые карточки листвы, которым нужен TwoSidedFoliage, а не одна нормаль;
#   (2) «инвертированные» модели с нормалями: средний угол между авторской нормалью и площадно-взвешенной
#       геометрической нормалью смежных треугольников > 90° (метод (2) asda_verify_mesh_normals.py, но БЕЗ UE);
#       знак геометрической нормали калибруется по большинству моделей карты (правило винда источника не
#       угадываем — берём то, при котором большинство моделей < 90°); отдельно список `tree_gru_*`.
#   СИСТЕМНЫЙ Python, только чтение .mapraw (asda_mapraw), ничего не пишет кроме отчёта.
# Запуск: python Tools/asda_e5_billboards.py [--maps alpen,rein,flammio] [--out <md>]
import os, sys, math, argparse, collections

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)
import asda_mapraw as _MR

RAW_DEFAULT = r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/maps"
OUT_DEFAULT = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/render_sets_2026-09-05/e5b_billboards.md"


def read_models(path):
    """Модели + число инстансов на модель (та же последовательность, что asda_build_map_level.read_mapraw)."""
    r, ver, name = _MR.open_mapraw(path)
    _MR.scene_fields(r, ver=ver)
    pc = r.u32(); ic = r.u32(); r.u16arr(ic)
    for _ in range(pc):
        vtx = r.u32()
        if vtx > 0:
            r.farr(vtx * 3); r.farr(vtx * 2); r.farr(vtx * 2); r.u8arr(vtx * 4)
        r.u32(); r.u32(); r.f32(); r.f32(); r.s(); r.f32(); r.s(); r.f32()
    mc = r.u32(); models = []
    for _ in range(mc):
        nm = r.s(); tex = r.s(); vtx = r.u32()
        pos = r.farr(vtx * 3); nrm = r.farr(vtx * 3); r.farr(vtx * 2)
        n = r.u32(); idx = r.u32arr(n)
        models.append({"name": nm, "tex": tex, "vtx": vtx, "pos": pos, "nrm": nrm, "idx": idx})
    inst_count = collections.Counter()
    try:
        ic = r.u32()
        for _ in range(ic):
            mi = r.u32(); r.farr(16); r.i32(); r.i32(); r.farr(4)
            inst_count[mi] += 1
    except Exception:
        pass
    return ver, models, inst_count


def components(vtx, idx):
    """Число компонент связности по треугольникам (union-find по индексам вершин); учитываются только
    вершины, входящие в треугольники. Также: сколько компонент по ПОЗИЦИИ (сшитые дубли вершин)."""
    parent = list(range(vtx))

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]; x = parent[x]
        return x

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb
    used = set()
    for t in range(0, len(idx) - 2, 3):
        a, b, c = idx[t], idx[t + 1], idx[t + 2]
        if max(a, b, c) >= vtx:
            continue
        union(a, b); union(b, c); used.update((a, b, c))
    return len(set(find(v) for v in used))


def components_by_position(vtx, pos, idx, q=1e-3):
    """То же, но вершины с одинаковой позицией (округление q) считаются одной — снимает «разрыв» по UV-швам."""
    key = {}
    remap = [0] * vtx
    for v in range(vtx):
        k = (round(pos[3 * v] / q), round(pos[3 * v + 1] / q), round(pos[3 * v + 2] / q))
        remap[v] = key.setdefault(k, len(key))
    n = len(key)
    parent = list(range(n))

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]; x = parent[x]
        return x
    used = set()
    for t in range(0, len(idx) - 2, 3):
        a, b, c = idx[t], idx[t + 1], idx[t + 2]
        if max(a, b, c) >= vtx:
            continue
        a, b, c = remap[a], remap[b], remap[c]
        for x, y in ((a, b), (b, c)):
            rx, ry = find(x), find(y)
            if rx != ry:
                parent[rx] = ry
        used.update((a, b, c))
    return len(set(find(v) for v in used))


def has_normals(nrm):
    return any(abs(v) > 1e-6 for v in nrm)


def mean_geo_angle(vtx, pos, nrm, idx):
    """Средний угол (°) между авторской нормалью и геометрической ((b−a)×(c−a), площадно-взвешенной)
    по вершинам с ненулевой нормалью; None — нет треугольников."""
    acc = [[0.0, 0.0, 0.0] for _ in range(vtx)]
    for t in range(0, len(idx) - 2, 3):
        a, b, c = idx[t], idx[t + 1], idx[t + 2]
        if max(a, b, c) >= vtx:
            continue
        pa = pos[3 * a:3 * a + 3]; pb = pos[3 * b:3 * b + 3]; pc = pos[3 * c:3 * c + 3]
        u = (pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]); v = (pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2])
        n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        for k in (a, b, c):
            acc[k][0] += n[0]; acc[k][1] += n[1]; acc[k][2] += n[2]
    s = 0.0; cnt = 0
    for k in range(vtx):
        g = acc[k]; lg = math.sqrt(g[0] ** 2 + g[1] ** 2 + g[2] ** 2)
        a = nrm[3 * k:3 * k + 3]; la = math.sqrt(a[0] ** 2 + a[1] ** 2 + a[2] ** 2)
        if lg < 1e-9 or la < 1e-6:
            continue
        d = max(-1.0, min(1.0, (g[0] * a[0] + g[1] * a[1] + g[2] * a[2]) / (lg * la)))
        s += math.degrees(math.acos(d)); cnt += 1
    return (s / cnt) if cnt else None


def bbox(pos):
    xs = pos[0::3]; ys = pos[1::3]; zs = pos[2::3]
    return (max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs)) if xs else (0, 0, 0)


def analyze_map(raw, m):
    ver, models, inst = read_models(os.path.join(raw, m + ".mapraw"))
    rows = []
    for i, md in enumerate(models):
        hn = has_normals(md["nrm"])
        tri = len(md["idx"]) // 3
        comp = components(md["vtx"], md["idx"])
        comp_pos = components_by_position(md["vtx"], md["pos"], md["idx"])
        ang = mean_geo_angle(md["vtx"], md["pos"], md["nrm"], md["idx"]) if hn else None
        rows.append({"i": i, "name": md["name"], "tex": md["tex"], "vtx": md["vtx"], "tri": tri, "has_nrm": hn,
                     "comp": comp, "comp_pos": comp_pos, "angle": ang, "inst": inst.get(i, 0), "bbox": bbox(md["pos"])})
    return ver, rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--raw", default=RAW_DEFAULT)
    ap.add_argument("--maps", default="alpen,rein,flammio")
    ap.add_argument("--out", default=OUT_DEFAULT)
    a = ap.parse_args()
    maps = [x for x in a.maps.split(",") if x]
    L = ["# Э5(б) — модели с нулевыми нормалями и >1 карточки; инвертированные `tree_gru_*` (офлайн, `Tools/asda_e5_billboards.py`)\n",
         "Источник: `export/maps/<map>.mapraw` (модели: pos/nrm/uv/idx; нулевая нормаль = FVF без D3DFVF_NORMAL, "
         "`asda_formats.cpp:713-716`). Компоненты связности — по индексам треугольников (`comp`) и по совпадающим позициям "
         "вершин (`comp_pos`, снимает разрывы по UV-швам). Инверсия — средний угол авторской нормали к геометрической "
         "((b−a)×(c−a), площадно-взвешенной) > 90° при знаке, выбранном по большинству моделей карты.\n"]
    summary = []
    for m in maps:
        p = os.path.join(a.raw, m + ".mapraw")
        if not os.path.exists(p):
            L.append("## %s — нет .mapraw\n" % m); continue
        ver, rows = analyze_map(a.raw, m)
        n = len(rows)
        zero = [r for r in rows if not r["has_nrm"]]
        zero_multi = [r for r in zero if r["comp_pos"] > 1]
        zero_single = [r for r in zero if r["comp_pos"] <= 1]
        with_n = [r for r in rows if r["angle"] is not None]
        # калибровка знака: если большинство > 90°, значит винд источника обратный формуле — переворачиваем
        over = sum(1 for r in with_n if r["angle"] > 90)
        flip = over > len(with_n) / 2
        for r in with_n:
            r["angle_cal"] = (180 - r["angle"]) if flip else r["angle"]
        inv = [r for r in with_n if r["angle_cal"] > 90]
        tree_inv = [r for r in inv if r["name"].lower().startswith("tree_gru")]
        tree_all = [r for r in rows if r["name"].lower().startswith("tree_gru")]
        L.append("## %s (.mapraw ver %d) — моделей %d, без нормалей %d, с нормалями %d\n" % (m, ver, n, len(zero), len(with_n)))
        L.append("Знак геометрической нормали: %s (моделей с углом >90° при формуле (b−a)×(c−a): %d из %d).\n" % (
            "ПЕРЕВЁРНУТ (винд источника обратный)" if flip else "прямой", over, len(with_n)))
        L.append("### (б) Нулевые нормали И >1 карточки (`comp_pos` > 1) — %d моделей (ещё %d без нормалей — одна связная деталь)\n" % (len(zero_multi), len(zero_single)))
        L.append("| # | модель | текстура | vtx | tri | comp (idx) | comp (pos) | инстансов | габарит (x,y,z) |\n|---|---|---|---|---|---|---|---|---|")
        for r in sorted(zero_multi, key=lambda r: (-r["comp_pos"], r["name"])):
            L.append("| %d | %s | %s | %d | %d | %d | %d | %d | %.0f×%.0f×%.0f |" % (
                r["i"], r["name"], r["tex"], r["vtx"], r["tri"], r["comp"], r["comp_pos"], r["inst"], *r["bbox"]))
        L.append("")
        # распределение числа карточек
        dist = collections.Counter(min(r["comp_pos"], 9) for r in zero)
        L.append("Распределение карточек у моделей без нормалей (comp_pos, 9 = ≥9): " + ", ".join("%d→%d" % kv for kv in sorted(dist.items())) + ".\n")
        if zero_single:
            L.append("Без нормалей, ОДНА связная деталь (не билборд-кросс, но тоже без авторской нормали): " +
                     ", ".join(sorted(set(r["name"] for r in zero_single))[:60]) + ("…" if len(set(r["name"] for r in zero_single)) > 60 else "") + "\n")
        L.append("### (список) Инвертированные модели с нормалями (средний угол > 90°) — %d; из них `tree_gru_*` — %d из %d tree_gru на карте\n" % (len(inv), len(tree_inv), len(tree_all)))
        L.append("| # | модель | текстура | vtx | tri | средний угол° | comp_pos | инстансов |\n|---|---|---|---|---|---|---|---|")
        for r in sorted(inv, key=lambda r: -r["angle_cal"]):
            L.append("| %d | %s | %s | %d | %d | %.0f | %d | %d |" % (r["i"], r["name"], r["tex"], r["vtx"], r["tri"], r["angle_cal"], r["comp_pos"], r["inst"]))
        L.append("")
        if tree_all:
            L.append("Все `tree_gru_*` карты (угол°/comp_pos/инстансов): " + ", ".join(
                "%s %s/%d/%d" % (r["name"], ("%.0f" % r["angle_cal"]) if r.get("angle_cal") is not None else "нет нормалей", r["comp_pos"], r["inst"])
                for r in sorted(tree_all, key=lambda r: r["name"])) + "\n")
        summary.append((m, n, len(zero), len(zero_multi), len(with_n), len(inv), len(tree_inv), len(tree_all)))
    L.insert(2, "| карта | моделей | без нормалей | без нормалей и >1 карточки | с нормалями | инвертированных | tree_gru инверт. / всего |\n|---|---|---|---|---|---|---|\n" +
             "\n".join("| %s | %d | %d | **%d** | %d | %d | %d / %d |" % s for s in summary) + "\n")
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, "w", encoding="utf-8") as f:
        f.write("\n".join(L) + "\n")
    print("-> %s" % a.out)
    for s in summary:
        print("%-14s models=%d zero=%d zero_multi=%d withN=%d inverted=%d tree_gru_inv=%d/%d" % s)


if __name__ == "__main__":
    main()
