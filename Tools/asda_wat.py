# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""asda_wat.py — разбор `.WAT` (геометрия воды карты) и растеризация её в маску тайлов.

ЗАЧЕМ. Шагам нужно знать, накрыта ли точка водой: по водному тайлу персонаж роняет всплеск,
а по мостику над той же водой — нет. Эталон спрашивает саму геометрию
(`sub_926A07` → `sub_B5E9B0` → `pickElems_testAnyHit` → `pickSpriteQuadRayTest`): элемент воды
сперва отсекается по прямоугольнику X/Z, и при попадании считается высота поверхности.
Сравнения высот там нет — важен только горизонтальный охват. Поэтому ответ можно посчитать
заранее: бит на тайл, «хотя бы один водный треугольник накрывает этот тайл».

ФОРМАТ (из `AsdaMapFormats.h`, сверено на alpen.WAT = 10 145 128 б):
    заголовок : u32 magic = 0x20060214, u32 count
    записи[count] : по 0x348 байт, начиная с 0x08
        +0x258 i32 triCount      — треугольников
        +0x25c i32 vtxCount      — вершин
        +0x264 i32 stride (= 40) — вершина это 10 float: x, y, z, e0, u, v, u2, v2, uD, vD
        +0x2cc угол bbox A (x,y,z), +0x2d8 угол bbox B
    геометрия[count] : в порядке записей, каждая = [triCount*3 u16 индексов][vtxCount*40 байт]

Координаты в файле — игровые (x, y-высота, z), те же, что у `.TER`, поэтому тайл = floor(x/100).
"""
import os
import struct

MAGIC = 0x20060214
REC_SIZE = 0x348
OFF_TRI = 0x258
OFF_VTX = 0x25C
OFF_STRIDE = 0x264
VERT_FLOATS = 10


def wat_path(ter_dir, folder, map_name=None):
    """Путь к `.WAT` карты: <folder>.WAT, иначе первый попавшийся в папке."""
    d = os.path.join(ter_dir, folder)
    if not os.path.isdir(d):
        return None
    cand = os.path.join(d, (map_name or folder) + ".WAT")
    if os.path.isfile(cand):
        return cand
    for f in os.listdir(d):
        if f.lower().endswith(".wat"):
            return os.path.join(d, f)
    return None


def load_triangles(path):
    """Все треугольники воды как список кортежей ((x1,z1),(x2,z2),(x3,z3)) в игровых координатах.

    Возвращает (треугольники, число_тел). Пустой список = воды нет или файл не наш.
    """
    with open(path, "rb") as f:
        b = f.read()
    if len(b) < 8:
        return [], 0
    magic, count = struct.unpack_from("<II", b, 0)
    if magic != MAGIC or count <= 0 or count > 4096:
        return [], 0

    recs = []
    for i in range(count):
        base = 8 + i * REC_SIZE
        if base + REC_SIZE > len(b):
            return [], 0
        tri = struct.unpack_from("<i", b, base + OFF_TRI)[0]
        vtx = struct.unpack_from("<i", b, base + OFF_VTX)[0]
        stride = struct.unpack_from("<i", b, base + OFF_STRIDE)[0]
        recs.append((tri, vtx, stride))

    tris = []
    cur = 8 + count * REC_SIZE
    for tri, vtx, stride in recs:
        if tri <= 0 or vtx <= 0 or stride != VERT_FLOATS * 4:
            # Тело без геометрии или с неожиданным шагом вершины — пропускаем вместе с его блоком.
            cur += max(0, tri) * 3 * 2 + max(0, vtx) * max(0, stride)
            continue
        idx_bytes = tri * 3 * 2
        vtx_bytes = vtx * stride
        if cur + idx_bytes + vtx_bytes > len(b):
            break
        idx = struct.unpack_from("<%dH" % (tri * 3), b, cur)
        vbase = cur + idx_bytes
        # Берём только X и Z каждой вершины: высота для ответа «над водой» не нужна.
        xs = [0.0] * vtx
        zs = [0.0] * vtx
        for v in range(vtx):
            o = vbase + v * stride
            xs[v], zs[v] = struct.unpack_from("<f", b, o)[0], struct.unpack_from("<f", b, o + 8)[0]
        for t in range(tri):
            a, c, d = idx[t * 3], idx[t * 3 + 1], idx[t * 3 + 2]
            if a < vtx and c < vtx and d < vtx:
                tris.append(((xs[a], zs[a]), (xs[c], zs[c]), (xs[d], zs[d])))
        cur += idx_bytes + vtx_bytes
    return tris, count


def rasterize_tiles(tris, width, height, cell):
    """Треугольники → множество индексов тайлов (tz*width + tx), которые они накрывают.

    Способ прямой: для каждого треугольника берём его прямоугольник и проверяем центры тайлов
    внутри. Треугольники воды крупные (сотни единиц), поэтому центра достаточно; на всякий
    случай тайл засчитывается и когда его центр лежит на ребре.
    """
    covered = set()
    for (ax, az), (bx, bz), (cx, cz) in tris:
        minx = min(ax, bx, cx); maxx = max(ax, bx, cx)
        minz = min(az, bz, cz); maxz = max(az, bz, cz)
        tx0 = max(0, int(minx // cell)); tx1 = min(width - 1, int(maxx // cell))
        tz0 = max(0, int(minz // cell)); tz1 = min(height - 1, int(maxz // cell))
        if tx1 < tx0 or tz1 < tz0:
            continue
        # Знаменатель барицентрических координат — общий для всего треугольника.
        den = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz)
        if den == 0.0:
            continue
        for tz in range(tz0, tz1 + 1):
            pz = (tz + 0.5) * cell
            for tx in range(tx0, tx1 + 1):
                idx = tz * width + tx
                if idx in covered:
                    continue
                px = (tx + 0.5) * cell
                u = ((bz - cz) * (px - cx) + (cx - bx) * (pz - cz)) / den
                v = ((cz - az) * (px - cx) + (ax - cx) * (pz - cz)) / den
                if u >= 0.0 and v >= 0.0 and (u + v) <= 1.0:
                    covered.add(idx)
    return covered


def water_bits(ter_dir, folder, width, height, cell, map_name=None):
    """Бит-маска воды по тайлам: (bytearray, сколько тайлов накрыто, сколько тел)."""
    nbytes = (width * height + 7) // 8
    bits = bytearray(nbytes)
    path = wat_path(ter_dir, folder, map_name)
    if not path:
        return bits, 0, 0
    tris, bodies = load_triangles(path)
    if not tris:
        return bits, 0, bodies
    covered = rasterize_tiles(tris, width, height, cell)
    for i in covered:
        bits[i >> 3] |= (1 << (i & 7))
    return bits, len(covered), bodies
