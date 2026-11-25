# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""СЫРЫЕ (клиентские) габариты меша оружия против СЫРОГО охвата полотен .jeb —
   пространство, в котором ОБА авторены (доказано дампом: нода меша = I, эффект = та же кость)."""
import sys, math
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

D = math.pi/180.0
CASES = [
    ("bali_e_ra_ma_03", "ch1_n_90_bbow209_00_00_0"),
    ("ohs_a_co_ma_01",  "ch1_n_00_swrd003_00_00_0"),
    ("ohs_a_co_ma_03",  "ch1_n_01_swrd108_00_00_0"),
    ("ths_a_co_ma_03",  "ch1_n_03_thnd001_00_00_0"),
    ("spr_a_co_ma_03",  "ch1_n_02_spir107_00_00_0"),
    ("bow_a_co_ma_03",  "ch1_n_90_lbow200_00_00_0"),
    ("shd_a_co_ma_03",  "ch1_n_01_shid103_00_00_0"),
]


def log(m): unreal.log_warning("[raw] %s" % m)
def RX(P, A): c, s = math.cos(A), math.sin(A); return (P[0], P[1]*c+P[2]*s, -P[1]*s+P[2]*c)
def RY(P, A): c, s = math.cos(A), math.sin(A); return (P[0]*c-P[2]*s, P[1], P[0]*s+P[2]*c)
def RZ(P, A): c, s = math.cos(A), math.sin(A); return (P[0]*c+P[1]*s, -P[0]*s+P[1]*c, P[2])
def RXd(P, A): c, s = math.cos(A), math.sin(A); return (P[0], P[1]*c-P[2]*s, P[1]*s+P[2]*c)
def RYd(P, A): c, s = math.cos(A), math.sin(A); return (P[0]*c+P[2]*s, P[1], -P[0]*s+P[2]*c)
def RZd(P, A): c, s = math.cos(A), math.sin(A); return (P[0]*c-P[1]*s, P[0]*s+P[1]*c, P[2])


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for eff, mesh in CASES:
        sm = unreal.EditorAssetLibrary.load_asset("/Game/AsdaAssets/Models/Char/%s/StaticMeshes/%s" % (mesh, mesh))
        log("=" * 108)
        if not sm:
            log("%s: НЕТ меша %s" % (eff, mesh)); continue
        try: V = unreal.ProceduralMeshLibrary.get_section_from_static_mesh(sm, 0, 0)[0]
        except Exception as e:
            log("%s: вершины недоступны %s" % (eff, e)); continue
        # UE-ассет = (rawX, −rawZ, rawY)*100 (+ relative Y 46.3)  ⇒  raw = (X/100, Z/100, −(Y−46.3)/100)
        R = [(v.x/100.0, v.z/100.0, -(v.y + 46.3)/100.0) for v in V]
        lo = [min(p[i] for p in R) for i in range(3)]
        hi = [max(p[i] for p in R) for i in range(3)]
        log("%s | меш %s (%d верш.) СЫРЫЕ габариты: x[%.1f..%.1f] y[%.1f..%.1f] z[%.1f..%.1f]"
            % (eff, mesh, len(R), lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]))
        bp = unreal.EditorAssetLibrary.load_asset("/Game/AsdaAssets/Effects/BP_Fx_%s" % eff)
        if not bp: log("  НЕТ BP"); continue
        cdo = unreal.get_default_object(bp.generated_class())
        for em in cdo.get_editor_property("emitters"):
            if not em.get_editor_property("single_quad"): continue
            nm = str(em.get_editor_property("name")); n0 = em.get_editor_property("nodes")[0]
            SX = max(n0.get_editor_property("size_x"), 1.0); SY = max(n0.get_editor_property("size_y"), 1.0)
            roll = n0.get_editor_property("ring_roll"); rt = n0.get_editor_property("ring_rot")
            po = n0.get_editor_property("ring_pos_ofs")
            Cc = RZd(RYd(RXd((po.x, po.y, po.z), rt.x*D), rt.y*D), rt.z*D)
            pts = []
            for g0 in [(-SX, 0, -SY), (SX, 0, -SY), (SX, 0, SY), (-SX, 0, SY)]:
                G = RY(g0, roll*D); G = RZ(RY(RX(G, rt.x*D), rt.y*D), rt.z*D)
                pts.append((G[0]+Cc[0], G[1]+Cc[1], G[2]+Cc[2]))
            q_lo = [min(p[i] for p in pts) for i in range(3)]
            q_hi = [max(p[i] for p in pts) for i in range(3)]
            log("  %-15s тип=0x%X  СЫРОЙ охват полотна: x[%.0f..%.0f] y[%.0f..%.0f] z[%.0f..%.0f]  (размер %.0fx%.0f roll=%.0f rot=(%.0f,%.0f,%.0f) pos=(%.1f,%.1f,%.1f))"
                % (nm, em.get_editor_property("jeb_type"), q_lo[0], q_hi[0], q_lo[1], q_hi[1], q_lo[2], q_hi[2],
                   SX, SY, roll, rt.x, rt.y, rt.z, po.x, po.y, po.z))
    log("[raw] ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
