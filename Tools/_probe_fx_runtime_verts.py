# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""Сверка РЕАЛЬНОЙ геометрии C++ (PMC-секции AAsdaEffectActor) с python-репликой формулы.
   Актор ставится в identity ⇒ вершины PMC = ЛОКАЛЬНЫЕ координаты эффекта."""
import sys, math
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

CASES = ["bali_e_ra_ma_03", "ohs_a_co_ma_03"]
D = math.pi/180.0


def log(m): unreal.log_warning("[rt] %s" % m)
def RX(P, A): c, s = math.cos(A), math.sin(A); return (P[0], P[1]*c+P[2]*s, -P[1]*s+P[2]*c)
def RY(P, A): c, s = math.cos(A), math.sin(A); return (P[0]*c-P[2]*s, P[1], P[0]*s+P[2]*c)
def RZ(P, A): c, s = math.cos(A), math.sin(A); return (P[0]*c+P[1]*s, -P[0]*s+P[1]*c, P[2])
def RXd(P, A): c, s = math.cos(A), math.sin(A); return (P[0], P[1]*c-P[2]*s, P[1]*s+P[2]*c)
def RYd(P, A): c, s = math.cos(A), math.sin(A); return (P[0]*c+P[2]*s, P[1], -P[0]*s+P[2]*c)
def RZd(P, A): c, s = math.cos(A), math.sin(A); return (P[0]*c-P[1]*s, P[0]*s+P[1]*c, P[2])


les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    try: les.new_level("/Game/Temp/_rt_probe")
    except Exception: pass
    for eff in CASES:
        bp = unreal.EditorAssetLibrary.load_asset("/Game/AsdaAssets/Effects/BP_Fx_%s" % eff)
        if not bp: log("НЕТ BP %s" % eff); continue
        cls = bp.generated_class()
        cdo = unreal.get_default_object(cls)
        prev = cdo.get_editor_property("strict_quad_attach")
        cdo.set_editor_property("strict_quad_attach", True)
        a = eas.spawn_actor_from_class(cls, unreal.Vector(0, 0, 0))
        pmc = a.get_component_by_class(unreal.ProceduralMeshComponent)
        ems = a.get_editor_property("emitters")
        log("=" * 110)
        log("%s: эмиттеров=%d strict=%s секций=%d" % (eff, len(ems), a.get_editor_property("strict_quad_attach"),
                                                      pmc.get_num_sections() if pmc else -1))
        for i in range(len(ems)):
            em = ems[i]
            if not em.get_editor_property("single_quad"): continue
            nm = str(em.get_editor_property("name"))
            try: V = unreal.ProceduralMeshLibrary.get_section_from_procedural_mesh(pmc, i)[0]
            except Exception as e:
                log("  %-14s секция недоступна: %s" % (nm, e)); continue
            if not V:
                log("  %-14s ПУСТО" % nm); continue
            n0 = em.get_editor_property("nodes")[0]
            SXv = max(n0.get_editor_property("size_x"), 1.0); SYv = max(n0.get_editor_property("size_y"), 1.0)
            roll = n0.get_editor_property("ring_roll"); rt = n0.get_editor_property("ring_rot")
            po = n0.get_editor_property("ring_pos_ofs")
            Cc = RZd(RYd(RXd((po.x, po.y, po.z), rt.x*D), rt.y*D), rt.z*D)
            rep = []
            for g0 in [(-SXv, 0, -SYv), (SXv, 0, -SYv), (SXv, 0, SYv), (-SXv, 0, SYv)]:
                G = RY(g0, roll*D); G = RZ(RY(RX(G, rt.x*D), rt.y*D), rt.z*D)
                G = (G[0]+Cc[0], G[1]+Cc[1], G[2]+Cc[2])
                rep.append((-G[2], G[0], G[1]))
            real = [(v.x, v.y, v.z) for v in V[:4]]
            dmax = max(min(math.dist(r, q) for q in rep) for r in real) if len(real) == 4 else -1
            log("  %-14s C++=%s | реплика=%s | maxΔ=%.2f"
                % (nm, "; ".join("(%.1f,%.1f,%.1f)" % p for p in real),
                   "; ".join("(%.1f,%.1f,%.1f)" % p for p in rep), dmax))
        eas.destroy_actor(a)
        cdo.set_editor_property("strict_quad_attach", prev)
    log("[rt] ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
