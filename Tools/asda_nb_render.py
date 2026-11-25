# Copyright (c) 2026 @sitxovski (Discord)
# asda_nb_render.py — превью «нормаль из альбедо» БЕЗ ДВИЖКА: свой растеризатор моделей .glb.
#
# ★ ПОЧЕМУ НЕ UE: headless-редактор не тикает во время python-скрипта, из-за чего кадры выходят
#   чёрными и одинаковыми (проверено: 10 файлов, разница средней яркости before/after = 0.1).
#   Здесь весь рендер под контролем: одна и та же геометрия, свет и камера, отличается ТОЛЬКО
#   источник нормали и шероховатости.
#
# Слева (ДО)  — как сейчас в проекте: нормаль берётся из геометрии, шероховатость = константа 0.9.
# Справа (ПОСЛЕ) — нормаль из карты (asda_nb_gen_maps.py) + карта шероховатости.
#
# Зависимости: numpy + Pillow. Запуск: python asda_nb_render.py
import json
import os
import struct

import numpy as np
from PIL import Image, ImageDraw, ImageFont

MODELS = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/RawAssets/Models"
MAPS = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Tools/_out/nb_preview"
OUT = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/nb_preview_compare.png"

CELL = 620          # сторона одной ячейки листа
ROUGH_CONST = 0.9   # шероховатость мастеров проекта (asda_build_materials_lit.py, ROUGH)
SUN = np.array([-0.42, 0.52, 0.74], np.float32)   # направление на свет В ПРОСТРАНСТВЕ КАМЕРЫ
SUN_COLOR = np.array([1.00, 0.97, 0.92], np.float32)
SKY_COLOR = np.array([0.42, 0.47, 0.58], np.float32)
AMBIENT = 0.24      # ★ «тень никогда не чёрная» — тот же приём, что в мастерах проекта

SAMPLES = [
    ("stone", "Камень — статуя NPC",      "Skeletal/NPC/mdel_stoneman-a_00.glb"),
    ("build", "Кладка и дерево — дом",    "Skeletal/NPC/mdel_spabuild-a_00.glb"),
    ("tree",  "Кора и листва — дерево",   "Skeletal/NPC/mdel_oldtree-a_00.glb"),
    ("armor", "Ткань и броня — нагрудник", "Char/ch1_f_00_chst103_00_00_0.glb"),
    ("mob",   "Шкура моба — голем",       "Mob/mon_b_3_firegolem-a_00.glb"),
]

_CT = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}
_NC = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


# ───────────────────────────── чтение .glb ─────────────────────────────
def read_glb(path):
    """Разбор бинарного glTF: возвращает (json-описание, буфер BIN)."""
    with open(path, "rb") as f:
        data = f.read()
    magic, _ver, _length = struct.unpack_from("<III", data, 0)
    if magic != 0x46546C67:
        raise ValueError("не glb: %s" % path)
    off, js, bin_ = 12, None, b""
    while off < len(data):
        clen, ctype = struct.unpack_from("<II", data, off)
        chunk = data[off + 8: off + 8 + clen]
        if ctype == 0x4E4F534A:
            js = json.loads(chunk.decode("utf-8"))
        elif ctype == 0x004E4942:
            bin_ = chunk
        off += 8 + clen + ((4 - clen % 4) % 4 if clen % 4 else 0)
    return js, bin_


def accessor(gl, bin_, idx):
    """Данные аксессора как массив (N, компонент)."""
    acc = gl["accessors"][idx]
    n, ncomp = acc["count"], _NC[acc["type"]]
    fmt = _CT[acc["componentType"]]
    itemsize = np.dtype(fmt).itemsize
    bv = gl["bufferViews"][acc["bufferView"]]
    base = bv.get("byteOffset", 0) + acc.get("byteOffset", 0)
    stride = bv.get("byteStride") or (itemsize * ncomp)
    if stride == itemsize * ncomp:
        a = np.frombuffer(bin_, dtype=fmt, count=n * ncomp, offset=base).reshape(n, ncomp)
    else:  # чередующийся буфер — вынимаем по шагу
        raw = np.frombuffer(bin_, dtype=np.uint8, count=stride * n, offset=base).reshape(n, stride)
        a = raw[:, : itemsize * ncomp].copy().view(fmt).reshape(n, ncomp)
    return np.asarray(a, np.float32) if fmt == "f" else np.asarray(a)


def node_matrix(node):
    """Локальная матрица ноды: либо готовая matrix, либо T·R·S."""
    if "matrix" in node:
        return np.array(node["matrix"], np.float32).reshape(4, 4).T
    m = np.eye(4, dtype=np.float32)
    if "scale" in node:
        m[:3, :3] = m[:3, :3] @ np.diag(np.array(node["scale"], np.float32))
    if "rotation" in node:
        x, y, z, w = node["rotation"]
        r = np.array([
            [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
        ], np.float32)
        m[:3, :3] = r @ m[:3, :3]
    if "translation" in node:
        m[:3, 3] = np.array(node["translation"], np.float32)
    return m


def load_mesh(path):
    """Все примитивы файла, сведённые в один набор: позиции, нормали, UV, индексы."""
    gl, bin_ = read_glb(path)
    P, N, T, I = [], [], [], []
    base = 0

    def walk(ni, parent):
        nonlocal base
        node = gl["nodes"][ni]
        world = parent @ node_matrix(node)
        if "mesh" in node:
            for prim in gl["meshes"][node["mesh"]].get("primitives", []):
                at = prim["attributes"]
                if "POSITION" not in at or "TEXCOORD_0" not in at:
                    continue
                p = accessor(gl, bin_, at["POSITION"])
                p = (world[:3, :3] @ p.T).T + world[:3, 3]
                nrm = (accessor(gl, bin_, at["NORMAL"]) if "NORMAL" in at
                       else np.zeros_like(p))
                nrm = (world[:3, :3] @ nrm.T).T
                uv = accessor(gl, bin_, at["TEXCOORD_0"])
                idx = (accessor(gl, bin_, prim["indices"]).reshape(-1)
                       if "indices" in prim else np.arange(len(p)))
                P.append(p)
                N.append(nrm)
                T.append(uv)
                I.append(np.asarray(idx, np.int64) + base)
                base += len(p)
        for c in node.get("children", []):
            walk(c, world)

    roots = gl.get("scenes", [{}])[gl.get("scene", 0)].get("nodes", range(len(gl.get("nodes", []))))
    for r in roots:
        walk(r, np.eye(4, dtype=np.float32))
    if not P:
        raise ValueError("в %s нет пригодных примитивов" % path)
    return (np.concatenate(P), np.concatenate(N), np.concatenate(T),
            np.concatenate(I).reshape(-1, 3))


# ───────────────────────────── рендер ─────────────────────────────
def sample(tex, u, v):
    """Билинейная выборка с повтором UV. tex: (h,w,c) float [0..1]."""
    h, w = tex.shape[:2]
    x = (u % 1.0) * (w - 1)
    y = ((1.0 - v) % 1.0) * (h - 1)     # glTF: V вниз
    x0 = np.floor(x).astype(np.int32)
    y0 = np.floor(y).astype(np.int32)
    x1 = np.minimum(x0 + 1, w - 1)
    y1 = np.minimum(y0 + 1, h - 1)
    fx = (x - x0)[..., None]
    fy = (y - y0)[..., None]
    return ((tex[y0, x0] * (1 - fx) + tex[y0, x1] * fx) * (1 - fy) +
            (tex[y1, x0] * (1 - fx) + tex[y1, x1] * fx) * fy)


def render(mesh, alb, nrm_map, rgh_map, size, use_maps):
    """Кадр модели. use_maps=False — нормаль из геометрии и шероховатость-константа («до»)."""
    P, Nv, UV, F = mesh

    # ── камера: вписываем объект, смотрим спереди-слева-сверху
    lo, hi = P.min(0), P.max(0)
    center = (lo + hi) * 0.5
    radius = float(np.linalg.norm(hi - lo)) * 0.5 or 1.0
    # ★ glTF — правая система с осью Y ВВЕРХ (не Z, как в UE). С up=(0,0,1) модели ложились на бок.
    eye = center + np.array([-1.05, 0.45, 0.98], np.float32) * radius
    fwd = center - eye
    fwd /= np.linalg.norm(fwd)
    up0 = np.array([0, 1, 0], np.float32)
    right = np.cross(fwd, up0)
    right /= (np.linalg.norm(right) or 1.0)
    up = np.cross(right, fwd)
    view = np.stack([right, up, -fwd])            # строки — базис камеры

    V = (P - eye) @ view.T                        # позиции в пространстве камеры (z<0 — впереди)
    Nc = Nv @ view.T
    ln = np.linalg.norm(Nc, axis=1, keepdims=True)
    Nc = Nc / np.where(ln > 1e-6, ln, 1.0)

    f = 1.0 / np.tan(np.radians(38.0) * 0.5)      # фокусное из угла обзора
    zc = -V[:, 2]
    zc = np.where(zc < 1e-3, 1e-3, zc)
    sx = (V[:, 0] * f / zc * 0.5 + 0.5) * size
    sy = (0.5 - V[:, 1] * f / zc * 0.5) * size
    invz = 1.0 / zc

    # ── буферы кадра
    zbuf = np.full((size, size), np.inf, np.float32)
    # атрибуты пикселя: u, v, nx, ny, nz, tx, ty, tz
    gbuf = np.zeros((size, size, 8), np.float32)
    mask = np.zeros((size, size), bool)

    # ── тангент на треугольник (по градиенту UV) — нужен для карты нормалей
    a, b, c = F[:, 0], F[:, 1], F[:, 2]
    e1 = P[b] - P[a]
    e2 = P[c] - P[a]
    d1 = UV[b] - UV[a]
    d2 = UV[c] - UV[a]
    det = d1[:, 0] * d2[:, 1] - d2[:, 0] * d1[:, 1]
    det = np.where(np.abs(det) < 1e-12, 1e-12, det)
    tan = ((e1 * d2[:, 1:2] - e2 * d1[:, 1:2]) / det[:, None]) @ view.T
    tl = np.linalg.norm(tan, axis=1, keepdims=True)
    tan = tan / np.where(tl > 1e-6, tl, 1.0)

    xs = sx[F]
    ys = sy[F]
    x0 = np.clip(np.floor(xs.min(1)).astype(np.int32), 0, size - 1)
    x1 = np.clip(np.ceil(xs.max(1)).astype(np.int32), 0, size - 1)
    y0 = np.clip(np.floor(ys.min(1)).astype(np.int32), 0, size - 1)
    y1 = np.clip(np.ceil(ys.max(1)).astype(np.int32), 0, size - 1)
    area = (xs[:, 1] - xs[:, 0]) * (ys[:, 2] - ys[:, 0]) - (xs[:, 2] - xs[:, 0]) * (ys[:, 1] - ys[:, 0])
    alive = (np.abs(area) > 1e-9) & (x1 > x0) & (y1 > y0) & (zc[F].min(1) > 1e-2)

    for t in np.nonzero(alive)[0]:
        i0, i1, i2 = F[t]
        px = np.arange(x0[t], x1[t] + 1, dtype=np.float32) + 0.5
        py = np.arange(y0[t], y1[t] + 1, dtype=np.float32) + 0.5
        gx, gy = np.meshgrid(px, py)
        ax, ay = sx[i0], sy[i0]
        bx, by = sx[i1], sy[i1]
        cx, cy = sx[i2], sy[i2]
        w0 = ((bx - ax) * (gy - ay) - (by - ay) * (gx - ax)) / area[t]
        w1 = ((gx - ax) * (cy - ay) - (gy - ay) * (cx - ax)) / area[t]
        # барицентрика: l1 — вес вершины b, l2 — вершины c
        l2, l1 = w0, w1
        l0 = 1.0 - l1 - l2
        inside = (l0 >= -1e-6) & (l1 >= -1e-6) & (l2 >= -1e-6)
        if not inside.any():
            continue
        iz = l0 * invz[i0] + l1 * invz[i1] + l2 * invz[i2]
        z = 1.0 / np.where(iz > 1e-12, iz, 1e-12)
        ys_, xs_ = np.nonzero(inside)
        Y = ys_ + y0[t]
        X = xs_ + x0[t]
        zz = z[inside]
        better = zz < zbuf[Y, X]
        if not better.any():
            continue
        Y, X, zz = Y[better], X[better], zz[better]
        pl0 = (l0[inside][better] * invz[i0]) * zz
        pl1 = (l1[inside][better] * invz[i1]) * zz
        pl2 = (l2[inside][better] * invz[i2]) * zz
        uv = UV[i0] * pl0[:, None] + UV[i1] * pl1[:, None] + UV[i2] * pl2[:, None]
        # альфа-тест: у листвы и решёток прозрачные части не должны становиться чёрными плоскостями
        al = sample(alb, uv[:, 0], uv[:, 1])
        if alb.shape[2] == 4:
            keep = al[:, 3] > 0.5
            if not keep.any():
                continue
            Y, X, zz, uv = Y[keep], X[keep], zz[keep], uv[keep]
            pl0, pl1, pl2 = pl0[keep], pl1[keep], pl2[keep]
        nn = Nc[i0] * pl0[:, None] + Nc[i1] * pl1[:, None] + Nc[i2] * pl2[:, None]
        nl = np.linalg.norm(nn, axis=1, keepdims=True)
        nn = nn / np.where(nl > 1e-6, nl, 1.0)
        zbuf[Y, X] = zz
        gbuf[Y, X, 0:2] = uv
        gbuf[Y, X, 2:5] = nn
        gbuf[Y, X, 5:8] = tan[t]
        mask[Y, X] = True

    # ── затенение
    img = np.zeros((size, size, 3), np.float32)
    yy, xx = np.nonzero(mask)
    if len(yy):
        uv = gbuf[yy, xx, 0:2]
        n = gbuf[yy, xx, 2:5]
        tn = gbuf[yy, xx, 5:8]
        # грани, отвёрнутые от камеры, разворачиваем: модели оригинала двусторонние
        n = np.where((n[:, 2:3] < 0), -n, n)
        alb_px = sample(alb, uv[:, 0], uv[:, 1])[:, :3] ** 2.2   # sRGB → линейно

        if use_maps:
            tn = tn - n * (tn * n).sum(1, keepdims=True)          # ортогонализация Грама–Шмидта
            tl = np.linalg.norm(tn, axis=1, keepdims=True)
            tn = tn / np.where(tl > 1e-6, tl, 1.0)
            bt = np.cross(n, tn)
            nm = sample(nrm_map, uv[:, 0], uv[:, 1])[:, :3] * 2.0 - 1.0
            n = tn * nm[:, 0:1] + bt * nm[:, 1:2] + n * nm[:, 2:3]
            nl = np.linalg.norm(n, axis=1, keepdims=True)
            n = n / np.where(nl > 1e-6, nl, 1.0)
            rough = sample(rgh_map, uv[:, 0], uv[:, 1])[:, 0]
        else:
            rough = np.full(len(yy), ROUGH_CONST, np.float32)

        ndl = np.clip(n @ SUN, 0.0, 1.0)[:, None]
        vdir = np.array([0, 0, 1], np.float32)
        h = SUN + vdir
        h /= np.linalg.norm(h)
        ndh = np.clip(n @ h, 0.0, 1.0)
        # блеск из шероховатости: гладкое → узкий яркий блик, матовое → почти ничего
        expn = np.clip(2.0 / np.maximum(rough, 0.05) ** 4 - 2.0, 1.0, 4096.0)
        spec = (1.15 * (1.0 - rough) ** 2 * ndh ** expn)[:, None]
        col = alb_px * (AMBIENT * SKY_COLOR + ndl * SUN_COLOR) + spec * SUN_COLOR
        img[yy, xx] = np.clip(col, 0, None) ** (1 / 2.2)          # линейно → sRGB

    # фон: мягкий вертикальный градиент, чтобы силуэт читался
    g = np.linspace(0.16, 0.30, size, dtype=np.float32)[:, None]
    bg = np.repeat(np.stack([g * 0.95, g * 0.99, g * 1.10], -1), size, axis=1)
    out = np.where(mask[..., None], img, bg)
    return (np.clip(out, 0, 1) * 255).astype(np.uint8)


def load_tex(path, rgba=False):
    im = Image.open(path)
    im = im.convert("RGBA" if rgba else "RGB")
    return np.asarray(im, np.float32) / 255.0


def font(size):
    for p in (r"C:/Windows/Fonts/segoeui.ttf", r"C:/Windows/Fonts/arial.ttf"):
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


def main():
    f_hdr = font(34)
    f_lbl = font(23)
    f_sub = font(18)

    pad, top, gap = 18, 132, 34
    sheet_w = pad * 2 + CELL * 2 + gap
    sheet_h = top + (CELL + 58) * len(SAMPLES) + pad
    sheet = Image.new("RGB", (sheet_w, sheet_h), (24, 25, 29))
    dr = ImageDraw.Draw(sheet)
    dr.text((pad, 22), "Объём из плоских текстур — пять типов поверхности", (238, 238, 242), font=f_hdr)
    dr.text((pad, 62), "слева — как сейчас (только альбедо, шероховатость 0.9) · справа — карта нормалей из альбедо + карта шероховатости",
            (150, 152, 162), font=f_sub)

    for row, (key, label, rel) in enumerate(SAMPLES):
        mesh = load_mesh(os.path.join(MODELS, rel))
        alb = load_tex(os.path.join(MAPS, "%s_A.png" % key), rgba=True)
        nrm = load_tex(os.path.join(MAPS, "%s_N.png" % key))
        rgh = load_tex(os.path.join(MAPS, "%s_R.png" % key))
        print("  %-6s треугольников=%d текстура=%dx%d" % (key, len(mesh[3]), alb.shape[1], alb.shape[0]))

        y = top + row * (CELL + 58)
        dr.text((pad, y - 30), label, (226, 228, 234), font=f_lbl)
        for col, use in ((0, False), (1, True)):
            px = render(mesh, alb, nrm, rgh, CELL, use)
            x = pad + col * (CELL + gap)
            sheet.paste(Image.fromarray(px), (x, y))
            dr.rectangle([x, y, x + CELL - 1, y + CELL - 1], outline=(58, 60, 68))
            tag = "ПОСЛЕ" if use else "ДО"
            tw = dr.textlength(tag, font=f_lbl)
            dr.rectangle([x + 10, y + 10, x + 26 + tw, y + 44], fill=(18, 19, 22))
            dr.text((x + 18, y + 14), tag, (255, 214, 120) if use else (172, 176, 186), font=f_lbl)

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    sheet.save(OUT)
    print("лист ->", OUT)


if __name__ == "__main__":
    main()
