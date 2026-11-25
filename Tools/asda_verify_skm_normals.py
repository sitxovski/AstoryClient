# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_verify_skm_normals.py — ПРОВЕРКА ПО ФАКТУ: сохраняет ли импорт glTF (Interchange) авторские нормали
#   SkeletalMesh персонажей/NPC/мобов (аудит света 2026-09-03 §4.3/§8 не смог прочитать свойство пайплайна).
#   Ничего не меняет и не сохраняет. Два независимых метода:
#   (1) `FSkeletalMeshLODInfo::BuildSettings.bRecomputeNormals` ассета (из Python не читается — C++
#       UAsdaMeshBuilder.get_skeletal_mesh_lod0_vertices);
#   (2) распределение угла «нормаль vs геометрическая (площадно-взвешенная)» у рендер-данных LOD0 и у glb-источника
#       (NORMAL-аксессор). Не зависит от осей/масштаба/зеркала импорта: если нормали импортированы — распределения
#       совпадают; если пересчитаны — рендер даёт ≈0° при заметных отклонениях у источника.
# Запуск (headless, скрыто): [ASDA_SKM="путь/ассета||..."] python Tools/run_headless_safe.py Tools/asda_verify_skm_normals.py --marker "[verify_skm] ГОТОВО"
import os as _os, sys as _sys, os, json, struct, math
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import unreal

import asda_paths as _P   # ★ гигиена 15.09.2026 (§4 п.2): корни путей — Tools/asda_paths.py (ASDA_ROOT и др.)
RAW_ROOT = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Models", "Skeletal")
DEFAULT = [  # (glb, ассет SkeletalMesh — Interchange кладёт в <модель>/SkeletalMeshes/<модель>)
    ("Characters/ch1_f_cremmy_preview.glb", "/Game/AsdaAssets/Models/Char/ch1_f_cremmy_preview/SkeletalMeshes/ch1_f_cremmy_preview"),
    ("NPC/mdel_bsophia-a_00.glb", "/Game/AsdaAssets/Models/NPC/mdel_bsophia-a_00/SkeletalMeshes/mdel_bsophia-a_00"),
    ("NPC/mdel_alpencart-a_00.glb", "/Game/AsdaAssets/Models/NPC/mdel_alpencart-a_00/SkeletalMeshes/mdel_alpencart-a_00"),
]
EAL = unreal.EditorAssetLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()
MB = unreal.AsdaMeshBuilder


def log(m): unreal.log("[verify_skm] %s" % m)


def norm(v):
    l = math.sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2])
    return (v[0]/l, v[1]/l, v[2]/l) if l > 1e-6 else None


def angle(a, b):
    d = max(-1.0, min(1.0, a[0]*b[0] + a[1]*b[1] + a[2]*b[2]))
    return math.degrees(math.acos(d))


def geo_stats(pos, nrm, idx, sign=-1.0):
    """Средний угол и доля >30° между нормалью вершины и геометрической нормалью (по треугольникам)."""
    acc = [[0.0, 0.0, 0.0] for _ in pos]
    for t in range(0, len(idx) - 2, 3):
        a, b, c = idx[t], idx[t+1], idx[t+2]
        if a >= len(pos) or b >= len(pos) or c >= len(pos):
            continue
        pa, pb, pc_ = pos[a], pos[b], pos[c]
        u = (pb[0]-pa[0], pb[1]-pa[1], pb[2]-pa[2]); v = (pc_[0]-pa[0], pc_[1]-pa[1], pc_[2]-pa[2])
        # ★ Формула ДВИЖКА (StaticMeshOperations::ComputeTriangleTangentsAndNormals: (P1-P2)^(P0-P2)) = -(u^v):
        #   иначе на зеркальной конверсии осей (det C = -1) «геометрическая» нормаль смотрит внутрь (154° вместо 26°).
        #   glb (правая система, CCW): sign=+1 — обычное u^v.
        n = (sign*(u[1]*v[2]-u[2]*v[1]), sign*(u[2]*v[0]-u[0]*v[2]), sign*(u[0]*v[1]-u[1]*v[0]))
        for i in (a, b, c):
            acc[i][0] += n[0]; acc[i][1] += n[1]; acc[i][2] += n[2]
    s = 0.0; n_ = 0; gt30 = 0; gt60 = 0
    for k in range(len(pos)):
        g = norm(acc[k]); nn = nrm[k]
        if g and nn:
            a = angle(nn, g); s += a; n_ += 1
            if a > 30: gt30 += 1
            if a > 60: gt60 += 1
    return (s / n_ if n_ else 0.0, 100.0 * gt30 / n_ if n_ else 0.0, 100.0 * gt60 / n_ if n_ else 0.0, n_)


def read_glb(path):
    """Позиции/нормали/индексы всех примитивов glb (склейка со смещением индексов)."""
    d = open(path, "rb").read()
    assert d[:4] == b"glTF"
    off = 12; js = None; bin_ = None
    while off < len(d):
        ln, typ = struct.unpack_from("<II", d, off); off += 8
        chunk = d[off:off+ln]; off += ln
        if typ == 0x4E4F534A: js = json.loads(chunk.decode("utf-8"))
        elif typ == 0x004E4942: bin_ = chunk
    def acc(i):
        a = js["accessors"][i]; bv = js["bufferViews"][a["bufferView"]]
        base = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
        comp = {5126: ("f", 4), 5125: ("I", 4), 5123: ("H", 2), 5121: ("B", 1)}[a["componentType"]]
        ncomp = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]]
        cnt = a["count"]; stride = bv.get("byteStride", comp[1] * ncomp)
        out = []
        for k in range(cnt):
            out.append(struct.unpack_from("<%d%s" % (ncomp, comp[0]), bin_, base + k * stride))
        return out
    pos = []; nrm = []; idx = []
    for mesh in js["meshes"]:
        for p in mesh["primitives"]:
            base = len(pos)
            P = acc(p["attributes"]["POSITION"]); N = acc(p["attributes"]["NORMAL"]) if "NORMAL" in p["attributes"] else [(0, 0, 0)] * len(P)
            pos.extend(P); nrm.extend(norm(n) for n in N)
            if "indices" in p:
                idx.extend(i[0] + base for i in acc(p["indices"]))
    return pos, nrm, idx


def main():
    pairs = DEFAULT
    env = os.environ.get("ASDA_SKM", "")
    if env:
        pairs = [tuple(x.split("=>")) for x in env.split("||")]
    for glb_rel, asset in pairs:
        glb = os.path.join(RAW_ROOT, glb_rel)
        if not os.path.exists(glb):
            log("нет glb %s" % glb); continue
        # ассет: указанный путь или первый SkeletalMesh в папке с этим именем
        skm = None
        if EAL.does_asset_exist(asset):
            skm = EAL.load_asset(asset)
        if not isinstance(skm, unreal.SkeletalMesh):
            skm = None
            folder = asset.rsplit("/", 1)[0]                     # папка модели → первый SkeletalMesh внутри
            for ad in AR.get_assets_by_path(folder, recursive=True):
                if str(ad.asset_class_path.asset_name) == "SkeletalMesh":
                    skm = EAL.load_asset(str(ad.package_name)); break
        if skm is None:
            log("нет SkeletalMesh для %s" % asset); continue
        r = MB.get_skeletal_mesh_lod0_vertices(skm)
        r = r if isinstance(r, tuple) else (r,)
        arrays = [x for x in r if isinstance(x, unreal.Array)]; bools = [x for x in r if isinstance(x, bool)]
        pos = arrays[0] if arrays else []; nrm = arrays[1] if len(arrays) > 1 else []; idx = arrays[2] if len(arrays) > 2 else []
        recompute = bools[-1] if bools else None      # последний bool = BuildSettings.bRecomputeNormals
        if not pos:
            log("%s: рендер-данные недоступны (%s)" % (skm.get_name(), [type(x).__name__ for x in r])); continue
        if not idx:
            log("%s: сборка DLL без индексов — метод (2) недоступен, только флаг bRecomputeNormals=%s" % (skm.get_name(), recompute)); continue
        rp = [(p.x, p.y, p.z) for p in pos]; rn = [norm((n.x, n.y, n.z)) for n in nrm]
        r_mean, r30, r60, r_n = geo_stats(rp, rn, list(idx))
        gp, gn, gi = read_glb(glb)
        g_mean, g30, g60, g_n = geo_stats(gp, gn, gi, sign=+1.0)
        verdict = "ИМПОРТИРОВАНЫ (распределения совпадают)" if abs(r_mean - g_mean) < 2.0 and abs(r30 - g30) < 3.0 \
            else ("ПЕРЕСЧИТАНЫ UE (рендер ≈ геометрия, источник отклоняется)" if r_mean + 2.0 < g_mean else "РАСХОЖДЕНИЕ — смотреть руками")
        log("%s: BuildSettings.bRecomputeNormals=%s | рендер LOD0: вершин %d, угол к геометрии средний %.1f°, >30° %.1f%%, >60° %.1f%% | "
            "glb: вершин %d, средний %.1f°, >30° %.1f%%, >60° %.1f%% → %s"
            % (skm.get_name(), recompute, r_n, r_mean, r30, r60, g_n, g_mean, g30, g60, verdict))
    unreal.log("[verify_skm] ГОТОВО")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
