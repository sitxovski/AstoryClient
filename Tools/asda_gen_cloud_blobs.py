#!/usr/bin/env python3
# Copyright (c) 2026 @sitxovski (Discord)
# asda_gen_cloud_blobs.py — ЦЕЛЬНЫЕ меши облаков (просьба юзера: «единые объекты, без очертаний окружностей»):
# метабол-поле из 5-8 долей → marching cubes → одна гладкая замкнутая поверхность без внутренних стенок
# (транслюцент чистый) → GLB (сферическая UV, сглаженные нормали) → импорт Interchange (проверенный путь).
# Выход: export/cloud_blobs/cloudblob_{a..d}.glb. Запуск: python asda_gen_cloud_blobs.py
import numpy as np, random, math, os
from skimage import measure
import pygltflib as g

OUT = r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/cloud_blobs"

def gen_blob(seed):
    # МУЛЬТЯШНЫЙ КУЧЕВОЙ (фидбек юзера: не «гладко-овальные лепестки»): круглая грудка из крупных долей
    # + отчётливые ШИШКИ сверху + мелкие бока; резкий металл-фаллофф — бугры читаются, не сливаются в блин.
    rnd = random.Random(seed)
    lobes = []
    # 1) грудка: 3-4 крупные доли нерегулярным кластером
    nb = rnd.randint(3, 4)
    for i in range(nb):
        ang = rnd.uniform(0, 2*math.pi)
        dist = rnd.uniform(0.0, 0.45)
        r = rnd.uniform(0.55, 0.75)
        lobes.append((math.cos(ang)*dist*1.6, math.sin(ang)*dist, r*0.15, r, rnd.uniform(0.85, 1.0), rnd.uniform(0.6, 0.72)))
    # 2) шишки сверху: 4-6 средних, сидят на грудке ВЫШЕ
    nt = rnd.randint(4, 6)
    for i in range(nt):
        ang = rnd.uniform(0, 2*math.pi)
        dist = rnd.uniform(0.15, 0.6)
        r = rnd.uniform(0.28, 0.45)
        lobes.append((math.cos(ang)*dist*1.5, math.sin(ang)*dist*0.8, rnd.uniform(0.35, 0.6), r, rnd.uniform(0.85, 1.0), rnd.uniform(0.8, 1.0)))
    # 3) мелкие боковые пузыри: 2-4 (ближе к телу — не отрываются)
    ns = rnd.randint(2, 4)
    for i in range(ns):
        ang = rnd.uniform(0, 2*math.pi)
        dist = rnd.uniform(0.45, 0.7)
        r = rnd.uniform(0.22, 0.34)
        lobes.append((math.cos(ang)*dist*1.4, math.sin(ang)*dist*0.85, rnd.uniform(0.05, 0.25), r, 1.0, rnd.uniform(0.7, 0.9)))
    # поле метаболов на сетке
    N = 96
    xs = np.linspace(-2.0, 2.0, N); ys = np.linspace(-1.4, 1.4, N); zs = np.linspace(-0.55, 1.5, N)
    X, Y, Z = np.meshgrid(xs, ys, zs, indexing="ij")
    F = np.zeros_like(X)
    for cx, cy, cz, r, sy, sz in lobes:
        d2 = ((X - cx) / r) ** 2 + ((Y - cy) / (r * sy)) ** 2 + ((Z - cz) / (r * sz)) ** 2
        F += np.exp(-d2 * 2.2)                       # бугры читаются, но тело единое
    F -= 0.40
    # плоское дно
    floor = -0.05
    F = np.where(Z < floor, F - (floor - Z) * 14.0, F)   # жёсткое плоское дно
    verts, faces, normals, _ = measure.marching_cubes(F, level=0.0,
        spacing=(xs[1]-xs[0], ys[1]-ys[0], zs[1]-zs[0]))
    verts += np.array([xs[0], ys[0], zs[0]])
    # ЕДИНЫЙ ОБЪЕКТ: оставить крупнейшую связную компоненту (metaball-острова отрезать)
    parent = np.arange(len(verts))
    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]; a = parent[a]
        return a
    for tri in faces:
        a, b, c2 = int(tri[0]), int(tri[1]), int(tri[2])
        ra, rb, rc = find(a), find(b), find(c2)
        parent[rb] = ra; parent[rc] = ra
    roots = np.array([find(int(i)) for i in range(len(verts))])
    vals, counts = np.unique(roots, return_counts=True)
    main = vals[np.argmax(counts)]
    keep_faces = np.array([t for t in faces if find(int(t[0])) == main])
    used = np.unique(keep_faces)
    remap = -np.ones(len(verts), np.int64); remap[used] = np.arange(len(used))
    verts = verts[used]; normals = normals[used]
    faces = remap[keep_faces]
    # мировые юниты: доли ~0.5-0.7 → облако длиной ~4.4 ед. ×2500 = ~11000 юнитов при scale 1
    verts *= 25.0    # ГРАБЛЯ Interchange: glTF трактуется как МЕТРЫ и умножается на 100 → экспортируем /100
    # сферическая UV (для тангентов/совместимости)
    c = verts.mean(axis=0); d = verts - c
    u = 0.5 + np.arctan2(d[:,1], d[:,0]) / (2*np.pi)
    v = 0.5 - np.arcsin(np.clip(d[:,2] / (np.linalg.norm(d, axis=1) + 1e-6), -1, 1)) / np.pi
    uv = np.stack([u, v], axis=1)
    return verts.astype(np.float32), faces.astype(np.uint32), normals.astype(np.float32), uv.astype(np.float32)

def to_glb(verts, faces, normals, uv, path):
    # glTF RH Y-up: (x,y,z)_наши(Z-up) → (x,z,-y); нормали так же; winding flip
    P = np.stack([verts[:,0], verts[:,2], -verts[:,1]], axis=1).astype(np.float32)
    Nn = np.stack([normals[:,0], normals[:,2], -normals[:,1]], axis=1).astype(np.float32)
    Nn /= (np.linalg.norm(Nn, axis=1, keepdims=True) + 1e-8)
    I = faces[:, ::-1].astype(np.uint32).reshape(-1)
    bins = bytearray(); views = []; accs = []
    def add(data, target, ctype, atype, count, mm=False):
        off = len(bins); bins.extend(data)
        while len(bins) % 4: bins.append(0)
        views.append(g.BufferView(buffer=0, byteOffset=off, byteLength=len(data), target=target))
        a = g.Accessor(bufferView=len(views)-1, componentType=ctype, count=count, type=atype)
        if mm:
            arr = np.frombuffer(data, np.float32).reshape(-1, 3)
            a.min = arr.min(0).tolist(); a.max = arr.max(0).tolist()
        accs.append(a); return len(accs)-1
    aP = add(P.tobytes(), g.ARRAY_BUFFER, g.FLOAT, g.VEC3, len(P), mm=True)
    aN = add(Nn.tobytes(), g.ARRAY_BUFFER, g.FLOAT, g.VEC3, len(Nn))
    aU = add(uv.astype(np.float32).tobytes(), g.ARRAY_BUFFER, g.FLOAT, g.VEC2, len(uv))
    aI = add(I.tobytes(), g.ELEMENT_ARRAY_BUFFER, g.UNSIGNED_INT, g.SCALAR, len(I))
    gl = g.GLTF2(scene=0, scenes=[g.Scene(nodes=[0])], nodes=[g.Node(mesh=0, name=os.path.basename(path))],
                 meshes=[g.Mesh(primitives=[g.Primitive(attributes=g.Attributes(POSITION=aP, NORMAL=aN, TEXCOORD_0=aU), indices=aI)])],
                 accessors=accs, bufferViews=views, buffers=[g.Buffer(byteLength=len(bins))])
    gl.set_binary_blob(bytes(bins))
    gl.save_binary(path)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global seed, uv, v
    os.makedirs(OUT, exist_ok=True)
    for name, seed in (("a", 11), ("b", 47), ("c", 83), ("d", 129)):
        v, f, n, uv = gen_blob(seed)
        p = os.path.join(OUT, "cloudblob_%s.glb" % name)
        to_glb(v, f, n, uv, p)
        print("OK %s: %d вершин, %d треугольников" % (p, len(v), len(f)))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
