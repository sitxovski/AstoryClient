# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_verify_mesh_normals.py — ЧИСЛОВАЯ ПРОВЕРКА нормалей мешей карты (Д-2) и габаритов рендер-данных.
#   Ничего не меняет и не сохраняет. Два независимых метода (закон false-negatives-method):
#   (1) ПО ПОЗИЦИЯМ: нормаль каждой вершины рендер-данных LOD0 (C++ UAsdaMeshBuilder.get_static_mesh_lod0_vertices)
#       сравнивается с АВТОРСКОЙ нормалью .mapraw той же позиции (game→UE (-z,x,y), ключ = позиция с
#       округлением до 1e-3; при нескольких кандидатах в одной позиции — минимальный угол). Считаем долю
#       вершин с отклонением >30° и >60°, средний угол по модели, «инвертированные» модели (средний >90°).
#       Цель после правки: доля >30° ≈ 0.
#   (2) ОТ ГЕОМЕТРИИ (без трансформа): угол между нормалью и площадно-взвешенной геометрической нормалью
#       смежных треугольников — считается и для рендер-данных (по индексам буфера), и для источника
#       (по индексам .mapraw). До правки рендер-распределение ≈ 0 (UE пересчитал), после — должно совпасть
#       с распределением источника.
#   (3) Габариты рендер-данных ВСЕХ StaticMesh карты (Models + Terrain) — конечны ли (аналог check_huge).
#   (4) Террейн: доля вершин SM_terrain с вершинным цветом ≠ белому (Д-4) и число секций.
# Запуск (headless, скрыто): ASDA_MAP=alpen [ASDA_SAMPLE=N] python Tools/run_headless_safe.py Tools/asda_verify_mesh_normals.py --marker "[verify_normals] ГОТОВО"
import os as _os, sys as _sys, math, os
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import unreal
import asda_mapraw as _MR
import asda_maps as _MAPS

MAP = os.environ.get("ASDA_MAP", "alpen")
RAW = os.environ.get("ASDA_RAW", r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/maps")
SAMPLE = int(os.environ.get("ASDA_SAMPLE", "0"))        # >0 = только первые N моделей
ASSET = _MAPS.map_asset_name(MAP)
ROOT = "/Game/Map/" + ASSET
EAL = unreal.EditorAssetLibrary
MB = unreal.AsdaMeshBuilder


def log(m): unreal.log("[verify_normals] %s" % m)


def read_models(path):
    """Модели .mapraw (та же последовательность чтения, что в asda_build_map_level.read_mapraw)."""
    r, ver, name = _MR.open_mapraw(path)
    _MR.scene_fields(r)
    pc = r.u32(); ic = r.u32(); r.u16arr(ic)
    patches = []
    for _ in range(pc):
        vtx = r.u32()
        col = b""
        if vtx > 0:
            r.farr(vtx * 3); r.farr(vtx * 2); r.farr(vtx * 2); col = r.u8arr(vtx * 4)
        r.u32(); r.u32(); r.f32(); r.f32(); r.s(); r.f32(); r.s(); r.f32()
        patches.append(col)
    mc = r.u32(); models = []
    for _ in range(mc):
        nm = r.s(); tex = r.s(); vtx = r.u32()
        pos = r.farr(vtx * 3); nrm = r.farr(vtx * 3); r.farr(vtx * 2)
        n = r.u32(); idx = r.u32arr(n)
        models.append({"name": nm, "vtx": vtx, "pos": pos, "nrm": nrm, "idx": idx})
    return models, patches


def to_ue(x, y, z): return (-z, x, y)


def norm(v):
    l = math.sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2])
    return (v[0]/l, v[1]/l, v[2]/l) if l > 1e-6 else None


def angle(a, b):
    d = max(-1.0, min(1.0, a[0]*b[0] + a[1]*b[1] + a[2]*b[2]))
    return math.degrees(math.acos(d))


def geometric_normals(pos, idx):
    """Площадно-взвешенная геометрическая нормаль на вершину по треугольникам (pos: список кортежей)."""
    acc = [[0.0, 0.0, 0.0] for _ in pos]
    for t in range(0, len(idx) - 2, 3):
        a, b, c = idx[t], idx[t+1], idx[t+2]
        if a >= len(pos) or b >= len(pos) or c >= len(pos):
            continue
        pa, pb, pc_ = pos[a], pos[b], pos[c]
        u = (pb[0]-pa[0], pb[1]-pa[1], pb[2]-pa[2]); v = (pc_[0]-pa[0], pc_[1]-pa[1], pc_[2]-pa[2])
        # ★ Формула ДВИЖКА (StaticMeshOperations::ComputeTriangleTangentsAndNormals: (P1-P2)^(P0-P2)) = -(u^v):
        #   иначе на зеркальной конверсии осей (det C = -1) «геометрическая» нормаль смотрит внутрь (154° вместо 26°).
        n = (-(u[1]*v[2]-u[2]*v[1]), -(u[2]*v[0]-u[0]*v[2]), -(u[0]*v[1]-u[1]*v[0]))   # длина = 2·площадь
        for i in (a, b, c):
            acc[i][0] += n[0]; acc[i][1] += n[1]; acc[i][2] += n[2]
    return [norm(v) for v in acc]


_shape_logged = [False]


def _lod0(sm):
    """Обёртка над C++: Python-биндинг отдаёт кортеж out-параметров (форма зависит от сборки DLL:
    без индексов/цветов до релинка 15:16+, с ними — после). Разбираем ПО ТИПАМ: массивы по порядку =
    pos, nrm, [idx], [colors]; последний bool = from_render_data; ok = есть позиции."""
    r = MB.get_static_mesh_lod0_vertices(sm)
    if not isinstance(r, tuple):
        r = (r,)
    if not _shape_logged[0]:
        _shape_logged[0] = True
        log("форма возврата get_static_mesh_lod0_vertices: %s" % [type(x).__name__ for x in r])
    arrays = [x for x in r if isinstance(x, unreal.Array)]
    bools = [x for x in r if isinstance(x, bool)]
    pos = arrays[0] if len(arrays) > 0 else []
    nrm = arrays[1] if len(arrays) > 1 else []
    idx = arrays[2] if len(arrays) > 2 else []
    col = arrays[3] if len(arrays) > 3 else []
    from_rd = bools[-1] if bools else False
    return len(pos) > 0, pos, nrm, idx, col, from_rd


def _bounds_ok(sm):
    """Габариты рендер-данных конечны? Разбор кортежа по типам (bool = вердикт C++; иначе — по значениям)."""
    r = MB.get_static_mesh_render_bounds(sm)
    if not isinstance(r, tuple):
        r = (r,)
    bools = [x for x in r if isinstance(x, bool)]
    if bools:
        return bools[0]
    vals = []
    for x in r:
        if isinstance(x, unreal.Vector):
            vals += [x.x, x.y, x.z]
        elif isinstance(x, float):
            vals.append(x)
    return all(v == v and abs(v) < 1e30 for v in vals) and bool(vals)


def sm_names(models):
    """Имена ассетов в порядке моделей — та же логика уникализации, что build_mesh."""
    used = {}; out = []
    for i, m in enumerate(models):
        base = "SM_" + m["name"]; cnt = used.get(base, 0); used[base] = cnt + 1
        out.append(base if cnt == 0 else "%s_%d" % (base, i))
    return out


def main():
    raw = os.path.join(RAW, MAP + ".mapraw")
    models, patches = read_models(raw)
    names = sm_names(models)
    n_models = len(models) if SAMPLE <= 0 else min(SAMPLE, len(models))
    tot = 0; gt30 = 0; gt60 = 0; inverted = 0; no_nrm = 0; missing_asset = 0; compared_models = 0
    geo_render_sum = 0.0; geo_render_n = 0; geo_src_sum = 0.0; geo_src_n = 0
    geo_render_gt30 = 0; geo_src_gt30 = 0
    unmatched = 0; from_desc = 0; bad_bounds = []
    worst = []
    for i in range(n_models):
        m = models[i]; path = ROOT + "/Models/" + names[i]
        # авторские нормали источника → UE; нулевые = «нет в источнике»
        src_nrm = [norm(to_ue(*m["nrm"][v*3:v*3+3])) for v in range(m["vtx"])]
        has = sum(1 for n in src_nrm if n is not None)
        if has == 0:
            no_nrm += 1
        if not EAL.does_asset_exist(path):
            missing_asset += 1; continue
        sm = EAL.load_asset(path)
        ok, pos, nrm, idx, col, from_rd = _lod0(sm)
        if not ok or len(pos) == 0:
            missing_asset += 1; continue
        if not from_rd:
            from_desc += 1
        if not _bounds_ok(sm):
            bad_bounds.append(names[i])
        # (2) от геометрии — рендер
        rpos = [(p.x, p.y, p.z) for p in pos]
        rn = [norm((n.x, n.y, n.z)) for n in nrm]
        g = geometric_normals(rpos, list(idx))
        for k in range(len(rn)):
            if rn[k] and g[k]:
                a = angle(rn[k], g[k]); geo_render_sum += a; geo_render_n += 1
                if a > 30: geo_render_gt30 += 1
        if has == 0:
            continue
        # (2) от геометрии — источник (используемые вершины)
        spos = [to_ue(*m["pos"][v*3:v*3+3]) for v in range(m["vtx"])]
        gs = geometric_normals(spos, list(m["idx"]))
        used = set(m["idx"])
        for v in used:
            if v < m["vtx"] and src_nrm[v] and gs[v]:
                a = angle(src_nrm[v], gs[v]); geo_src_sum += a; geo_src_n += 1
                if a > 30: geo_src_gt30 += 1
        # (1) по позициям
        table = {}
        for v in used:
            if v < m["vtx"] and src_nrm[v]:
                key = (round(spos[v][0], 3), round(spos[v][1], 3), round(spos[v][2], 3))
                table.setdefault(key, []).append(src_nrm[v])
        msum = 0.0; mn = 0
        for k in range(len(rn)):
            key = (round(rpos[k][0], 3), round(rpos[k][1], 3), round(rpos[k][2], 3))
            cands = table.get(key)
            if not cands or not rn[k]:
                unmatched += 1; continue
            a = min(angle(rn[k], c) for c in cands)
            tot += 1; msum += a; mn += 1
            if a > 30: gt30 += 1
            if a > 60: gt60 += 1
        if mn:
            compared_models += 1
            mean = msum / mn
            if mean > 90: inverted += 1
            worst.append((mean, names[i], mn))
    worst.sort(reverse=True)
    log("КАРТА %s: моделей %d (проверено %d, без нормалей в источнике %d, ассета нет/пусто %d, из описания %d)"
        % (MAP, len(models), n_models, no_nrm, missing_asset, from_desc))
    log("(1) ПО ПОЗИЦИЯМ: сравнено вершин %d в %d моделях | >30°: %d (%.1f%%) | >60°: %d (%.1f%%) | инвертированных моделей (средний >90°): %d | несопоставлено %d"
        % (tot, compared_models, gt30, 100.0*gt30/tot if tot else 0, gt60, 100.0*gt60/tot if tot else 0, inverted, unmatched))
    log("(2) ОТ ГЕОМЕТРИИ: рендер — средний угол %.1f° (>30°: %.1f%% из %d) | источник — средний %.1f° (>30°: %.1f%% из %d)"
        % (geo_render_sum/geo_render_n if geo_render_n else 0, 100.0*geo_render_gt30/geo_render_n if geo_render_n else 0, geo_render_n,
           geo_src_sum/geo_src_n if geo_src_n else 0, 100.0*geo_src_gt30/geo_src_n if geo_src_n else 0, geo_src_n))
    log("   худшие по (1): %s" % ", ".join("%s %.0f°/%d" % (n, a, c) for a, n, c in worst[:8]))
    # (3) габариты всех SM карты
    checked = 0
    for folder in (ROOT + "/Models", ROOT + "/Terrain"):
        if not EAL.does_directory_exist(folder):
            continue
        for a in EAL.list_assets(folder, recursive=False):
            sm = EAL.load_asset(a)
            if not isinstance(sm, unreal.StaticMesh):
                continue
            checked += 1
            nm = a.rsplit("/", 1)[-1].split(".")[0]
            if not _bounds_ok(sm) and nm not in bad_bounds:
                bad_bounds.append(nm)
    log("(3) ГАБАРИТЫ рендер-данных: проверено %d StaticMesh, НЕконечных %d %s" % (checked, len(bad_bounds), bad_bounds[:10]))
    # (4) террейн: вершинный цвет
    tp = ROOT + "/Terrain/SM_terrain"
    if EAL.does_asset_exist(tp):
        sm = EAL.load_asset(tp)
        ok, pos, nrm, idx, col, from_rd = _lod0(sm)
        nonwhite = sum(1 for c in col if c.r < 0.996 or c.g < 0.996 or c.b < 0.996)
        src_nonwhite = 0; src_tot = 0
        for cb in patches:
            for v in range(len(cb) // 4):
                src_tot += 1
                if cb[v*4] < 255 or cb[v*4+1] < 255 or cb[v*4+2] < 255: src_nonwhite += 1
        log("(4) ТЕРРЕЙН: вершин %d, цветов в буфере %d, не-белых %d (%.1f%%) | источник: вершин %d, не-белых %d (%.1f%%) | секций %d"
            % (len(pos), len(col), nonwhite, 100.0*nonwhite/len(col) if col else 0, src_tot, src_nonwhite,
               100.0*src_nonwhite/src_tot if src_tot else 0, len(sm.get_editor_property("static_materials"))))
    else:
        log("(4) ТЕРРЕЙН: %s отсутствует" % tp)
    unreal.log("[verify_normals] ГОТОВО %s" % MAP)


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
