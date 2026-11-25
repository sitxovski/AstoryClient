# Copyright (c) 2026 @sitxovski (Discord)
# _probe_weapon_glow_attach.py — ЗАМЕР посадки эффектов свечения оружия относительно МЕША ОРУЖИЯ.
#
# Что меряем (в СЫРЫХ клиентских единицах = единицах, в которых авторены и меш оружия, и .jeb):
#   * габариты статик-меша оружия из ПОРТА (обратная конверсия UE→raw: raw=(X/100, Z/100, -Y/100));
#   * центр каждого квад-эмиттера BP_Fx по ФОРМУЛЕ РАНТАЙМА (AsdaEffectActor::BuildMesh, ветка
#     single-quad) — с флипом bAttachedQuad(RotY π) и без него;
#   * позиции частичных эмиттеров (PosBase — флипу не подвержены).
# Эталон: эффект живёт в матрице кости 8_a1, ровно как и модель оружия (entity_char.c:14721/28482,
#   sub_93DE55 сажает модель оружия на ТУ ЖЕ кость) ⇒ координаты .jeb обязаны совпадать с
#   координатами меша оружия 1:1.
# Запуск: Tools/run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\_probe_weapon_glow_attach.py
import sys, os, math, struct
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import unreal

DATA_DIR = r"@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Data\Character"
WEAPON_BASE = {0: "ohs", 1: "ohs", 2: "spr", 3: "ths", 5: "stf", 8: "bow", 9: "xbow", 10: "bow", 11: "bali"}

# (эффект, itemId-пример, меш оружия) — по одному представителю на базу
CASES = [
    ("ohs_a_co_ma_03", 23020, "ch1_n_01_swrd108_00_00_0"),
    ("ths_a_co_ma_03", 23108, "ch1_n_03_thnd001_00_00_0"),
    ("spr_a_co_ma_03", 23022, "ch1_n_02_spir107_00_00_0"),
    ("bow_a_co_ma_03", 24098, "ch1_n_90_lbow200_00_00_0"),
    ("shd_a_co_ma_03", 23064, "ch1_n_01_shid103_00_00_0"),
]


def log(m): unreal.log_warning("[замер-заточка] %s" % m)


def rx(p, a):
    c, s = math.cos(a), math.sin(a); return (p[0], p[1]*c + p[2]*s, -p[1]*s + p[2]*c)
def ry(p, a):
    c, s = math.cos(a), math.sin(a); return (p[0]*c - p[2]*s, p[1], p[0]*s + p[2]*c)
def rz(p, a):
    c, s = math.cos(a), math.sin(a); return (p[0]*c + p[1]*s, -p[0]*s + p[1]*c, p[2])


def quad_center(pos_ofs, roll, rot, holder, flip):
    """ИСТОРИЧЕСКАЯ формула рантайма (Corners=0): G=PosOfs → RY(roll) → RX/RY/RZ(rot, транспонированные) →
       [RY(pi) если attached] → RY(holder)."""
    d = math.pi / 180.0
    g = tuple(pos_ofs)
    g = ry(g, roll * d)
    g = rx(g, rot[0] * d); g = ry(g, rot[1] * d); g = rz(g, rot[2] * d)
    if flip: g = ry(g, math.pi)
    g = ry(g, holder * d)
    return g


# ★ ЭТАЛОННЫЕ D3DX row-vector ротации (дизасм 0xFF4ADD/0xFF4BDA/0xFF4CED)
def rxd(p, a):
    c, s = math.cos(a), math.sin(a); return (p[0], p[1]*c - p[2]*s, p[1]*s + p[2]*c)
def ryd(p, a):
    c, s = math.cos(a), math.sin(a); return (p[0]*c + p[2]*s, p[1], -p[0]*s + p[2]*c)
def rzd(p, a):
    c, s = math.cos(a), math.sin(a); return (p[0]*c - p[1]*s, p[0]*s + p[1]*c, p[2])


def quad_center_strict(pos_ofs, rot, holder):
    """bStrictQuadAttach: центр = posChan · RotX·RotY·RotZ (D3DX) · N48(holder); roll/π НЕ участвуют."""
    d = math.pi / 180.0
    g = rzd(ryd(rxd(tuple(pos_ofs), rot[0]*d), rot[1]*d), rot[2]*d)
    return ryd(g, holder * d)


def mesh_raw_bounds(mesh_name):
    p = "/Game/AsdaAssets/Models/Char/%s/StaticMeshes/%s" % (mesh_name, mesh_name)
    a = unreal.EditorAssetLibrary.load_asset(p)
    if not a: return None
    b = a.get_bounds()   # BoxSphereBounds
    o, e = b.box_extent, b.origin
    mn = (e.x - o.x, e.y - o.y, e.z - o.z)
    mx = (e.x + o.x, e.y + o.y, e.z + o.z)
    # ★ Обратная конверсия UE-меш → сырые клиентские: конвертер даёт UE=(x, -z, y)*100.
    #   Смещение компонента в порте: relative (0, +46.3, 0) в единицах скелета = raw z −0.463.
    raw = lambda v: (v[0] / 100.0, v[2] / 100.0, -v[1] / 100.0)
    a1, a2 = raw(mn), raw(mx)
    lo = tuple(min(a1[i], a2[i]) for i in range(3))
    hi = tuple(max(a1[i], a2[i]) for i in range(3))
    return lo, hi


def main():
    for eff, item_id, mesh in CASES:
        bp_path = "/Game/AsdaAssets/Effects/BP_Fx_%s" % eff.replace("-", "_")
        bp = unreal.EditorAssetLibrary.load_asset(bp_path)
        if not bp:
            log("НЕТ BP: %s" % bp_path); continue
        cdo = unreal.get_default_object(bp.generated_class())
        ems = cdo.get_editor_property("emitters")
        mb = mesh_raw_bounds(mesh)
        log("=" * 100)
        if mb:
            lo, hi = mb
            log("%s  меш=%s  СЫРЫЕ габариты: x[%.1f..%.1f] y[%.1f..%.1f] z[%.1f..%.1f] (клинок вдоль +z)"
                % (eff, mesh, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]))
        else:
            log("%s  меш=%s — статик-меш не найден" % (eff, mesh))
        for em in ems:
            t = em.get_editor_property("jeb_type")
            nm = em.get_editor_property("name")
            nodes = em.get_editor_property("nodes")
            single = em.get_editor_property("single_quad")
            bmode = em.get_editor_property("billboard_mode")
            if not nodes: continue
            n0 = nodes[0]
            po = n0.get_editor_property("ring_pos_ofs")
            bp_ = n0.get_editor_property("ring_base_pos")
            rot = n0.get_editor_property("ring_rot")
            roll = n0.get_editor_property("ring_roll")
            if single:
                cf = quad_center((po.x, po.y, po.z), roll, (rot.x, rot.y, rot.z), 0.0, True)
                cn = quad_center((po.x, po.y, po.z), roll, (rot.x, rot.y, rot.z), 0.0, False)
                # bStrictQuadAttach: pos-канал крутят ТОЛЬКО rot-каналы (D3DX), roll — геометрию, без π
                cr = quad_center_strict((po.x, po.y, po.z), (rot.x, rot.y, rot.z), 0.0)
                log("  квад %-14s тип=0x%X bb=%d  pos=(%.1f,%.1f,%.1f) basePos=(%.1f,%.1f,%.1f) "
                    "размер=(%.0f,%.0f) roll=%.0f rot=(%.0f,%.0f,%.0f) → БЫЛО(roll+π)=(%.1f,%.1f,%.1f)  "
                    "[roll без π]=(%.1f,%.1f,%.1f)  СТАЛО(bStrictQuadAttach)=(%.1f,%.1f,%.1f)"
                    % (nm, t, bmode, po.x, po.y, po.z, bp_.x, bp_.y, bp_.z,
                       n0.get_editor_property("size_x"), n0.get_editor_property("size_y"),
                       roll, rot.x, rot.y, rot.z,
                       cf[0], cf[1], cf[2], cn[0], cn[1], cn[2], cr[0], cr[1], cr[2]))
            else:
                pbu = em.get_editor_property("pos_base")     # UE-оси: (-z, x, y)
                log("  части %-14s тип=0x%X  posBase(fx)=(%.1f,%.1f,%.1f)  (частицы флипу НЕ подвержены)"
                    % (nm, t, pbu.y, pbu.z, -pbu.x))
    log("ГОТОВО")


if __name__ == "__main__":
    main()
