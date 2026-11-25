# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""asda_water_swim.py — маска «открытой воды» карт (плавание): ЕДИНЫЙ источник для сервера и клиента.

ЗАЧЕМ (задача владельца 28.10.2026). В клиенте есть плавание (перенос из донора AsdaFableClassic, 23.10), но в воду не
войти: и клиент, и сервер считают водные тайлы непроходимыми.
  • Клиент: проходимость = байт1 флага тайла `.TER` (блок при {1,2,129,130,65,193}); вода помечена значением 1 (бит 0) —
    тем же, что стены (walkable_grid_re.md, сверка water_swim: вода в .TER = 1/129/65/193, значения «2» на воде нет).
  • Сервер: GameServer `MapData_Info\\<MapInfo-имя>.sim`, u32 на клетку, бит 0 = непроходимо
    (`sub_419E50` загрузка, `sub_41A010` проверка: MoveSyncProcess@0x5427D0, навыки RecvChar_SkillUse, удары
    Send_CharNormalMonsterAttack/Recv_User_AttackToPlayer, P57 move guard). Бит 1/2 на водных тайлах не стоят.
  Различить «воду» и «стену» по флагу нельзя — только геометрией: тайл ВОДНЫЙ, если над ним есть плоскость `.WAT`
  выше земли `.TER` (хотя бы один угол тайла под водой).

ЧТО ОТКРЫВАЕМ (тайл получает бит маски), по порядку:
  1) блок-тайл (байт1 .TER ∈ BLOCK), над центром которого есть вода `.WAT`, и поверхность выше нижнего угла земли тайла;
  2) тело воды — НЕ лава: тела с текстурой цвета лавы (средний цвет PNG красный) пропускаются целиком
     (Mountain Flamion, castle_of_chaos, dragonlair, firewayforest, inferion — WATER_005/05/006 «152,18,20»);
  3) нет ОБЪЕКТА карты, торчащего из воды (камень, свая, лодка, тростник): треугольник инстанса `.mapraw`, у которого
     нижняя вершина ниже поверхности + kObjLow, а верхняя выше поверхности + kObjHigh, — его тайлы остаются блоком;
  4) тайл не у края карты (валидные тайлы клиента 1..510; берём запас kEdge);
  4б) БЕРЕГ: реки в ущельях (Rein: суша 800–880, дно 0–20, вода 350) отделены от суши полосой СУХИХ блок-тайлов склона —
     без них в воду не войти. Берег = блок-тайл под плоскостью `.WAT`, земля выше поверхности не больше kShoreRise, перепад
     углов тайла ≤ kShoreSlope (круче — обрыв), не дальше kShoreBand шагов от воды; объекты проверяются от верха земли;
  5) тайл СВЯЗАН с исходной сушей (4-соседство через открытые тайлы), а берег ещё и ведёт к воде — замкнутые озёра
     без выхода и полосы берега без воды не открываем;
  5б) не дальше kMaxFromLand тайлов от исходной суши (широкие разливы у края карты: Альпия, поле битвы);
  6) OCEAN_LIMIT: у карты с «океаном» (решение владельца 28.10: sunnycoast — только ~30 % зоны) тела-океаны
     открываются полосой от берега: в каждом столбце X — 30 % глубины зоны от береговой линии до края карты; столбцы
     без берега (океан за пределами береговой линии) закрыты — до края карты не доплыть.

Выход (`--write`): `Tools/data/water_swim/<карта>.bits` — бит-пак 1 бит/тайл (tz*W+tx), как `UAsdaWalkGrid::Bits`;
`Tools/data/water_swim/summary.json` — числа по картам; `--png <папка>` — превью (серый суша, синий открыто, красный
объект, тёмно-синий оставлено блоком). Сервер: `--make-sims <папка .sim> <папка вывода>` — копии `.sim` со снятым
битом 0 на открытых тайлах (биты 1/2 и прочие не трогаются). Клиент: `asda_export_swimbits.py` (headless UE) пишет
маску в `DA_WalkGrid.SwimBits` (ключ `asda.Swim.OpenWater`).

Модуль без побочных эффектов при импорте (tools-no-import-side-effects).
"""
import os
import sys
import json
import struct
import collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_paths as _P                # noqa: E402
import asda_maps as _MAPS              # noqa: E402
import asda_ter_height as _TER         # noqa: E402
import asda_wat as _WAT                # noqa: E402
import asda_mapraw as _MR              # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(HERE, "data", "water_swim")                       # маски .bits + summary.json
MAPRAW_DIR = os.path.join(_P.PORT_DIR, "Data", "Export", "maps")         # <карта>.mapraw (объекты карты)
WATER_TEX_DIR = os.path.join(_P.PORT_DIR, "Data", "Export", "maps")      # <карта>/textures/WATER*.png
REGISTRY = os.path.join(HERE, "data", "asda_map_registry.json")          # клиентская папка → MapInfo-имя сервера

kEdge = 3            # тайлы ближе kEdge к краю сетки не открываем (клиент валиден 1..510)
kObjLow = 50.0       # объект «в воде»: нижняя вершина треугольника ниже поверхности + 50
kObjHigh = 30.0      # … и верхняя выше поверхности + 30 (торчит над водой)
kObjMaxSpan = 3000.0 # треугольники шире 30 тайлов не считаем препятствием (меш-скала/склон, а не предмет в воде)
kShoreSlope = 250.0  # берег: перепад углов тайла не больше 250 на 100 ед. (круче — обрыв, остаётся блоком)
kShoreRise = 700.0   # берег: верх земли не выше поверхности воды + 700 (ущелья Rein: суша 880, вода 350)
kShoreBand = 10      # берег: не дальше 10 тайлов от воды (дальше — обычный блок суши, не спуск к воде)
kSwimDepth = 100.0   # порог плавания клиента (донор flt_C7501C: земля + 100 < вода) — глубже мобам нельзя
kMaxFromLand = 40    # вода не дальше 40 тайлов (8-соседство) от исходной суши — не уплыть к краю карты (кроме OCEAN_LIMIT)
# ★ 08.11.2026 [ИСПРАВЛЕНИЕ] (пачка 18, баг 9: «игроки проходят сквозь невидимые стены», Альпен x106 y384 и x54 y403).
#   Маска выше открывала блок-тайлы, до которых от суши оригинала можно дойти только СКВОЗЬ стену:
#     • обрыв: «мокрый» тайл проверялся лишь по `вода > нижний угол земли`, без перепада к соседу — провал в центре Альпена
#       (земля .TER −1000 под башней `tower_1`, мостовая вокруг 864, вода 446) и котловины под обрывами Санни-Кост стали
#       «водой», в них шагали с края мостовой/берега вниз на 400–600;
#     • кромка города: полоса «берега» (п. 4б) открывала РОВНЫЕ блок-тайлы безопасной зоны (бит 0x80 байта1 .TER) на уровне
#       мостовой — граница города, которую оригинал держит стеной (Альпен: все 45 входов в море вокруг плато — с мостовой
#       на 724–736, Силалис — так же).
#   Правило «стены» (множество `wall`, клиент `DA_WalkGrid.SwimWallBits`, ключ `asda.Swim.WallGuard`): открытый тайл остаётся
#   открытым, только если до него можно дойти от проходимой суши `.TER` 4-соседством по открытым тайлам, НЕ входя в «берег»
#   безопасной зоны и делая шаг не круче kMaxStep (высота стояния: центр земли; в глубокой воде — поверхность).
#   kMaxStep = 230: перепад соседних ПРОХОДИМЫХ тайлов оригинала — 99,99 % ≤ 175 на всех 22 картах, максимум ≤ 231 на всех,
#   кроме единичных лестниц (castle_of_chaos 517, firewayforest 544, inferion 895).
kMaxStep = 230.0

# ★ Решение владельца 28.10.2026: на sunnycoast «океан» открыть только ~30 % (не доплыть до края карты).
#   Тело-океан узнаём по прямоугольнику .WAT (+0x2cc): поверхность 400 и северная кромка карты (z ≥ 34000).
OCEAN_LIMIT = {
    "sunnycoast": {"fraction": 0.30, "min_z": 34000.0, "surface": 400.0},
}

# ★ 31.10.2026 РЕШЕНИЕ ВЛАДЕЛЬЦА (отзыв игроков «you shouldn't be able to go in water» на поле «Битвы армий»): карты, где ИГРОКУ
#   вода не открывается вовсе — маска `open` пустая (клиент `DA_WalkGrid.SwimBits` очищается, `--make-sims` файл не делает;
#   сервер — `AstoryServer/Patches/AsdaPatch/tools/deploy_water_swim_bf_close.py` вернул исходный .sim). Маски мобов
#   (`mob_open`/`mob_close`, NpcServer) считаются как прежде — их выкладка 28.10 не меняется.
EXCLUDE = {
    "battlefield": "карта 19 «Битва армий» (CampWar): в воду не входить — решение владельца 31.10.2026",
}


def _is_lava_png(path):
    """Средний цвет текстуры воды — красный (лава). Нет файла/PIL — не лава."""
    try:
        from PIL import Image
        r, g, b = Image.open(path).convert("RGB").resize((1, 1), Image.BOX).getpixel((0, 0))
    except Exception:
        return False
    return r > 110 and g < 60 and b < 60


def load_water_bodies(map_name):
    """Тела воды `.WAT`: список {tex, lava, bbox, tris[(A,B,C) game xyz]}."""
    folder, fname = _TER.MAP_TER.get(map_name, (map_name, map_name + ".TER"))
    wp = _WAT.wat_path(_TER.TER_DIR, folder, fname[:-4])
    if not wp:
        return []
    b = open(wp, "rb").read()
    if len(b) < 8:
        return []
    magic, count = struct.unpack_from("<II", b, 0)
    if magic != _WAT.MAGIC or count <= 0 or count > 4096:
        return []
    recs = []
    for i in range(count):
        base = 8 + i * _WAT.REC_SIZE
        tri = struct.unpack_from("<i", b, base + _WAT.OFF_TRI)[0]
        vtx = struct.unpack_from("<i", b, base + _WAT.OFF_VTX)[0]
        stride = struct.unpack_from("<i", b, base + _WAT.OFF_STRIDE)[0]
        raw = b[base:base + _WAT.REC_SIZE]
        # Имя первой текстуры записи — первая ASCII-строка «\map\...\Water*.ext».
        tex = ""
        k = raw.find(b"\\map\\")
        if k >= 0:
            e = raw.find(b"\x00", k)
            tex = raw[k:e].decode("latin1", "replace")
        bbox = struct.unpack_from("<6f", raw, 0x2CC)
        recs.append((tri, vtx, stride, tex, bbox))
    tex_dir = os.path.join(WATER_TEX_DIR, map_name, "textures")
    bodies = []
    cur = 8 + count * _WAT.REC_SIZE
    for tri, vtx, stride, tex, bbox in recs:
        if tri <= 0 or vtx <= 0 or stride != _WAT.VERT_FLOATS * 4:
            cur += max(0, tri) * 6 + max(0, vtx) * max(0, stride)
            continue
        idx = struct.unpack_from("<%dH" % (tri * 3), b, cur)
        vb = cur + tri * 6
        V = [struct.unpack_from("<3f", b, vb + v * stride) for v in range(vtx)]
        tris = []
        for t in range(tri):
            a, c, d = idx[3 * t:3 * t + 3]
            if max(a, c, d) < vtx:
                tris.append((V[a], V[c], V[d]))
        cur += tri * 6 + vtx * stride
        png = os.path.join(tex_dir, os.path.splitext(os.path.basename(tex.replace("\\", "/")))[0].upper() + ".png")
        bodies.append({"tex": tex, "lava": _is_lava_png(png), "bbox": bbox, "tris": tris})
    return bodies


def _raster_tris(tris, W, H, cell, cb):
    """Центры тайлов внутри треугольников (x,z); cb(idx, u, v, tri) на каждое попадание."""
    for T in tris:
        (ax, ay, az), (bx, by, bz), (cx, cy, cz) = T
        den = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz)
        if den == 0.0:
            continue
        tx0 = max(0, int(min(ax, bx, cx) // cell)); tx1 = min(W - 1, int(max(ax, bx, cx) // cell))
        tz0 = max(0, int(min(az, bz, cz) // cell)); tz1 = min(H - 1, int(max(az, bz, cz) // cell))
        for tz in range(tz0, tz1 + 1):
            pz = (tz + 0.5) * cell
            for tx in range(tx0, tx1 + 1):
                px = (tx + 0.5) * cell
                u = ((bz - cz) * (px - cx) + (cx - bx) * (pz - cz)) / den
                v = ((cz - az) * (px - cx) + (ax - cx) * (pz - cz)) / den
                if u >= 0.0 and v >= 0.0 and u + v <= 1.0:
                    cb(tz * W + tx, u, v, T)


def water_surface(bodies, W, H, cell):
    """Тайл → (высота поверхности в центре, номер тела). При перекрытии — верхняя поверхность."""
    surf = {}
    for bi, body in enumerate(bodies):
        def put(i, u, v, T, bi=bi):
            y = u * T[0][1] + v * T[1][1] + (1.0 - u - v) * T[2][1]
            if i not in surf or y > surf[i][0]:
                surf[i] = (y, bi)
        _raster_tris(body["tris"], W, H, cell, put)
    return surf


def _read_mapraw_objects(path):
    """Модели и инстансы `.mapraw` (тот же порядок секций, что `asda_build_map_level.read_mapraw`)."""
    r, ver, _name = _MR.open_mapraw(path)
    _MR.scene_fields(r, ver=ver)
    pc = r.u32(); ic = r.u32(); r.u16arr(ic)
    for _ in range(pc):
        vtx = r.u32()
        if vtx > 0:
            r.farr(vtx * 3); r.farr(vtx * 2); r.farr(vtx * 2); r.u8arr(vtx * 4)
        r.u32(); r.u32(); r.f32(); r.f32(); r.s(); r.f32(); r.s(); r.f32()
    mc = r.u32(); models = []
    for _ in range(mc):
        nm = r.s(); r.s(); vtx = r.u32()
        pos = r.farr(vtx * 3); r.farr(vtx * 3); r.farr(vtx * 2)
        icnt = r.u32(); idx = r.u32arr(icnt)
        models.append((nm, pos, idx))
    n = r.u32(); inst = []
    for _ in range(n):
        mi = r.u32(); m = r.farr(16); r.i32(); r.i32(); r.farr(4)
        inst.append((mi, m))
    return models, inst


def object_tiles(map_name, cand, ref, W, H, cell):
    """Тайлы-кандидаты, в которых объект карты торчит над опорной высотой ref[тайл] (вода — поверхность, берег — верх земли; п.3 шапки)."""
    path = os.path.join(MAPRAW_DIR, map_name + ".mapraw")
    if not os.path.isfile(path) or not cand:
        return set(), {}
    models, inst = _read_mapraw_objects(path)
    # Прямоугольник кандидатов — отсев инстансов по положению.
    xs = [i % W for i in cand]; zs = [i // W for i in cand]
    bx0, bx1 = (min(xs) - 2) * cell, (max(xs) + 3) * cell
    bz0, bz1 = (min(zs) - 2) * cell, (max(zs) + 3) * cell
    hit = set(); names = collections.Counter()
    for mi, m in inst:
        if mi >= len(models):
            continue
        nm, pos, idx = models[mi]
        if not pos or not idx:
            continue
        # Мировые вершины инстанса (строка-вектор D3D: p' = p·M, перенос m[12..14]).
        P = []
        for k in range(0, len(pos), 3):
            x, y, z = pos[k], pos[k + 1], pos[k + 2]
            P.append((x * m[0] + y * m[4] + z * m[8] + m[12],
                      x * m[1] + y * m[5] + z * m[9] + m[13],
                      x * m[2] + y * m[6] + z * m[10] + m[14]))
        px = [p[0] for p in P]; pz = [p[2] for p in P]
        if max(px) < bx0 or min(px) > bx1 or max(pz) < bz0 or min(pz) > bz1:
            continue
        tris = []
        for t in range(0, len(idx) - 2, 3):
            a, b, c = idx[t], idx[t + 1], idx[t + 2]
            if max(a, b, c) >= len(P):
                continue
            A, B, C = P[a], P[b], P[c]
            if max(A[0], B[0], C[0]) - min(A[0], B[0], C[0]) > kObjMaxSpan:
                continue
            if max(A[2], B[2], C[2]) - min(A[2], B[2], C[2]) > kObjMaxSpan:
                continue
            tris.append((A, B, C))
        if not tris:
            continue
        before = len(hit)
        # Инстанс «в воде», если его нижняя точка ниже опоры + kObjLow; тогда блок — каждый тайл, где ЛЮБОЙ его треугольник
        #   выше опоры + kObjHigh (так камень закрывается целиком, а не кольцом боковин). Дерево на высоком берегу (низ
        #   ствола выше воды) воду под кроной не закрывает.
        inst_lo = min(p[1] for p in P)

        def put(i, u, v, T):
            if i not in cand:
                return
            s = ref[i]
            if inst_lo < s + kObjLow and max(T[0][1], T[1][1], T[2][1]) > s + kObjHigh:
                hit.add(i)
        _raster_tris(tris, W, H, cell, put)
        # Маленький треугольник может не накрыть ни одного центра тайла — засчитываем тайлы его вершин.
        for (A, B, C) in tris:
            hi = max(A[1], B[1], C[1])
            for Pv in (A, B, C):
                tx = int(Pv[0] // cell); tz = int(Pv[2] // cell)
                if 0 <= tx < W and 0 <= tz < H:
                    i = tz * W + tx
                    if i in cand and inst_lo < ref[i] + kObjLow and hi > ref[i] + kObjHigh:
                        hit.add(i)
        if len(hit) > before:
            names[nm] += len(hit) - before
    return hit, dict(names.most_common(12))


def compute(map_name):
    """Маска открытой воды карты. Возвращает dict (W, H, open:set, stats…) или None (нет .TER/воды)."""
    p = _TER.ter_path(map_name)
    if not p:
        return None
    t = _TER.TerFile(p)
    t.check_body()
    W, H, cell = t.width, t.height, t.cell
    b1 = t.tile_attr_byte1()
    corners = t.corner_heights()
    bodies = load_water_bodies(map_name)
    res = {"map": map_name, "W": W, "H": H, "open": set(), "bodies": len(bodies),
           "lava_bodies": sum(1 for b in bodies if b["lava"]), "wet_blocked": 0, "lava": 0, "edge": 0,
           "objects": 0, "object_models": {}, "unreachable": 0, "shore": 0, "far": 0, "ocean_cut": 0, "ocean": {},
           "wall": set(), "wall_peace_shore": 0, "wall_step": 0}
    if not bodies:
        return res
    surf = water_surface(bodies, W, H, cell)
    walk = [v not in _TER.BLOCK_VALUES for v in b1]
    wet = set()      # блок-тайлы под водой
    shore = set()    # блок-тайлы берега: под плоскостью воды, но земля выше поверхности (спуск к воде)
    ref = {}         # опорная высота для проверки объектов: поверхность воды (вода) / верх земли (берег)
    for i, (y, bi) in surf.items():
        if walk[i]:
            continue
        g = corners[4 * i:4 * i + 4]
        tx, tz = i % W, i // W
        edge = tx < kEdge or tz < kEdge or tx >= W - kEdge or tz >= H - kEdge
        if y > min(g):
            res["wet_blocked"] += 1
            if bodies[bi]["lava"]:
                res["lava"] += 1; continue
            if edge:
                res["edge"] += 1; continue
            wet.add(i); ref[i] = y
        elif not bodies[bi]["lava"] and not edge and max(g) - min(g) <= kShoreSlope and max(g) - y <= kShoreRise:
            shore.add(i); ref[i] = max(g)
    cand = wet | shore
    # П.5б: удалённость от исходной суши (8-соседство по всей сетке); тела-океаны OCEAN_LIMIT режет своё правило.
    lim0 = OCEAN_LIMIT.get(map_name)
    ocean0 = set()
    if lim0:
        ocean0 = {bi for bi, b in enumerate(bodies)
                  if abs(b["bbox"][1] - lim0["surface"]) < 1.0 and min(b["bbox"][2], b["bbox"][5]) >= lim0["min_z"]}
    far = _far_from_land(walk, W, H, kMaxFromLand)
    res["far"] = len([i for i in cand if i in far and surf[i][1] not in ocean0])
    cand = {i for i in cand if i not in far or surf[i][1] in ocean0}
    objs, names = object_tiles(map_name, cand, ref, W, H, cell)
    res["objects"] = len(objs); res["object_models"] = names
    cand -= objs
    wet -= objs
    # Полоса берега: шаги до ближайшей воды (BFS от воды по кандидатам); дальше kShoreBand — не берег, а просто суша-блок.
    to_wet = _bfs(wet, cand, W, H)
    cand = {i for i in cand if i in wet or to_wet.get(i, 1 << 30) <= kShoreBand}
    # Связность с исходной сушей + расстояние от суши (BFS по открытым тайлам).
    seeds = set()
    for i in cand:
        tx, tz = i % W, i // W
        for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, nz = tx + dx, tz + dz
            if 0 <= nx < W and 0 <= nz < H and walk[nz * W + nx]:
                seeds.add(i); break
    dist = _bfs(seeds, cand, W, H)
    # Берег без выхода к воде (связан с сушей, но вода от него недостижима) не открываем.
    reach_wet = {i for i in dist if i in wet}
    back = _bfs(reach_wet, set(dist), W, H)
    dist = {i: d for i, d in dist.items() if i in back}
    res["unreachable"] = len(wet) - len(reach_wet)
    res["shore"] = len([i for i in dist if i in shore])
    opened = set(dist)
    lim = OCEAN_LIMIT.get(map_name)
    if lim:
        ocean_bodies = [bi for bi, b in enumerate(bodies)
                        if abs(b["bbox"][1] - lim["surface"]) < 1.0 and min(b["bbox"][2], b["bbox"][5]) >= lim["min_z"]]
        ocean = {i for i in opened if surf[i][1] in ocean_bodies}
        if ocean:
            # Океан у северной кромки: в каждом столбце X берег = самый северный тайл суши/открытого не-океана южнее
            #   океана; глубина зоны = от берега до края карты; открываем 30 % глубины. Столбцы без берега (океан
            #   западнее/восточнее береговой линии) не открываем — вдоль кромки к краю карты не проплыть.
            keep = set(); depth_max = 0
            for tx in range(W):
                col = [tz for tz in range(H) if (tz * W + tx) in ocean]
                if not col:
                    continue
                z0 = min(col)
                # Берег столбца — первый не-океанский тайл южнее океана: суша, открытый берег/вода или объект у кромки
                #   (камни на линии прибоя). Нет суши в 12 тайлах — столбец за пределами береговой линии.
                shore_z = None
                for tz in range(z0 - 1, max(-1, z0 - 12), -1):
                    j = tz * W + tx
                    if walk[j] or (j in opened and j not in ocean) or j in objs:
                        shore_z = tz; break
                if shore_z is None:
                    continue
                depth = (H - 1) - shore_z
                depth_max = max(depth_max, depth)
                lim_z = shore_z + int(round(depth * lim["fraction"]))
                keep |= {tz * W + tx for tz in col if tz <= lim_z}
            # Остаток должен быть связан с сушей (BFS по оставленному).
            kept_open = (opened - ocean) | keep
            seeds = {i for i in kept_open if dist.get(i) == 1}
            dd = _bfs(seeds, kept_open, W, H)
            new_open = set(dd)
            res["ocean_cut"] = len(opened) - len(new_open)
            res["ocean"] = {"bodies": ocean_bodies, "tiles": len(ocean), "kept": len(keep & new_open),
                            "depth_max": depth_max, "fraction": lim["fraction"]}
            opened = new_open
    res["open"] = opened
    res["wall"], res["wall_peace_shore"], res["wall_step"] = wall_tiles(res, corners, b1, surf, walk)
    # ★ 28.10.2026 (решение владельца): мобам — только мелководье. Тайл открыт мобам, если открыт игроку и ни в одной его
    #   точке у игрока не включится плавание: поверхность − НИЖНИЙ угол земли ≤ kSwimDepth (берег-спуск сухой — тоже).
    res["mob_open"] = {i for i in opened if surf[i][0] - min(corners[4 * i:4 * i + 4]) <= kSwimDepth}
    # ★ 28.10.2026 (баг владельца: Клевун посреди озера Rein): мобам ЗАКРЫТА вся глубокая вода (не лава), даже исходно проходимая —
    #   в стоке озёра бывают проходимыми для всех (Rein: 4 точки спавна 17/18 в озере). Закрытую клетку появления NpcServer
    #   заменяет ближайшей проходимой по спирали (`sub_41AC10`, NpcServer.exe.c:18539) — моб встанет у берега.
    res["mob_close"] = {i for i, (y, bi) in surf.items()
                        if not bodies[bi]["lava"] and y - min(corners[4 * i:4 * i + 4]) > kSwimDepth}
    res["surf"] = surf
    res["objs"] = objs
    res["walk"] = walk
    res["lava_set"] = {i for i, (y, bi) in surf.items() if bodies[bi]["lava"]}
    return res


def _far_from_land(walk, W, H, limit):
    """Тайлы дальше limit шагов (8-соседство) от проходимой суши."""
    near = bytearray(W * H)
    cur = [i for i in range(W * H) if walk[i]]
    for i in cur:
        near[i] = 1
    for _ in range(limit):
        nxt = []
        for i in cur:
            tx, tz = i % W, i // W
            for dz in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    nx, nz = tx + dx, tz + dz
                    if 0 <= nx < W and 0 <= nz < H:
                        j = nz * W + nx
                        if not near[j]:
                            near[j] = 1; nxt.append(j)
        cur = nxt
    return {i for i in range(W * H) if not near[i]}


def _bfs(seeds, allowed, W, H):
    """Шаги 4-соседства от seeds (шаг 1) по тайлам allowed: {тайл: шаг}."""
    dist = {i: 1 for i in seeds if i in allowed}
    q = collections.deque(dist)
    while q:
        i = q.popleft()
        tx, tz = i % W, i // W
        for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, nz = tx + dx, tz + dz
            j = nz * W + nx
            if 0 <= nx < W and 0 <= nz < H and j in allowed and j not in dist:
                dist[j] = dist[i] + 1; q.append(j)
    return dist


def wall_tiles(res, corners, b1, surf, walk):
    """★ 08.11.2026 [ИСПРАВЛЕНИЕ]: открытые тайлы, до которых от суши не дойти без стены (обрыв > kMaxStep или кромка города).

    Возвращает (wall, n_peace_shore, n_step): wall — множество тайлов, которые клиент держит блоком при `asda.Swim.WallGuard 1`.
    """
    W, H = res["W"], res["H"]
    op = res["open"]
    if not op:
        return set(), 0, 0
    ctr = {}

    def gc(i):
        v = ctr.get(i)
        if v is None:
            v = ctr[i] = sum(corners[4 * i:4 * i + 4]) / 4.0
        return v

    def stand(i):
        # Высота стояния: на суше и мелководье — земля; в глубокой воде (земля ниже поверхности − kSwimDepth) — поверхность.
        g = gc(i)
        if i in op:
            s = surf[i][0]
            return g if g >= s - kSwimDepth else s
        return g

    # Берег безопасной зоны: суша (центр земли выше поверхности) с битом 0x80 — граница города, не открываем.
    peace_shore = {i for i in op if (b1[i] & _TER.PEACE_BIT) and gc(i) >= surf[i][0]}
    allowed = op - peace_shore
    reach = set()
    q = collections.deque()
    for i in allowed:
        tx, tz = i % W, i // W
        for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, nz = tx + dx, tz + dz
            if 0 <= nx < W and 0 <= nz < H:
                j = nz * W + nx
                if walk[j] and abs(stand(i) - stand(j)) <= kMaxStep:
                    reach.add(i); q.append(i); break
    while q:
        i = q.popleft()
        tx, tz = i % W, i // W
        for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, nz = tx + dx, tz + dz
            if 0 <= nx < W and 0 <= nz < H:
                j = nz * W + nx
                if j in allowed and j not in reach and abs(stand(i) - stand(j)) <= kMaxStep:
                    reach.add(j); q.append(j)
    wall = op - reach
    n_peace = len(wall & peace_shore)
    return wall, n_peace, len(wall) - n_peace


def pack_bits(W, H, tiles):
    """Бит-пак 1 бит/тайл (tz*W+tx), порядок битов как `UAsdaWalkGrid::Bits`."""
    out = bytearray((W * H + 7) // 8)
    for i in tiles:
        out[i >> 3] |= 1 << (i & 7)
    return out


def unpack_bits(data, W, H):
    """Обратное к pack_bits: множество индексов тайлов."""
    return {i for i in range(W * H) if (data[i >> 3] >> (i & 7)) & 1}


def server_sim_name(map_name):
    """Имя `.sim` GameServer для клиентской карты (MapInfo-имя из реестра карт); None — карта без .sim."""
    reg = json.load(open(REGISTRY, encoding="utf-8"))
    for m in reg["maps"]:
        if m.get("folder") == map_name:
            nm = m["server"]["mapinfo_name"]
            return None if nm.startswith("^") else nm + ".sim"
    return None


def patch_sim(src, dst, opened, W=512, H=512, closed=None):
    """Копия `.sim` с бит 0 = 0 на открытых тайлах и бит 0 = 1 на закрытых (`closed`). Возвращает (снято, взведено),
    без `closed` — только число снятых (как прежде)."""
    b = bytearray(open(src, "rb").read())
    sw, sh = struct.unpack_from("<II", b, 0)
    if (sw, sh) != (W, H):
        raise ValueError("%s: размер %dx%d, ожидалось %dx%d" % (src, sw, sh, W, H))
    n = 0
    for i in opened:
        off = 8 + 4 * i                                   # row-major: (row=tz, col=tx) — как sub_419E50
        v = struct.unpack_from("<I", b, off)[0]
        if v & 1:
            struct.pack_into("<I", b, off, v & ~1)
            n += 1
    m = 0
    for i in (closed or ()):
        off = 8 + 4 * i
        v = struct.unpack_from("<I", b, off)[0]
        if not v & 1:
            struct.pack_into("<I", b, off, v | 1)
            m += 1
    open(dst, "wb").write(bytes(b))
    return (n, m) if closed is not None else n


def render_png(res, path):
    """Превью: суша серая, блок тёмный, открытая вода синяя, оставленная блоком вода тёмно-синяя, объект красный, лава оранжевая."""
    from PIL import Image
    W, H = res["W"], res["H"]
    im = Image.new("RGB", (W, H)); px = im.load()
    surf = res.get("surf", {}); walk = res["walk"]
    for i in range(W * H):
        c = (200, 200, 200) if walk[i] else (60, 60, 60)
        if i in surf and not walk[i]:
            c = (0, 0, 110)
        if i in res.get("lava_set", ()) and not walk[i]:
            c = (230, 110, 0)
        if i in res["open"]:
            c = (60, 140, 255)
        if i in res.get("objs", ()):
            c = (230, 0, 0)
        px[i % W, H - 1 - i // W] = c
    im.save(path)


def main(argv):
    import argparse
    ap = argparse.ArgumentParser(description="маска открытой воды (плавание) по картам")
    ap.add_argument("--maps", default="", help="карты через запятую (по умолчанию все)")
    ap.add_argument("--write", action="store_true", help="записать Tools/data/water_swim/<карта>.bits + summary.json")
    ap.add_argument("--png", default="", help="папка превью PNG")
    ap.add_argument("--make-sims", nargs=2, metavar=("SRC", "DST"), help="пропатчить .sim из SRC в DST")
    ap.add_argument("--make-npc-sims", nargs=2, metavar=("SRC", "DST"),
                    help="пропатчить .sim NpcServer (только мелководье, mob_open) из SRC в DST")
    a = ap.parse_args(argv)
    maps = [m.strip() for m in a.maps.split(",") if m.strip()] or _MAPS.ALL_MAPS
    summary = {}
    for mp in maps:
        res = compute(mp)
        if res is None:
            print("%-18s нет .TER" % mp); continue
        if mp in EXCLUDE:
            res["open"] = set()   # игроку вода закрыта целиком (мобы — без изменений)
            res["wall"] = set(); res["wall_peace_shore"] = res["wall_step"] = 0
            print("%-18s ИСКЛЮЧЕНА: %s" % (mp, EXCLUDE[mp]))
        s = {k: v for k, v in res.items() if k not in ("open", "mob_open", "mob_close", "surf", "objs", "walk", "lava_set", "wall")}
        s["opened"] = len(res["open"])
        s["wall"] = len(res.get("wall", ()))
        s["mob_opened"] = len(res.get("mob_open", ()))
        s["sim"] = server_sim_name(mp)
        if mp in EXCLUDE:
            s["excluded"] = EXCLUDE[mp]
        summary[mp] = s
        print("%-18s тел %2d (лава %d) мокрых-блок %6d лава %6d край %4d далеко %5d объекты %5d без выхода %5d берег %5d океан-срез %6d -> ОТКРЫТО %6d  стена %5d (кромка города %d, обрыв %d)  sim=%s"
              % (mp, s["bodies"], s["lava_bodies"], s["wet_blocked"], s["lava"], s["edge"], s["far"], s["objects"],
                 s["unreachable"], s["shore"], s["ocean_cut"], s["opened"], s["wall"], s["wall_peace_shore"], s["wall_step"], s["sim"]))
        if a.make_npc_sims and s["sim"] and (res.get("mob_open") or res.get("mob_close")):
            src_dir, dst_dir = a.make_npc_sims
            os.makedirs(dst_dir, exist_ok=True)
            files = {f.lower(): f for f in os.listdir(src_dir)}
            f = files.get(s["sim"].lower())
            if not f:
                print("   !!! нет %s в %s" % (s["sim"], src_dir))
            else:
                n, m = patch_sim(os.path.join(src_dir, f), os.path.join(dst_dir, f), res.get("mob_open", ()),
                                 closed=res.get("mob_close", set()))
                s["npc_sim_cleared"] = n
                s["npc_sim_closed"] = m
                print("   NpcServer %s: мобам открыто мелководье — снят бит 0 у %d клеток (из %d); глубокая вода закрыта — взведён у %d"
                      % (f, n, len(res.get("mob_open", ())), m))
        if a.write:
            os.makedirs(OUT_DIR, exist_ok=True)
            open(os.path.join(OUT_DIR, mp + ".bits"), "wb").write(bytes(pack_bits(res["W"], res["H"], res["open"])))
            # ★ 08.11.2026 [ИСПРАВЛЕНИЕ]: «стена» — отдельный файл (`DA_WalkGrid.SwimWallBits`); маска `.bits` = список, снятый с
            #   бита 0 `.sim` живого сервера 28.10, остаётся прежней. Пустая стена — файла нет (поле ассета очищается).
            wp = os.path.join(OUT_DIR, mp + ".walls.bits")
            if res.get("wall"):
                open(wp, "wb").write(bytes(pack_bits(res["W"], res["H"], res["wall"])))
            elif os.path.isfile(wp):
                os.remove(wp)
        if a.png and "walk" in res:
            os.makedirs(a.png, exist_ok=True)
            render_png(res, os.path.join(a.png, "water_%s.png" % mp.replace(" ", "_")))
        if a.make_sims and s["sim"] and res["open"]:
            src_dir, dst_dir = a.make_sims
            os.makedirs(dst_dir, exist_ok=True)
            files = {f.lower(): f for f in os.listdir(src_dir)}
            f = files.get(s["sim"].lower())
            if not f:
                print("   !!! нет %s в %s" % (s["sim"], src_dir))
            else:
                # ★ 08.11.2026: на сервер — открытое БЕЗ «стены» (паритет с клиентом при `asda.Swim.WallGuard 1`).
                n = patch_sim(os.path.join(src_dir, f), os.path.join(dst_dir, f), res["open"] - res.get("wall", set()))
                s["sim_cleared"] = n
                print("   %s: снят бит 0 у %d клеток" % (f, n))
    if a.write:
        json.dump(summary, open(os.path.join(OUT_DIR, "summary.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
