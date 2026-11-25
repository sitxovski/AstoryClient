# Copyright (c) 2026 @sitxovski (Discord)
# _probe_vertex_colors.py — статистика ВЕРШИННЫХ ЦВЕТОВ оригинала AsdaStory (только чтение, без сайд-эффектов).
#
# Что читает (форматы по Tools/MapViewer/src/asda_formats.cpp, эталон model_anim.c meshLoadBuffersFromFile):
#   ТЕРРЕЙН  map/<map>/BUFFER.SEQ + BUFFER.TVB (FVF 0x242: pos@0 f32x3, diffuse ARGB@12, uv0@16, uv1@24; 32 Б)
#            + TerrainAlpha.SEQ/.TEX и TerrainColor.SEQ/.TEX (плитки .TEX = 36 Б шапка + DDS DXT3 256x256)
#   ОБЪЕКТЫ  map/<map>/Pos0.PBB / Pos1.PBB / Pos2.PBB (пулы буферов; FVF в файле; diffuse там, где бит 0x40)
#   ПЕРСОНАЖИ/ПРЕДМЕТЫ  Data/Item/Model/*.jbm и *.bnz (zip) — вершина 44 Б: pos@0, nrm@12, diffuse u32@24, uv0@28, uv1@36
#
# Считает: распределение яркости/насыщенности/альфы; долю белых/серых/цветных; корреляцию яркости с
# «нормаль вверх», с высотой, с запечённым AO плитки (TerrainAlpha.b) и с яркостью цветовой плитки (TerrainColor.rgb)
# в той же точке; однородность цвета внутри меша (объекты).
#
# Запуск:  python Tools/_probe_vertex_colors.py [--root @@ASTORY_DEV_ROOT_FWD@@/Donors/AsdaChroniclesInterlude] [--maps alpen,StagnantDesert,...]
#          [--md out.md]   — таблицы markdown в файл (по умолчанию печать в stdout)
# Без numpy работает, с numpy — быстрее.

import os, sys, struct, glob, math, zipfile, argparse
from collections import Counter, defaultdict

# ------------------------------------------------------------------ утилиты статистики
def pearson(xs, ys):
    n = len(xs)
    if n < 3:
        return float('nan')
    mx = sum(xs) / n; my = sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs); syy = sum((y - my) ** 2 for y in ys)
    if sxx <= 0 or syy <= 0:
        return float('nan')
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    return sxy / math.sqrt(sxx * syy)

def percentile(sorted_vals, p):
    if not sorted_vals:
        return float('nan')
    k = (len(sorted_vals) - 1) * p
    f = int(math.floor(k)); c = min(f + 1, len(sorted_vals) - 1)
    return sorted_vals[f] + (sorted_vals[c] - sorted_vals[f]) * (k - f)

def argb_split(u):
    return ((u >> 16) & 255, (u >> 8) & 255, u & 255, (u >> 24) & 255)

class ColorStats(object):
    """Накопитель по набору вершинных цветов (r,g,b,a в 0..255)."""
    def __init__(self):
        self.n = 0
        self.white = 0      # r=g=b=255
        self.grey = 0       # r=g=b (не белый)
        self.near_grey = 0  # (max-min) <= 8, не белый
        self.colored = 0    # (max-min) > 8
        self.lum = []       # яркость = mean(r,g,b)/255
        self.sat = []       # (max-min)/max
        self.alpha = Counter()
        self.hist_lum = Counter()   # корзины по 0.1
        self.chan_sum = [0, 0, 0]
        self.hue_bias = Counter()   # какой канал доминирует у цветных
        self.raw = Counter()        # самые частые u32 (первые 200000)

    def add(self, r, g, b, a):
        self.n += 1
        mx = max(r, g, b); mn = min(r, g, b)
        if r == 255 and g == 255 and b == 255:
            self.white += 1
        elif r == g == b:
            self.grey += 1
        elif mx - mn <= 8:
            self.near_grey += 1
        else:
            self.colored += 1
            self.hue_bias['R' if mx == r else ('G' if mx == g else 'B')] += 1
        L = (r + g + b) / (3.0 * 255)
        self.lum.append(L)
        self.sat.append((mx - mn) / float(mx) if mx else 0.0)
        self.alpha[a] += 1
        self.hist_lum[min(9, int(L * 10))] += 1
        self.chan_sum[0] += r; self.chan_sum[1] += g; self.chan_sum[2] += b
        if len(self.raw) < 4096:
            self.raw[(a << 24) | (r << 16) | (g << 8) | b] += 1

    def summary(self):
        if not self.n:
            return {}
        s = sorted(self.lum)
        ssat = sorted(self.sat)
        return {
            'n': self.n,
            'white%': 100.0 * self.white / self.n,
            'grey%': 100.0 * (self.grey + self.near_grey) / self.n,
            'colored%': 100.0 * self.colored / self.n,
            'lum_mean': sum(self.lum) / self.n,
            'lum_med': percentile(s, 0.5),
            'lum_p05': percentile(s, 0.05),
            'lum_p95': percentile(s, 0.95),
            'lum_min': s[0], 'lum_max': s[-1],
            'sat_mean': sum(self.sat) / self.n,
            'sat_p95': percentile(ssat, 0.95),
            'sat>0.2%': 100.0 * sum(1 for v in self.sat if v > 0.2) / self.n,
            'chan_mean': tuple(c / float(self.n) for c in self.chan_sum),
            'alpha': dict(self.alpha.most_common(6)),
            'hist': [self.hist_lum.get(i, 0) for i in range(10)],
            'hue_bias': dict(self.hue_bias),
            'top_raw': [('%08X' % k, v) for k, v in self.raw.most_common(6)],
        }

# ------------------------------------------------------------------ DXT3 (плитки террейна)
def read_u32_file(p):
    with open(p, 'rb') as f:
        b = f.read()
    return list(struct.unpack('<%dI' % (len(b) // 4), b[:len(b) // 4 * 4]))

class Dxt3Tile(object):
    """Одна плитка TerrainColor/TerrainAlpha: .TEX (36 Б) + DDS. Декодирует блоки по требованию."""
    def __init__(self, data, off):
        self.ok = False
        if off + 36 + 128 > len(data):
            return
        magic = struct.unpack_from('<I', data, off)[0]
        if magic != 0x20040603:
            return
        self.w = struct.unpack_from('<I', data, off + 8)[0]
        self.h = struct.unpack_from('<I', data, off + 12)[0]
        dds = off + 36
        if data[dds:dds + 4] != b'DDS ':
            return
        fourcc = data[dds + 84:dds + 88]
        self.fourcc = fourcc
        self.base = dds + 128
        self.data = data
        self.bw = max(1, self.w // 4)
        self.cache = {}
        self.ok = fourcc in (b'DXT3', b'DXT2', b'DXT1', b'DXT5')

    def texel(self, u, v):
        """(r,g,b,a) в 0..255 по uv 0..1 (clamp)."""
        x = min(self.w - 1, max(0, int(u * self.w)))
        y = min(self.h - 1, max(0, int(v * self.h)))
        bx, by = x >> 2, y >> 2
        key = (bx, by)
        blk = self.cache.get(key)
        if blk is None:
            blk = self._decode_block(bx, by)
            self.cache[key] = blk
        return blk[(y & 3) * 4 + (x & 3)]

    def _decode_block(self, bx, by):
        d = self.data
        if self.fourcc == b'DXT1':
            o = self.base + (by * self.bw + bx) * 8
            alpha = [255] * 16
            co = o
        else:
            o = self.base + (by * self.bw + bx) * 16
            if self.fourcc == b'DXT5':
                a0, a1 = d[o], d[o + 1]
                bits = int.from_bytes(d[o + 2:o + 8], 'little')
                pal = [a0, a1]
                if a0 > a1:
                    pal += [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
                else:
                    pal += [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
                alpha = [pal[(bits >> (3 * i)) & 7] for i in range(16)]
            else:  # DXT2/3: 4 бита на тексель
                alpha = []
                for row in range(4):
                    a16 = d[o + row * 2] | (d[o + row * 2 + 1] << 8)
                    for col in range(4):
                        alpha.append(((a16 >> (4 * col)) & 15) * 17)
            co = o + 8
        c0 = d[co] | (d[co + 1] << 8); c1 = d[co + 2] | (d[co + 3] << 8)
        idx = int.from_bytes(d[co + 4:co + 8], 'little')
        def rgb565(c):
            return (((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31)
        p0, p1 = rgb565(c0), rgb565(c1)
        if c0 > c1 or self.fourcc != b'DXT1':
            p2 = tuple((2 * a + b) // 3 for a, b in zip(p0, p1))
            p3 = tuple((a + 2 * b) // 3 for a, b in zip(p0, p1))
        else:
            p2 = tuple((a + b) // 2 for a, b in zip(p0, p1))
            p3 = (0, 0, 0)
        pal = (p0, p1, p2, p3)
        out = []
        for i in range(16):
            c = pal[(idx >> (2 * i)) & 3]
            out.append((c[0], c[1], c[2], alpha[i]))
        return out

# ------------------------------------------------------------------ ТЕРРЕЙН
def probe_terrain(map_dir, max_patches=None, light_dir=None):
    res = {}
    seq_p = os.path.join(map_dir, 'BUFFER.SEQ')
    if not os.path.exists(seq_p):
        return None
    bseq = read_u32_file(seq_p)
    with open(os.path.join(map_dir, 'BUFFER.TVB'), 'rb') as f:
        tvb = f.read()
    aseq = cseq = []
    adata = cdata = b''
    if os.path.exists(os.path.join(map_dir, 'TerrainAlpha.SEQ')):
        aseq = read_u32_file(os.path.join(map_dir, 'TerrainAlpha.SEQ'))
        with open(os.path.join(map_dir, 'TerrainAlpha.TEX'), 'rb') as f:
            adata = f.read()
    if os.path.exists(os.path.join(map_dir, 'TerrainColor.SEQ')):
        cseq = read_u32_file(os.path.join(map_dir, 'TerrainColor.SEQ'))
        with open(os.path.join(map_dir, 'TerrainColor.TEX'), 'rb') as f:
            cdata = f.read()

    cs = ColorStats()
    lum_all = []; up_all = []; y_all = []; ao_all = []; cmap_all = []; cw_all = []; ndl_all = []
    patch_uniform = 0; patch_cnt = 0; patch_nonwhite = 0
    grid_ok = 0
    per_patch_mean = []
    for p, o in enumerate(bseq):
        if max_patches and p >= max_patches:
            break
        if o + 8 > len(tvb):
            break
        mg, vc = struct.unpack_from('<II', tvb, o)
        if mg != 0x20060117:
            break
        base = o + 8
        if base + vc * 32 > len(tvb):
            break
        patch_cnt += 1
        pos = []; cols = []; uvs = []
        for v in range(vc):
            q = base + v * 32
            x, y, z, diff, u0, v0 = struct.unpack_from('<fffIff', tvb, q)
            pos.append((x, y, z)); cols.append(argb_split(diff)); uvs.append((u0, v0))
        distinct = set(cols)
        if len(distinct) == 1:
            patch_uniform += 1
        if any(c[:3] != (255, 255, 255) for c in cols):
            patch_nonwhite += 1
        lp = []
        for c in cols:
            cs.add(*c)
            lp.append((c[0] + c[1] + c[2]) / 765.0)
        per_patch_mean.append(sum(lp) / len(lp))
        # сетка 65x65 (row stride 65 по asda_formats.cpp) — нормаль «вверх» из соседних высот
        side = int(round(math.sqrt(vc)))
        is_grid = side * side == vc and side >= 3
        atile = Dxt3Tile(adata, aseq[p]) if (p < len(aseq) and adata) else None
        ctile = Dxt3Tile(cdata, cseq[p]) if (p < len(cseq) and cdata) else None
        if atile is not None and not atile.ok:
            atile = None
        if ctile is not None and not ctile.ok:
            ctile = None
        if is_grid:
            grid_ok += 1
        for v in range(vc):
            L = lp[v]
            lum_all.append(L)
            y_all.append(pos[v][1])
            if is_grid:
                r, c = divmod(v, side)
                r0, r1 = max(0, r - 1), min(side - 1, r + 1)
                c0, c1 = max(0, c - 1), min(side - 1, c + 1)
                pa, pb = pos[r * side + c0], pos[r * side + c1]
                pc, pd = pos[r0 * side + c], pos[r1 * side + c]
                dx = (pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2])
                dz = (pd[0] - pc[0], pd[1] - pc[1], pd[2] - pc[2])
                nx = dx[1] * dz[2] - dx[2] * dz[1]
                ny = dx[2] * dz[0] - dx[0] * dz[2]
                nz = dx[0] * dz[1] - dx[1] * dz[0]
                ln = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
                if ny < 0:
                    nx, ny, nz = -nx, -ny, -nz
                up_all.append(ny / ln)
                if light_dir:
                    ndl_all.append(max(0.0, (nx * light_dir[0] + ny * light_dir[1] + nz * light_dir[2]) / ln))
                else:
                    ndl_all.append(float('nan'))
            else:
                up_all.append(float('nan')); ndl_all.append(float('nan'))
            u, w = uvs[v]
            if atile is not None:
                t = atile.texel(u, w)
                ao_all.append(t[2] / 255.0)   # .b = AO (ps.1.1: t2.b; FX: Tex4.z)
            if ctile is not None:
                t = ctile.texel(u, w)
                cmap_all.append((t[0] + t[1] + t[2]) / 765.0)
                cw_all.append(t[3] / 255.0)
    res['stats'] = cs.summary()
    res['patches'] = patch_cnt
    res['patches_uniform'] = patch_uniform
    res['patches_nonwhite'] = patch_nonwhite
    res['grid_patches'] = grid_ok
    ups = [(l, u) for l, u in zip(lum_all, up_all) if u == u]
    res['corr_lum_up'] = pearson([a for a, b in ups], [b for a, b in ups]) if ups else float('nan')
    res['corr_lum_height'] = pearson(lum_all, y_all)
    # линейная регрессия яркость ~ n.y (насколько яркость — функция уклона)
    if ups:
        xs = [b for a, b in ups]; ys2 = [a for a, b in ups]
        n = len(xs); mx = sum(xs) / n; my = sum(ys2) / n
        sxx = sum((x - mx) ** 2 for x in xs) or 1e-9
        slope = sum((x - mx) * (y - my) for x, y in zip(xs, ys2)) / sxx
        icpt = my - slope * mx
        ss_res = sum((y - (icpt + slope * x)) ** 2 for x, y in zip(xs, ys2)); ss_tot = sum((y - my) ** 2 for y in ys2) or 1e-9
        res['fit_slope'] = slope; res['fit_icpt'] = icpt; res['fit_r2'] = 1 - ss_res / ss_tot
    nd = [(l, d) for l, d in zip(lum_all, ndl_all) if d == d]
    res['corr_lum_ndl'] = pearson([a for a, b in nd], [b for a, b in nd]) if nd else float('nan')
    res['corr_lum_ao'] = pearson(lum_all[:len(ao_all)], ao_all) if ao_all else float('nan')
    res['corr_lum_cmap'] = pearson(lum_all[:len(cmap_all)], cmap_all) if cmap_all else float('nan')
    res['ao_mean'] = (sum(ao_all) / len(ao_all)) if ao_all else float('nan')
    res['cmap_mean'] = (sum(cmap_all) / len(cmap_all)) if cmap_all else float('nan')
    res['cmap_w_mean'] = (sum(cw_all) / len(cw_all)) if cw_all else float('nan')
    # яркость по уклону: плоские (up>0.95) vs крутые (up<0.7)
    flat = [l for l, u in ups if u > 0.95]; steep = [l for l, u in ups if u < 0.7]
    res['lum_flat'] = sum(flat) / len(flat) if flat else float('nan')
    res['lum_steep'] = sum(steep) / len(steep) if steep else float('nan')
    # яркость по высоте: нижняя/верхняя квартиль
    if y_all:
        ys = sorted(y_all); q1 = percentile(ys, 0.25); q3 = percentile(ys, 0.75)
        lo = [l for l, y in zip(lum_all, y_all) if y <= q1]; hi = [l for l, y in zip(lum_all, y_all) if y >= q3]
        res['lum_lowQ'] = sum(lo) / len(lo) if lo else float('nan')
        res['lum_highQ'] = sum(hi) / len(hi) if hi else float('nan')
    pm = sorted(per_patch_mean)
    res['patch_mean_min'] = pm[0] if pm else float('nan')
    res['patch_mean_max'] = pm[-1] if pm else float('nan')
    return res

# ------------------------------------------------------------------ ОБЪЕКТЫ (PBB)
STRIDE = {0x002: 12, 0x042: 16, 0x044: 20, 0x102: 20, 0x104: 24, 0x142: 24, 0x144: 28, 0x152: 36,
          0x242: 32, 0x252: 44, 0x256: 48, 0x304: 40, 0x60252: 72}

def fvf_layout(fvf):
    """(diffuseOff|-1, normalOff|-1) по D3D FVF."""
    pos = fvf & 0x00E
    off = 16 if pos == 0x004 else (12 + (4 if pos == 0x006 else 0))  # XYZRHW / XYZB1 / XYZ
    nrm = -1
    if fvf & 0x010:
        nrm = off; off += 12
    dif = -1
    if fvf & 0x040:
        dif = off; off += 4
    return dif, nrm

def parse_pbb(path):
    """Пулы буферов: list of (typeTexList, [ (vtxOff, vtxCount, fvf, stride) ... ]) по патчам."""
    with open(path, 'rb') as f:
        b = f.read()
    n = len(b)
    def U(o):
        return struct.unpack_from('<I', b, o)[0] if 0 <= o and o + 4 <= n else 0
    patches = []
    c = 8
    while c + 0x20 <= n:
        count = U(c + 4); bs = U(c + 0x1c)
        if bs == 0:
            patches.append(([], [])); c += 0x20; continue
        data = c + 0x20
        mc0 = U(data + count * 0x20 + count * 0x100)
        if mc0 == 0 or mc0 > 4096:
            patches.append(([], [])); break
        tex = []
        for t in range(min(count, 4096)):
            so = data + count * 0x20 + t * 0x100
            e = b.find(b'\0', so, so + 0x100)
            full = b[so:e if e >= 0 else so + 0x100].decode('latin1', 'ignore')
            tex.append(full.replace('\\', '/').rsplit('/', 1)[-1])
        o = data + bs
        ok = True
        for i in range(mc0):
            ib = U(o)
            if o + 4 + ib > n:
                ok = False; break
            o += 4 + ib
        meshes = []
        for i in range(mc0):
            if not ok:
                break
            vb = U(o); fvf = U(o + 4)
            st = STRIDE.get(fvf, 0)
            if st == 0 or vb == 0 or vb % st or o + 8 + vb > n:
                ok = False; break
            meshes.append((o + 8, vb // st, fvf, st))
            o += 8 + vb
        patches.append((tex, meshes))
        if not ok:
            break
        c = o
    return b, patches

def probe_objects(map_dir):
    res = {'fvf': Counter(), 'meshes': 0, 'meshes_with_diffuse': 0, 'uniform_meshes': 0,
           'white_meshes': 0, 'buffers_bytes': 0}
    cs = ColorStats()
    per_fvf = defaultdict(ColorStats)
    lum_all = []; up_all = []; yrel_all = []
    per_mesh_mean = []
    for pool in ('Pos0.PBB', 'Pos1.PBB', 'Pos2.PBB'):
        p = os.path.join(map_dir, pool)
        if not os.path.exists(p):
            continue
        b, patches = parse_pbb(p)
        res['buffers_bytes'] += len(b)
        for tex, meshes in patches:
            for (vo, vc, fvf, st) in meshes:
                res['fvf'][fvf] += 1
                res['meshes'] += 1
                dif, nrm = fvf_layout(fvf)
                if dif < 0:
                    continue
                res['meshes_with_diffuse'] += 1
                cols = []; ups = []; ys = []
                for v in range(vc):
                    q = vo + v * st
                    diff = struct.unpack_from('<I', b, q + dif)[0]
                    c = argb_split(diff)
                    cols.append(c)
                    cs.add(*c)
                    per_fvf[fvf].add(*c)
                    L = (c[0] + c[1] + c[2]) / 765.0
                    lum_all.append(L)
                    if nrm >= 0:
                        nx, ny, nz = struct.unpack_from('<fff', b, q + nrm)
                        ln = math.sqrt(nx * nx + ny * ny + nz * nz)
                        ups.append(ny / ln if ln > 1e-6 else float('nan'))
                    else:
                        ups.append(float('nan'))
                    ys.append(struct.unpack_from('<f', b, q + 4)[0])
                if not cols:
                    continue
                up_all.extend(ups)
                ymin = min(ys); ymax = max(ys); rng = (ymax - ymin) or 1.0
                yrel_all.extend((y - ymin) / rng for y in ys)
                if len(set(cols)) == 1:
                    res['uniform_meshes'] += 1
                    if cols[0][:3] == (255, 255, 255):
                        res['white_meshes'] += 1
                per_mesh_mean.append(sum((c[0] + c[1] + c[2]) / 765.0 for c in cols) / len(cols))
    res['stats'] = cs.summary()
    res['per_fvf'] = {('0x%X' % k): v.summary() for k, v in per_fvf.items()}
    pairs = [(l, u) for l, u in zip(lum_all, up_all) if u == u]
    res['corr_lum_up'] = pearson([a for a, b in pairs], [b for a, b in pairs]) if pairs else float('nan')
    res['corr_lum_yrel'] = pearson(lum_all, yrel_all) if yrel_all else float('nan')
    upf = [l for l, u in pairs if u > 0.7]; dnf = [l for l, u in pairs if u < -0.3]; sidef = [l for l, u in pairs if -0.3 <= u <= 0.7]
    res['lum_facing_up'] = sum(upf) / len(upf) if upf else float('nan')
    res['lum_facing_down'] = sum(dnf) / len(dnf) if dnf else float('nan')
    res['lum_facing_side'] = sum(sidef) / len(sidef) if sidef else float('nan')
    lo = [l for l, y in zip(lum_all, yrel_all) if y < 0.25]; hi = [l for l, y in zip(lum_all, yrel_all) if y > 0.75]
    res['lum_bottom'] = sum(lo) / len(lo) if lo else float('nan')
    res['lum_top'] = sum(hi) / len(hi) if hi else float('nan')
    pm = sorted(per_mesh_mean)
    res['mesh_mean_p05'] = percentile(pm, 0.05); res['mesh_mean_p95'] = percentile(pm, 0.95)
    return res

# ------------------------------------------------------------------ JBM (предметы/персонажи)
def probe_jbm_bytes(d, cs, counter):
    if len(d) < 0x210 or struct.unpack_from('<I', d, 0)[0] != 0x20050706:
        return 0
    H = 4
    mc = struct.unpack_from('<I', d, H + 4)[0]; mat = struct.unpack_from('<I', d, H + 0x30)[0]
    if mc > 4096 or mat > 4096:
        return 0
    mr = H + 0x20C + mat * 0x20
    off = mr + mc * 0x29C
    nv = 0
    for m in range(mc):
        r = mr + m * 0x29C
        if r + 0x30 > len(d):
            break
        V = struct.unpack_from('<I', d, r + 0x28)[0]; T = struct.unpack_from('<I', d, r + 0x2C)[0]
        vo = off + T * 6
        if vo + V * 44 > len(d):
            break
        for v in range(V):
            u = struct.unpack_from('<I', d, vo + v * 44 + 24)[0]
            counter[u] += 1
            cs.add(*argb_split(u))
            nv += 1
        off = vo + V * 44 + V * 12
    return nv

def probe_jbm_family(paths, limit_files=None):
    cs = ColorStats(); counter = Counter(); files = 0; verts = 0
    for p in paths:
        if limit_files and files >= limit_files:
            break
        if p.lower().endswith('.bnz'):
            try:
                z = zipfile.ZipFile(p)
            except Exception:
                continue
            for nm in z.namelist():
                if not nm.lower().endswith('.jbm'):
                    continue
                if limit_files and files >= limit_files:
                    break
                try:
                    d = z.read(nm)
                except Exception:
                    continue
                n = probe_jbm_bytes(d, cs, counter)
                if n:
                    files += 1; verts += n
        else:
            with open(p, 'rb') as f:
                d = f.read()
            n = probe_jbm_bytes(d, cs, counter)
            if n:
                files += 1; verts += n
    return {'files': files, 'verts': verts, 'top': [('%08X' % k, v) for k, v in counter.most_common(8)],
            'stats': cs.summary()}

# ------------------------------------------------------------------ вывод
def fmt(v):
    if isinstance(v, float):
        return 'nan' if v != v else '%.3f' % v
    return str(v)

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default=r'@@ASTORY_DEV_ROOT_FWD@@/Donors/AsdaChroniclesInterlude')
    ap.add_argument('--maps', default='alpen,StagnantDesert,nightvalley,alpen_dungeon')
    ap.add_argument('--md', default='')
    ap.add_argument('--jbm-limit', type=int, default=0)
    ap.add_argument('--mapraw-dir', default=r'@@ASTORY_DEV_ROOT_FWD@@/Data/Export/maps', help='откуда брать lightDir карты (.mapraw, поле lightDir)')
    a = ap.parse_args(argv)
    out = []
    P = out.append
    maps = [m for m in a.maps.split(',') if m]
    P('## Террейн (BUFFER.TVB, FVF 0x242, diffuse@12)')
    P('| карта | патчей | верш. | белых% | серых% | цветных% | ярк.mean | ярк.med | p05..p95 | sat mean | sat>0.2% | альфа | corr(ярк,up) | flat/steep | corr(ярк,высота) | lowQ/highQ | corr(ярк,AO плитки) | corr(ярк,colormap) | патчей однотонных |')
    P('|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|')
    terr = {}
    light = {}
    for m in maps:
        mp = os.path.join(a.mapraw_dir, m + '.mapraw')
        try:
            with open(mp, 'rb') as f:
                d = f.read(4096)
            o = 8; ln = struct.unpack_from('<I', d, o)[0]; o += 4 + ln
            o += 4 + 4 + 4 + 12 + 12
            light[m] = struct.unpack_from('<fff', d, o)
        except Exception:
            light[m] = None
    for m in maps:
        md = os.path.join(a.root, 'map', m)
        r = probe_terrain(md, light_dir=light.get(m))
        terr[m] = r
        if not r:
            P('| %s | нет BUFFER.SEQ |' % m); continue
        s = r['stats']
        P('| %s | %d | %d | %.1f | %.1f | %.1f | %.3f | %.3f | %.3f..%.3f | %.3f | %.1f | %s | %s | %s/%s | %s | %s/%s | %s | %s | %d/%d |' % (
            m, r['patches'], s['n'], s['white%'], s['grey%'], s['colored%'], s['lum_mean'], s['lum_med'], s['lum_p05'], s['lum_p95'],
            s['sat_mean'], s['sat>0.2%'], s['alpha'], fmt(r['corr_lum_up']), fmt(r['lum_flat']), fmt(r['lum_steep']),
            fmt(r['corr_lum_height']), fmt(r.get('lum_lowQ', float('nan'))), fmt(r.get('lum_highQ', float('nan'))),
            fmt(r['corr_lum_ao']), fmt(r['corr_lum_cmap']), r['patches_uniform'], r['patches']))
    P('')
    P('Гистограмма яркости террейна (корзины 0.0-0.1 … 0.9-1.0), средние каналы RGB, самые частые ARGB:')
    for m in maps:
        r = terr.get(m)
        if r:
            s = r['stats']
            P('- %s: hist=%s  RGBmean=(%.1f,%.1f,%.1f)  top=%s  AOплитки mean=%s  colormap mean=%s (w=%s)  hue_bias=%s' % (
                m, s['hist'], s['chan_mean'][0], s['chan_mean'][1], s['chan_mean'][2], s['top_raw'], fmt(r['ao_mean']), fmt(r['cmap_mean']), fmt(r['cmap_w_mean']), s['hue_bias']))
            P('  регрессия ярк = %s + %s·n.y  R²=%s ; corr(ярк, max(0,N·L_mapraw))=%s  lightDir=%s' % (
                fmt(r.get('fit_icpt', float('nan'))), fmt(r.get('fit_slope', float('nan'))), fmt(r.get('fit_r2', float('nan'))), fmt(r['corr_lum_ndl']), light.get(m)))
    P('')
    P('## Объекты карт (Pos0/1/2.PBB, diffuse по FVF)')
    P('| карта | мешей | с diffuse | однотонных | из них белых | верш. | белых% | серых% | цветных% | ярк.mean | ярк.med | p05..p95 | sat mean | sat>0.2% | альфа | corr(ярк,n.y) | up/side/down | corr(ярк,отн.высота) | низ/верх | mesh mean p05..p95 |')
    P('|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|')
    objs = {}
    for m in maps:
        md = os.path.join(a.root, 'map', m)
        r = probe_objects(md)
        objs[m] = r
        s = r['stats']
        if not s:
            P('| %s | %d | 0 | | | | | | | | | | | | | | | | | |' % (m, r['meshes'])); continue
        P('| %s | %d | %d | %d | %d | %d | %.1f | %.1f | %.1f | %.3f | %.3f | %.3f..%.3f | %.3f | %.1f | %s | %s | %s/%s/%s | %s | %s/%s | %s..%s |' % (
            m, r['meshes'], r['meshes_with_diffuse'], r['uniform_meshes'], r['white_meshes'], s['n'], s['white%'], s['grey%'], s['colored%'],
            s['lum_mean'], s['lum_med'], s['lum_p05'], s['lum_p95'], s['sat_mean'], s['sat>0.2%'], s['alpha'],
            fmt(r['corr_lum_up']), fmt(r['lum_facing_up']), fmt(r['lum_facing_side']), fmt(r['lum_facing_down']),
            fmt(r['corr_lum_yrel']), fmt(r['lum_bottom']), fmt(r['lum_top']), fmt(r['mesh_mean_p05']), fmt(r['mesh_mean_p95'])))
    P('')
    for m in maps:
        r = objs[m]; s = r['stats']
        P('- %s: FVF=%s  hist=%s  RGBmean=%s  top=%s  hue_bias=%s' % (
            m, {('0x%X' % k): v for k, v in r['fvf'].items()}, s.get('hist'), tuple('%.1f' % c for c in s['chan_mean']) if s else '',
            s.get('top_raw'), s.get('hue_bias')))
        for k, v in sorted(r.get('per_fvf', {}).items()):
            P('    FVF %s: n=%d белых%%=%.1f серых%%=%.1f цветных%%=%.1f ярк.mean=%.3f альфа=%s top=%s' % (
                k, v['n'], v['white%'], v['grey%'], v['colored%'], v['lum_mean'], v['alpha'], v['top_raw'][:4]))
    P('')
    P('## JBM (предметы Data/Item/Model, персонажи/NPC/мобы из *.bnz): u32@24 файловой 44-Б вершины')
    fams = [('Item/Model *.jbm', glob.glob(os.path.join(a.root, 'Data/Item/Model/*.jbm')) + glob.glob(os.path.join(a.root, 'Data/Item/Model/*.JBM'))),
            ('Character/Model *.bnz', glob.glob(os.path.join(a.root, 'Data/Character/Model/*.bnz'))),
            ('NPC/Model *.bnz', glob.glob(os.path.join(a.root, 'Data/NPC/Model/*.bnz'))),
            ('Mob/Model *.bnz', glob.glob(os.path.join(a.root, 'Data/Mob/Model/*.bnz')))]
    P('| семейство | файлов | вершин | самые частые u32 | белых% | серых% | цветных% |')
    P('|---|---|---|---|---|---|---|')
    for name, paths in fams:
        r = probe_jbm_family(paths, a.jbm_limit or None)
        s = r['stats']
        P('| %s | %d | %d | %s | %s | %s | %s |' % (name, r['files'], r['verts'], r['top'],
              fmt(s.get('white%', float('nan'))), fmt(s.get('grey%', float('nan'))), fmt(s.get('colored%', float('nan')))))
    text = '\n'.join(out)
    if a.md:
        with open(a.md, 'w', encoding='utf-8') as f:
            f.write(text + '\n')
        print('written', a.md)
    else:
        print(text)

if __name__ == '__main__':
    main()
