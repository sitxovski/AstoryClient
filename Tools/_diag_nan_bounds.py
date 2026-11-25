# Copyright (c) 2026 @sitxovski (Discord)
# _diag_nan_bounds.py — ДИАГНОСТИКА: почему у части StaticMesh габариты (Bounds) = NaN.
#   Печатает по каждому мешу: габариты, число LOD/вершин/треугольников, наличие NaN в позициях
#   вершин mesh-description, min/max по осям. Сравнивает «битые» (из списка лога кука) и «здоровые».
#   Ничего не меняет и не сохраняет. Запуск: headless, ASDA_NANLIST=<файл со списком пакетов>.
import os, unreal

EAL = unreal.EditorAssetLibrary
def log(m): unreal.log("[diag] " + str(m))

def isnan(v): return v != v
def bad(v):   return (v != v) or (abs(v) > 1e30)


def dump(pkg):
    """Полный структурный разбор одного StaticMesh: габариты + сырые вершины описания."""
    sm = unreal.load_asset(pkg)
    if not isinstance(sm, unreal.StaticMesh):
        log("%s: НЕ StaticMesh" % pkg); return
    out = [pkg]
    try:
        b = sm.get_bounds()
        out.append("bounds origin=(%r,%r,%r) extent=(%r,%r,%r) r=%r" % (
            b.origin.x, b.origin.y, b.origin.z,
            b.box_extent.x, b.box_extent.y, b.box_extent.z, b.sphere_radius))
    except Exception as e:
        out.append("bounds EXC %s" % e)
    for prop in ("positive_bounds_extension", "negative_bounds_extension"):
        try:
            v = sm.get_editor_property(prop)
            out.append("%s=(%r,%r,%r)" % (prop, v.x, v.y, v.z))
        except Exception as e:
            out.append("%s EXC %s" % (prop, e))
    try:
        nlod = sm.get_num_lods()
    except Exception:
        nlod = -1
    out.append("lods=%d" % nlod)
    for l in range(max(0, nlod)):
        try:
            out.append("  LOD%d verts=%d tris=%d" % (l, sm.get_num_vertices(l), sm.get_num_triangles(l)))
        except Exception as e:
            out.append("  LOD%d EXC %s" % (l, e))
    # разбор mesh-description (источник габаритов при fast build)
    try:
        d = sm.get_static_mesh_description(0)
    except Exception as e:
        d = None; out.append("desc EXC %s" % e)
    if d:
        try:
            vids = d.get_vertex_ids()
        except Exception:
            vids = None
        if vids is None:
            out.append("desc: get_vertex_ids отсутствует; dir=%s" % [x for x in dir(d) if 'vert' in x.lower()])
        else:
            nnan = 0; norph = 0
            mn = [1e38] * 3; mx = [-1e38] * 3
            for vid in vids:
                p = d.get_vertex_position(vid)
                c = (p.x, p.y, p.z)
                if any(isnan(x) for x in c) or any(abs(x) > 1e30 for x in c):
                    nnan += 1
                    if nnan <= 3: out.append("   NaN vert %r" % (c,))
                else:
                    for i in range(3):
                        if c[i] < mn[i]: mn[i] = c[i]
                        if c[i] > mx[i]: mx[i] = c[i]
                try:
                    if d.is_vertex_orphaned(vid): norph += 1
                except Exception:
                    pass
            out.append("desc: verts=%d nanVerts=%d orphan=%d min=%r max=%r" % (len(vids), nnan, norph, mn, mx))
    log(" | ".join(str(x) for x in out))


def main():
    lst = os.environ.get("ASDA_NANLIST", "")
    pkgs = []
    if lst and os.path.isfile(lst):
        pkgs = [l.strip() for l in open(lst, "r", encoding="utf-8") if l.strip()]
    n = int(os.environ.get("ASDA_N", "5"))
    log("=== БИТЫЕ (из лога кука) ===")
    for p in pkgs[:n]:
        dump(p.split(".")[0] if "." in p.split("/")[-1] else p)
    # здоровые для сравнения — из тех же папок, но не в списке
    bad_set = set()
    for p in pkgs:
        bad_set.add(p.split(".")[0] if "." in p.split("/")[-1] else p)
    log("=== ЗДОРОВЫЕ (та же папка, не в списке) ===")
    AR = unreal.AssetRegistryHelpers.get_asset_registry()
    shown = 0
    for ad in AR.get_assets_by_path(unreal.Name("/Game/Map/rein/Models"), recursive=True):
        pkg = str(ad.package_name)
        if pkg in bad_set: continue
        dump(pkg); shown += 1
        if shown >= 3: break


if __name__ == "__main__":
    main()
