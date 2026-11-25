# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""_probe_weapon_fx_world.py — МИРОВОЙ замер: где эмиттеры свечения оружия относительно
   МИРОВОЙ ГЕОМЕТРИИ МЕША ОРУЖИЯ (вершины клинка).

Воспроизводит РОВНО цепочку порта:
  * меш оружия:  v_world = ( v_asset + Rel(0,46.3,0) ) · BoneCS(8_a1) · Comp
  * эффект:      A = FxToBone · BoneCS · Comp ; Loc = A.origin ; Q = normalize(A)
                 центр квада = Xf.TransformPosition( UE(-Gz,Gx,Gy) )   [плоская ветка]
                 центр квада = Xf.TransformPosition( PoUE ) ...        [billboard-ветка, если BillboardMode!=0]
Критерий: расстояние центра эмиттера до ОСИ КЛИНКА (главная ось меша) ≤ нескольких юнитов.

Запуск: Tools/run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\_probe_weapon_fx_world.py
"""
import sys, os, math
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

SKEL = "/Game/AsdaAssets/Models/Skeletal/Characters/ch1_m_cremmy_preview/SkeletalMeshes/ch1_m_cremmy_preview"
CASES = [
    ("ohs_a_co_ma_03", "ch1_n_01_swrd108_00_00_0", "8_a1"),
    ("ths_a_co_ma_03", "ch1_n_03_thnd001_00_00_0", "8_a1"),
    ("spr_a_co_ma_03", "ch1_n_02_spir107_00_00_0", "8_a1"),
    ("bow_a_co_ma_03", "ch1_n_90_lbow200_00_00_0", "_8_a1"),
    ("shd_a_co_ma_03", "ch1_n_01_shid103_00_00_0", "_7_a1"),
]
REL_WEAPON = (0.0, 46.3, 0.0)     # AsdaCharAssembly::AttachWeapon


def log(m): unreal.log_warning("[fxWorld] %s" % m)


# ---------- мини-матричная библиотека (row-vector, как FMatrix/D3DX) ----------
def m_ident(): return [[1,0,0,0],[0,1,0,0],[0,0,1,0],[0,0,0,1]]
def m_mul(a, b): return [[sum(a[i][k]*b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]
def v_xf(v, m): return [sum(v[k]*m[k][j] for k in range(3)) + m[3][j] for j in range(3)]
def v_rot(v, m): return [sum(v[k]*m[k][j] for k in range(3)) for j in range(3)]
def m_norm(m):
    o = [r[:] for r in m]
    for i in range(3):
        n = math.sqrt(sum(o[i][j]**2 for j in range(3))) or 1.0
        for j in range(3): o[i][j] /= n
    o[3] = [0,0,0,1]
    return o
def t_to_m(t):
    """unreal.Transform → 4x4 row-vector с масштабом"""
    q = t.rotation; s = t.scale3d; loc = t.translation
    x, y, z, w = q.x, q.y, q.z, q.w
    r = [[1-2*(y*y+z*z), 2*(x*y+z*w),   2*(x*z-y*w),   0],
         [2*(x*y-z*w),   1-2*(x*x+z*z), 2*(y*z+x*w),   0],
         [2*(x*z+y*w),   2*(y*z-x*w),   1-2*(x*x+y*y), 0],
         [0,0,0,1]]
    for i in range(3):
        for j in range(3): r[i][j] *= (s.x, s.y, s.z)[i]
    r[3] = [loc.x, loc.y, loc.z, 1]
    return r


_LIVE = {}


def live_comp(sk):
    """Спавним SkeletalMeshActor в пустом уровне — get_socket_transform(RTS_COMPONENT) даёт
       component-space трансформ кости из ref-позы (точно, без зеркала)."""
    if "c" in _LIVE: return _LIVE["c"]
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.new_level("/Game/Temp/_fx_world_probe")
    a = eas.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(0, 0, 0))
    c = a.skeletal_mesh_component
    c.set_skeletal_mesh_asset(sk)
    _LIVE["c"] = c
    return c


def bone_cs(sk, name):
    c = live_comp(sk)
    try:
        t = c.get_socket_transform(unreal.Name(name), unreal.RelativeTransformSpace.RTS_COMPONENT)
    except Exception as e:
        unreal.log_warning("[fxWorld] socket '%s': %s" % (name, e)); return None
    return t_to_m(t)


def main():
    sk = unreal.EditorAssetLibrary.load_asset(SKEL)
    if not sk:
        log("НЕТ скелетала %s" % SKEL); return
    comp = unreal.SkeletalMeshComponent()
    comp.set_skeletal_mesh_asset(sk)
    nb = 0
    names = []
    while nb < 400:
        n = str(comp.get_bone_name(nb))
        if n in ("None", ""): break
        names.append(n); nb += 1
    log("скелет '%s': костей %d" % (SKEL.rsplit("/", 1)[-1], nb))
    log("кости: %s" % ", ".join(names))
    # Comp = MeshRoot(0.01,-0.01,0.01) — компонент тела в мире (актор в origin, без поворота)
    Comp = [[0.01,0,0,0],[0,-0.01,0,0],[0,0,0.01,0],[0,0,0,1]]
    FxToBone = [[0,100,0,0],[100,0,0,0],[0,0,100,0],[0,0,0,1]]

    for eff, mesh, bone in CASES:
        log("=" * 110)
        B = bone_cs(sk, bone)
        if B is None:
            log("%s: кость '%s' не найдена" % (eff, bone)); continue
        S = m_mul(B, Comp)                     # кость→мир (как у сокета)
        A = m_mul(FxToBone, S)                 # трансформ актора эффекта
        Loc = [A[3][0], A[3][1], A[3][2]]
        Q = m_norm(A)
        det = (Q[0][0]*(Q[1][1]*Q[2][2]-Q[1][2]*Q[2][1])
               - Q[0][1]*(Q[1][0]*Q[2][2]-Q[1][2]*Q[2][0])
               + Q[0][2]*(Q[1][0]*Q[2][1]-Q[1][1]*Q[2][0]))
        log("%s кость=%s  Loc=(%.2f,%.2f,%.2f) det(Q)=%.3f" % (eff, bone, Loc[0], Loc[1], Loc[2], det))

        # ---- мировая геометрия меша оружия ----
        mp = "/Game/AsdaAssets/Models/Char/%s/StaticMeshes/%s" % (mesh, mesh)
        sm = unreal.EditorAssetLibrary.load_asset(mp)
        blade = []
        if sm:
            try:
                r = unreal.ProceduralMeshLibrary.get_section_from_static_mesh(sm, 0, 0)
                verts = r[0]
            except Exception as e:
                verts = []; log("  вершины меша недоступны: %s" % e)
            for v in verts:
                lv = [v.x + REL_WEAPON[0], v.y + REL_WEAPON[1], v.z + REL_WEAPON[2]]
                blade.append(v_xf(lv, S))
        if not blade:
            log("  меш %s: вершин нет — пропуск" % mesh); continue
        n = len(blade)
        cx = [sum(p[i] for p in blade)/n for i in range(3)]
        # главная ось (power iteration по ковариации)
        ax = [1.0, 0.0, 0.0]
        for _ in range(60):
            acc = [0.0, 0.0, 0.0]
            for p in blade:
                d = [p[i]-cx[i] for i in range(3)]
                s = d[0]*ax[0] + d[1]*ax[1] + d[2]*ax[2]
                for i in range(3): acc[i] += s*d[i]
            L = math.sqrt(sum(c*c for c in acc)) or 1.0
            ax = [c/L for c in acc]
        proj = [sum((p[i]-cx[i])*ax[i] for i in range(3)) for p in blade]
        log("  меш '%s': вершин=%d центр=(%.1f,%.1f,%.1f) ось=(%.3f,%.3f,%.3f) длина=%.1f"
            % (mesh, n, cx[0], cx[1], cx[2], ax[0], ax[1], ax[2], max(proj)-min(proj)))

        def dist_to_blade(P):
            d = [P[i]-cx[i] for i in range(3)]
            s = d[0]*ax[0] + d[1]*ax[1] + d[2]*ax[2]
            s = max(min(s, max(proj)), min(proj))
            perp = [d[i]-s*ax[i] for i in range(3)]
            return math.sqrt(sum(c*c for c in perp)), s

        # ---- эмиттеры ----
        bp = unreal.EditorAssetLibrary.load_asset("/Game/AsdaAssets/Effects/BP_Fx_%s" % eff.replace("-", "_"))
        if not bp: log("  НЕТ BP"); continue
        cdo = unreal.get_default_object(bp.generated_class())
        for em in cdo.get_editor_property("emitters"):
            t = em.get_editor_property("jeb_type")
            nm = str(em.get_editor_property("name"))
            single = em.get_editor_property("single_quad")
            bmode = em.get_editor_property("billboard_mode")
            nodes = em.get_editor_property("nodes")
            if not nodes: continue
            n0 = nodes[0]
            if single:
                po = n0.get_editor_property("ring_pos_ofs")
                rot = n0.get_editor_property("ring_rot")
                roll = n0.get_editor_property("ring_roll")
                # ЭТАЛОННЫЙ центр (bStrictQuadAttach): pos · RotX·RotY·RotZ (D3DX), game-оси
                d = math.pi/180.0
                def RXd(p, a):
                    c, s = math.cos(a), math.sin(a); return (p[0], p[1]*c - p[2]*s, p[1]*s + p[2]*c)
                def RYd(p, a):
                    c, s = math.cos(a), math.sin(a); return (p[0]*c + p[2]*s, p[1], -p[0]*s + p[2]*c)
                def RZd(p, a):
                    c, s = math.cos(a), math.sin(a); return (p[0]*c - p[1]*s, p[0]*s + p[1]*c, p[2])
                G = RZd(RYd(RXd((po.x, po.y, po.z), rot.x*d), rot.y*d), rot.z*d)
                UE = [-G[2], G[0], G[1]]
                Pflat = [v_rot(UE, Q)[i] + Loc[i] for i in range(3)]
                # billboard-ветка (в ИГРЕ при BillboardMode!=0): pos БЕЗ RotX/RotZ, только rotY вокруг UE-Z
                PoUE = [-po.z, po.x, po.y]
                ao = rot.y*d; co, so = math.cos(ao), math.sin(ao)
                PoUE2 = [PoUE[0]*co + PoUE[1]*so, -PoUE[0]*so + PoUE[1]*co, PoUE[2]]
                Pbb = [v_rot(PoUE2, Q)[i] + Loc[i] for i in range(3)]
                df, sf = dist_to_blade(Pflat)
                db, sb = dist_to_blade(Pbb)
                log("  квад %-16s тип=0x%X bb=%d pos=(%.1f,%.1f,%.1f) roll=%.0f rot=(%.0f,%.0f,%.0f) | "
                    "плоская: мир=(%.1f,%.1f,%.1f) Δось=%.1f | billboard: мир=(%.1f,%.1f,%.1f) Δось=%.1f"
                    % (nm, t, bmode, po.x, po.y, po.z, roll, rot.x, rot.y, rot.z,
                       Pflat[0], Pflat[1], Pflat[2], df, Pbb[0], Pbb[1], Pbb[2], db))
            else:
                pb = em.get_editor_property("pos_base")
                UE = [pb.x, pb.y, pb.z]
                P = [v_rot(UE, Q)[i] + Loc[i] for i in range(3)]
                dd, ss = dist_to_blade(P)
                log("  части %-16s тип=0x%X posBase(ue)=(%.1f,%.1f,%.1f) мир=(%.1f,%.1f,%.1f) Δось=%.1f"
                    % (nm, t, pb.x, pb.y, pb.z, P[0], P[1], P[2], dd))
    log("ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
