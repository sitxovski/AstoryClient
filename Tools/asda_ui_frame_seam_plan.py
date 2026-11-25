# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_frame_seam_plan.py — ПЛАНИРОВЩИК шага `asda_ui_frame_seam_overlap.py` (24.10.2026, решение владельца: «проверь все
#   окна с такой рамкой и поправь щель тем же способом»). Обычный python (не unreal), без побочных эффектов при импорте.
#   ВХОД: слепок `asda_ui_dump_tree_order.py`. ВЫХОД: план `Tools/data/ui_frame_seam_overlap.json` и аудит всех составных рамок.
#   Рамка = 9 (3×3) или 3 (ряд/столбец) картинок `<контрол>_<обход>_<план>` одного родителя, встык, одна текстура/анкор.
#   Середина уходит под соседей на 1 (растяжение без смены UV — ДОПУСКАЕТСЯ только для однородного среза: все столбцы/строки
#   текселей равны) с ZOrder −1 (края середины) / −2 (тело). Каждая сторона проверяется ОФЛАЙН-СИМУЛЯЦИЕЙ кадра при масштабе
#   1.0 (ближайший тексель, альфа «over», исходники RawAssets/Textures/JUI/*.png): сторона берётся, только если пиксели не
#   меняются (полупрозрачный край соседа/угла — сторона пропускается). Рамка, перед которой в родителе нарисован чужой узел
#   в её области, пропускается (понижение z подняло бы его над рамкой).
#   ЗАПУСК: python -I Tools\asda_ui_frame_seam_plan.py <слепок> <план.json> <аудит.json>
import re, sys, json, collections, os
from PIL import Image

TEXDIR = r"@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Textures\JUI"
LINE = re.compile(r"^(\S+) :: (\d+) (\S+) (\S+) род=(\S+) вид=(\S+) (POINT|SPAN) анкор \[([-\d.]+),([-\d.]+)\]-\[([-\d.]+),([-\d.]+)\] "
                  r"off\(L([-\d.]+) T([-\d.]+) R([-\d.]+) B([-\d.]+)\) align\(([-\d.]+),([-\d.]+)\) auto=(\d) z=(-?\d+)"
                  r"(?: браш=(\S+) uv\[([-\d.]+) ([-\d.]+)\]-\[([-\d.]+) ([-\d.]+)\] тинт\(([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+)\))?")


def main(SRC, OUT_PLAN, OUT_AUDIT):
    """Построить план и аудит по слепку SRC."""
    WINP = {}
    nodes = collections.defaultdict(list)   # окно → [узлы по порядку]
    for ln in open(SRC, encoding="utf-8"):
        ln = ln.rstrip("\n")
        if ln.startswith("#WIN "):
            _, w, p = ln.split(" ", 2); WINP[w] = p; continue
        m = LINE.match(ln)
        if not m:
            continue
        g = m.groups()
        nodes[g[0]].append(dict(i=int(g[1]), name=g[2], cls=g[3], par=g[4], vis=g[5], sch=g[6], anch=tuple(map(float, g[7:11])),
                                off=tuple(map(float, g[11:15])), align=tuple(map(float, g[15:17])), auto=int(g[17]), z=int(g[18]),
                                tex=g[19], uv=tuple(map(float, g[20:24])) if g[20] else None,
                                tint=tuple(map(float, g[24:28])) if g[24] else None))

    _texcache = {}
    def tex(name):
        if name not in _texcache:
            p = os.path.join(TEXDIR, name + ".png")
            _texcache[name] = Image.open(p).convert("RGBA") if os.path.exists(p) else None
        return _texcache[name]

    def render(pieces, ox, oy, W, H):
        """pieces: [(x,y,w,h, uv, tint, texname)] в порядке отрисовки. Возврат — список RGBA-флотов."""
        buf = [[(0.0, 0.0, 0.0, 0.0)] * W for _ in range(H)]
        for (x, y, w, h, uv, tint, tn) in pieces:
            t = tex(tn); TW, TH = t.size; px_ = t.load()
            u0, v0, u1, v1 = uv
            for py in range(H):
                cy = py + 0.5 + oy
                if not (y <= cy < y + h):
                    continue
                v = v0 + (cy - y) / h * (v1 - v0)
                ty = int(v * TH) % TH
                for pxi in range(W):
                    cx = pxi + 0.5 + ox
                    if not (x <= cx < x + w):
                        continue
                    u = u0 + (cx - x) / w * (u1 - u0)
                    tx = int(u * TW) % TW
                    r, g, b, a = px_[tx, ty]
                    a = a / 255.0 * tint[3]
                    r, g, b = r / 255.0 * tint[0], g / 255.0 * tint[1], b / 255.0 * tint[2]
                    R, G, Bc, A = buf[py][pxi]
                    buf[py][pxi] = (r * a + R * (1 - a), g * a + G * (1 - a), b * a + Bc * (1 - a), a + A * (1 - a))
        return buf

    PL = re.compile(r"^(.*)_(\d+)_(\d)$")
    plan, audit = [], []
    for win, lst in sorted(nodes.items()):
        by_par = collections.defaultdict(list)
        for d in lst:
            by_par[d["par"]].append(d)
        groups = collections.defaultdict(dict)
        for d in lst:
            m = PL.match(d["name"])
            if m and d["cls"] == "Image":
                groups[(m.group(1), m.group(2), d["par"])][int(m.group(3))] = d
        for (base, walk, par), pl in sorted(groups.items()):
            ks = sorted(pl)
            kind = 9 if ks == list(range(9)) else (3 if ks == list(range(3)) else None)
            if not kind:
                continue
            ds = [pl[k] for k in ks]
            rec = dict(win=win, path=WINP.get(win), frame="%s_%s" % (base, walk), parent=par, kind=kind,
                       tex=ds[0]["tex"], pieces={d["name"]: list(d["off"]) for d in ds})
            def skip(why):
                rec["verdict"] = "пропуск"; rec["why"] = why; audit.append(rec)
            if any(d["sch"] != "POINT" or d["tex"] in (None, "None") or d["uv"] is None for d in ds):
                skip("не точечный слот или без текстуры"); continue
            if len({d["tex"] for d in ds}) != 1 or len({d["anch"] for d in ds}) != 1:
                skip("разные текстуры/анкоры у кусков"); continue
            if any(d["align"] != (0.0, 0.0) or d["auto"] for d in ds):
                skip("alignment/авторазмер не вида импортёра"); continue
            zs = {d["z"] for d in ds}
            R = {k: pl[k]["off"] for k in ks}
            # Уже исправлено (вид шага): середины ушли под соседей, z −1/−2.
            if zs != {0}:
                rec["verdict"] = "z≠0 (уже исправлено или чужая правка)"; rec["why"] = "z=%s" % sorted(zs); audit.append(rec); continue
            if kind == 9:
                okm = all(abs(R[r*3+c][0] + R[r*3+c][2] - R[r*3+c+1][0]) < 0.01 and abs(R[r*3+c][1] - R[r*3+c+1][1]) < 0.01
                          and abs(R[r*3+c][3] - R[r*3+c+1][3]) < 0.01 for r in range(3) for c in range(2)) and \
                      all(abs(R[r*3+c][1] + R[r*3+c][3] - R[(r+1)*3+c][1]) < 0.01 and abs(R[r*3+c][0] - R[(r+1)*3+c][0]) < 0.01
                          and abs(R[r*3+c][2] - R[(r+1)*3+c][2]) < 0.01 for c in range(3) for r in range(2))
                orient = "9"
            else:
                a, b, c3 = R[0], R[1], R[2]
                if abs(a[0]+a[2]-b[0]) < .01 and abs(b[0]+b[2]-c3[0]) < .01 and a[1] == b[1] == c3[1] and a[3] == b[3] == c3[3]:
                    orient, okm = "H", True
                elif abs(a[1]+a[3]-b[1]) < .01 and abs(b[1]+b[3]-c3[1]) < .01 and a[0] == b[0] == c3[0] and a[2] == b[2] == c3[2]:
                    orient, okm = "V", True
                else:
                    orient, okm = "?", False
            if not okm:
                skip("куски не встык (не мозаика)"); continue
            if tex(ds[0]["tex"]) is None:
                skip("нет исходника текстуры %s.png" % ds[0]["tex"]); continue
            # Кандидаты: стороны средних кусков (середина уходит под соседа на 1, UV продлевается — старые пиксели те же).
            def is_mid(k):
                if kind == 9:
                    return (k % 3 == 1), (k // 3 == 1)
                return (orient == "H" and k == 1), (orient == "V" and k == 1)
            def uniform(k, axis):
                """Срез куска одинаков вдоль оси растяжения (все столбцы/строки текселей равны) — растяжение без потерь."""
                t = tex(pl[k]["tex"]); TW, TH = t.size
                u0, v0, u1, v1 = pl[k]["uv"]
                xs = range(int(u0 * TW) % TW, (int(u1 * TW - 1e-6) % TW) + 1)
                ys = range(int(v0 * TH) % TH, (int(v1 * TH - 1e-6) % TH) + 1)
                if axis == "x":
                    cols = [tuple(t.getpixel((x_, y_)) for y_ in ys) for x_ in xs]
                    return len(set(cols)) <= 1
                rows = [tuple(t.getpixel((x_, y_)) for x_ in xs) for y_ in ys]
                return len(set(rows)) <= 1
            cands = []
            nonuni = []
            for k in ks:
                mx, my = is_mid(k)
                if mx:
                    if uniform(k, "x"): cands += [(k, "L"), (k, "R")]
                    else: nonuni.append("%s:x" % pl[k]["name"])
                if my:
                    if uniform(k, "y"): cands += [(k, "T"), (k, "B")]
                    else: nonuni.append("%s:y" % pl[k]["name"])
            def build(sel):
                out = {}
                anysel = bool(sel)
                for k in ks:
                    x, y, w, h = R[k]
                    u0, v0, u1, v1 = pl[k]["uv"]
                    du = (u1 - u0) / w; dv = (v1 - v0) / h
                    mx, my = is_mid(k)
                    if (k, "L") in sel: x -= 1; w += 1
                    if (k, "R") in sel: w += 1
                    if (k, "T") in sel: y -= 1; h += 1
                    if (k, "B") in sel: h += 1
                    z = -(int(mx) + int(my)) if anysel else 0
                    out[k] = ((x, y, w, h), z, (u0, v0, u1, v1))
                return out
            def strips(sel):
                pix = set()
                for (k, sd) in sel:
                    x, y, w, h = R[k]
                    if sd == "L": xs, ys = [x - 1], [y - 1 + i for i in range(int(h) + 2)]
                    elif sd == "R": xs, ys = [x + w], [y - 1 + i for i in range(int(h) + 2)]
                    elif sd == "T": xs, ys = [x - 1 + i for i in range(int(w) + 2)], [y - 1]
                    else: xs, ys = [x - 1 + i for i in range(int(w) + 2)], [y + h]
                    for xx in xs:
                        for yy in ys:
                            pix.add((xx, yy))
                return pix
            def render_pts(pieces, pts):
                res = {}
                for (cx0, cy0) in pts:
                    cx, cy = cx0 + 0.5, cy0 + 0.5
                    R_, G_, B_, A_ = 0.0, 0.0, 0.0, 0.0
                    for (x, y, w, h, uv, tint, tn) in pieces:
                        if not (x <= cx < x + w and y <= cy < y + h):
                            continue
                        t = tex(tn); TW, TH = t.size
                        u = uv[0] + (cx - x) / w * (uv[2] - uv[0]); v = uv[1] + (cy - y) / h * (uv[3] - uv[1])
                        r, g, bb, al = t.getpixel((int(u * TW) % TW, int(v * TH) % TH))
                        al = al / 255.0 * tint[3]
                        R_, G_, B_, A_ = (r / 255.0 * tint[0] * al + R_ * (1 - al), g / 255.0 * tint[1] * al + G_ * (1 - al),
                                          bb / 255.0 * tint[2] * al + B_ * (1 - al), al + A_ * (1 - al))
                    res[(cx0, cy0)] = (R_, G_, B_, A_)
                return res
            before = [(R[k][0], R[k][1], R[k][2], R[k][3], pl[k]["uv"], pl[k]["tint"], pl[k]["tex"]) for k in ks]
            def diff_of(sel):
                nw = build(sel)
                order = sorted(ks, key=lambda k: (nw[k][1], pl[k]["i"]))
                after = [(nw[k][0][0], nw[k][0][1], nw[k][0][2], nw[k][0][3], nw[k][2], pl[k]["tint"], pl[k]["tex"]) for k in order]
                pts = strips(sel)
                A = render_pts(before, pts); Bm = render_pts(after, pts)
                return sum(1 for p_ in pts if any(abs(a_ - b_) > 0.5 / 255 for a_, b_ in zip(A[p_], Bm[p_])))
            # Сосед в родителе, нарисованный РАНЬШЕ рамки и задевающий её область (после понижения z оказался бы сверху).
            sibs = by_par[par]
            first_i = min(d["i"] for d in ds)
            x0 = min(R[k][0] for k in ks) - 1; y0 = min(R[k][1] for k in ks) - 1
            x1 = max(R[k][0] + R[k][2] for k in ks) + 1; y1 = max(R[k][1] + R[k][3] for k in ks) + 1
            early = [s["name"] for s in sibs if s["i"] < first_i and s["name"] not in rec["pieces"] and s["sch"] == "POINT"
                     and s["anch"] == ds[0]["anch"] and not (s["off"][0] >= x1 or s["off"][0] + s["off"][2] <= x0
                     or s["off"][1] >= y1 or s["off"][1] + s["off"][3] <= y0)]
            early_span = [s["name"] for s in sibs if s["i"] < first_i and s["name"] not in rec["pieces"] and (s["sch"] != "POINT" or s["anch"] != ds[0]["anch"])]
            neg = [s["name"] for s in sibs if s["z"] < 0 and s["name"] not in rec["pieces"]]
            if early or early_span or neg:
                skip("в родителе раньше рамки нарисованы %s; z<0 у %s — порядок поменялся бы" % ((early + early_span)[:4], neg[:4])); continue
            good = [c_ for c_ in cands if diff_of({c_}) == 0]
            bad_sides = [c_ for c_ in cands if c_ not in good]
            sel = set(good)
            while sel and diff_of(sel):
                sel.discard(sorted(sel)[-1])
            if not sel:
                skip("кадр при 1.0 изменился бы на любой стороне" if cands else "срез середины неоднороден — растяжение исказило бы рисунок (%s)" % nonuni[:3]); continue
            new = build(sel)
            rec["sides_bad"] = ["%s:%s" % (pl[k]["name"], sd) for (k, sd) in bad_sides]
            rec["nonuniform"] = nonuni
            rec["sides_ok"] = ["%s:%s" % (pl[k]["name"], sd) for (k, sd) in sorted(sel)]
            rec["verdict"] = "в план"; rec["orient"] = orient
            rec["changes"] = {pl[k]["name"]: dict(old=list(R[k]), z_old=0, uv_old=list(pl[k]["uv"]), new=list(new[k][0]), z_new=new[k][1], uv_new=list(new[k][2])) for k in ks}
            rec["full"] = not bad_sides and not nonuni and len(sel) == len(cands)
            audit.append(rec); plan.append(rec)

    json.dump(dict(frames=[dict(path=r["path"], win=r["win"], frame=r["frame"], kind=r["kind"], orient=r["orient"], tex=r["tex"],
                                pieces=r["changes"]) for r in plan]), open(OUT_PLAN, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    json.dump(audit, open(OUT_AUDIT, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    c = collections.Counter((r["verdict"], r.get("why", "").split(":")[0][:60]) for r in audit)
    with open(OUT_AUDIT + ".txt", "w", encoding="utf-8") as f:
        for k, v in c.most_common():
            f.write("%d %s" % (v, k) + chr(10))
        f.write("в план рамок %d окон %d" % (len(plan), len({r["win"] for r in plan})) + chr(10))



# ★★★ ЗАКОН tools-no-import-side-effects.
if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3])
