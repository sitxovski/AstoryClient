# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""_probe_weapon_fx_panels.py — ЧИСЛЕННЫЙ КРИТЕРИЙ «полотно лежит ВДОЛЬ клинка».

Для каждого квад-эмиттера свечения считаем 4 МИРОВЫХ УГЛА по формуле рантайма порта
(ветка bStrictQuadAttach) и меряем относительно МИРОВОЙ ГЕОМЕТРИИ МЕША ОРУЖИЯ:
  * Δось  — расстояние центра полотна до оси клинка;
  * ∠норм — угол между НОРМАЛЬЮ полотна и осью клинка (90° = полотно СОДЕРЖИТ клинок, 0° = поперёк);
  * ∠длин — угол между ДЛИННОЙ стороной полотна и осью клинка (0/180° = вдоль).
Плохо = ∠норм далеко от 90° ИЛИ ∠длин далеко от 0/180 при вытянутом полотне.
"""
import sys, math
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

SKEL = "/Game/AsdaAssets/Models/Skeletal/Characters/ch1_m_cremmy_preview/SkeletalMeshes/ch1_m_cremmy_preview"
MESHSCALE = unreal.Vector(0.01, -0.01, 0.01)
REL_WEAPON = (0.0, 46.3, 0.0)
CASES = [
    ("bali_e_ra_ma_03", "ch1_n_90_bbow209_00_00_0", "8_a1"),   # ЖИВОЙ случай из лога
    ("ohs_a_co_ma_03",  "ch1_n_01_swrd108_00_00_0", "8_a1"),
    ("ohs_a_co_ma_01",  "ch1_n_00_swrd003_00_00_0", "8_a1"),   # ForceWeaponGlowGrade 5
    ("ths_a_co_ma_03",  "ch1_n_03_thnd001_00_00_0", "8_a1"),
    ("spr_a_co_ma_03",  "ch1_n_02_spir107_00_00_0", "8_a1"),
    ("bow_a_co_ma_03",  "ch1_n_90_lbow200_00_00_0", "_8_a1"),
    ("shd_a_co_ma_03",  "ch1_n_01_shid103_00_00_0", "_7_a1"),
]
D = math.pi / 180.0


def log(m): unreal.log_warning("[panels] %s" % m)
def m_mul(a, b): return [[sum(a[i][k]*b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]
def v_rot(v, m): return [sum(v[k]*m[k][j] for k in range(3)) for j in range(3)]
def v_xf(v, m): return [v_rot(v, m)[j] + m[3][j] for j in range(3)]
def sub(a, b): return [a[i]-b[i] for i in range(3)]
def dot(a, b): return sum(a[i]*b[i] for i in range(3))
def cross(a, b): return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]
def nrm(a):
    n = math.sqrt(dot(a, a)) or 1.0; return [c/n for c in a]
def ang(a, b): return math.degrees(math.acos(max(-1.0, min(1.0, dot(nrm(a), nrm(b))))))


def t_to_m(t):
    q = t.rotation; s = t.scale3d; l = t.translation
    x, y, z, w = q.x, q.y, q.z, q.w
    r = [[1-2*(y*y+z*z), 2*(x*y+z*w), 2*(x*z-y*w), 0],
         [2*(x*y-z*w), 1-2*(x*x+z*z), 2*(y*z+x*w), 0],
         [2*(x*z+y*w), 2*(y*z-x*w), 1-2*(x*x+y*y), 0], [0, 0, 0, 1]]
    for i in range(3):
        for j in range(3): r[i][j] *= (s.x, s.y, s.z)[i]
    r[3] = [l.x, l.y, l.z, 1]
    return r


def m_norm(m):
    o = [r[:] for r in m]
    for i in range(3):
        n = math.sqrt(sum(o[i][j]**2 for j in range(3))) or 1.0
        for j in range(3): o[i][j] /= n
    o[3] = [0, 0, 0, 1]
    return o


# ---- вращения РОВНО как в AsdaEffectActor::BuildMesh ----
def RX(P, A): c, s = math.cos(A), math.sin(A); return (P[0], P[1]*c+P[2]*s, -P[1]*s+P[2]*c)
def RY(P, A): c, s = math.cos(A), math.sin(A); return (P[0]*c-P[2]*s, P[1], P[0]*s+P[2]*c)
def RZ(P, A): c, s = math.cos(A), math.sin(A); return (P[0]*c+P[1]*s, -P[0]*s+P[1]*c, P[2])
def RXd(P, A): c, s = math.cos(A), math.sin(A); return (P[0], P[1]*c-P[2]*s, P[1]*s+P[2]*c)
def RYd(P, A): c, s = math.cos(A), math.sin(A); return (P[0]*c+P[2]*s, P[1], -P[0]*s+P[2]*c)
def RZd(P, A): c, s = math.cos(A), math.sin(A); return (P[0]*c-P[1]*s, P[0]*s+P[1]*c, P[2])


def main():
    sk = unreal.EditorAssetLibrary.load_asset(SKEL)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    try: les.new_level("/Game/Temp/_panels_probe")
    except Exception: pass
    act = eas.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(0, 0, 0))
    comp = act.skeletal_mesh_component
    comp.set_skeletal_mesh_asset(sk)
    comp.set_world_scale3d(MESHSCALE)
    CompW = t_to_m(comp.get_world_transform())
    FxToBone = [[0, 100, 0, 0], [100, 0, 0, 0], [0, 0, 100, 0], [0, 0, 0, 1]]

    for eff, mesh, bone in CASES:
        log("=" * 118)
        try:
            Tc = comp.get_socket_transform(unreal.Name(bone), unreal.RelativeTransformSpace.RTS_COMPONENT)
        except Exception as e:
            log("%s: кость %s — %s" % (eff, bone, e)); continue
        S = m_mul(t_to_m(Tc), CompW)
        A = m_mul(FxToBone, S)
        Q = m_norm(A); Loc = [A[3][0], A[3][1], A[3][2]]
        # --- геометрия меша оружия в мире ---
        sm = unreal.EditorAssetLibrary.load_asset("/Game/AsdaAssets/Models/Char/%s/StaticMeshes/%s" % (mesh, mesh))
        if not sm:
            log("%s: НЕТ меша %s" % (eff, mesh)); continue
        try: verts = unreal.ProceduralMeshLibrary.get_section_from_static_mesh(sm, 0, 0)[0]
        except Exception as e:
            log("%s: вершины недоступны (%s)" % (eff, e)); continue
        W = [v_xf([v.x+REL_WEAPON[0], v.y+REL_WEAPON[1], v.z+REL_WEAPON[2]], S) for v in verts]
        n = len(W); C0 = [sum(p[i] for p in W)/n for i in range(3)]
        ax = [1.0, 0.0, 0.0]
        for _ in range(80):
            acc = [0.0]*3
            for p in W:
                d = sub(p, C0); s2 = dot(d, ax)
                for i in range(3): acc[i] += s2*d[i]
            ax = nrm(acc)
        proj = [dot(sub(p, C0), ax) for p in W]
        lo, hi = min(proj), max(proj)
        # радиус клинка (макс. перпендикулярное отклонение вершин)
        rad = max(math.sqrt(max(dot(sub(p, C0), sub(p, C0)) - dot(sub(p, C0), ax)**2, 0.0)) for p in W)
        log("%s | меш %s: вершин=%d длина=%.1f радиус=%.1f ось=(%.3f,%.3f,%.3f)"
            % (eff, mesh, n, hi-lo, rad, ax[0], ax[1], ax[2]))

        def dist_ax(P):
            d = sub(P, C0); s2 = max(min(dot(d, ax), hi), lo)
            return math.sqrt(max(dot(d, d) - 2*s2*dot(d, ax) + s2*s2, 0.0))

        bp = unreal.EditorAssetLibrary.load_asset("/Game/AsdaAssets/Effects/BP_Fx_%s" % eff.replace("-", "_"))
        if not bp: log("  НЕТ BP"); continue
        cdo = unreal.get_default_object(bp.generated_class())
        for em in cdo.get_editor_property("emitters"):
            if not em.get_editor_property("single_quad"): continue
            nm = str(em.get_editor_property("name")); t = em.get_editor_property("jeb_type")
            bm = em.get_editor_property("billboard_mode")
            nds = em.get_editor_property("nodes")
            if not nds: continue
            n0 = nds[0]
            SXv = max(n0.get_editor_property("size_x"), 1.0); SYv = max(n0.get_editor_property("size_y"), 1.0)
            roll = n0.get_editor_property("ring_roll"); rt = n0.get_editor_property("ring_rot")
            po = n0.get_editor_property("ring_pos_ofs")
            corners = [(-SXv, 0, -SYv), (SXv, 0, -SYv), (SXv, 0, SYv), (-SXv, 0, SYv)]
            Cc = RZd(RYd(RXd((po.x, po.y, po.z), rt.x*D), rt.y*D), rt.z*D)
            Wc = []
            for g0 in corners:
                G = RY(g0, roll*D)
                G = RZ(RY(RX(G, rt.x*D), rt.y*D), rt.z*D)
                G = (G[0]+Cc[0], G[1]+Cc[1], G[2]+Cc[2])
                UE = [-G[2], G[0], G[1]]
                Wc.append([v_rot(UE, Q)[k] + Loc[k] for k in range(3)])
            cen = [sum(p[i] for p in Wc)/4.0 for i in range(3)]
            e1 = sub(Wc[1], Wc[0]); e2 = sub(Wc[3], Wc[0])
            nvec = cross(e1, e2)
            l1, l2 = math.sqrt(dot(e1, e1)), math.sqrt(dot(e2, e2))
            longe = e1 if l1 >= l2 else e2
            an = ang(nvec, ax); al = ang(longe, ax); al = min(al, 180.0-al)
            verdict = "OK" if (abs(an-90.0) <= 15.0) else "!!ПОПЕРЁК!!"
            log("  %-15s тип=0x%X bb=%d размер=(%.0fx%.0f) roll=%.0f rot=(%.0f,%.0f,%.0f) pos=(%.1f,%.1f,%.1f) | "
                "центр=(%.1f,%.1f,%.1f) Δось=%.1f ∠норм=%.1f° ∠длин=%.1f° %s"
                % (nm, t, bm, SXv, SYv, roll, rt.x, rt.y, rt.z, po.x, po.y, po.z,
                   cen[0], cen[1], cen[2], dist_ax(cen), an, al, verdict))
    log("ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
