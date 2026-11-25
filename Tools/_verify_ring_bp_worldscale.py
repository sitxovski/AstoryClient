# Copyright (c) 2026 @sitxovski (Discord)
# _verify_ring_bp_worldscale.py — проверка ВОССТАНОВЛЕННОЙ семантики basePos 0xE (2026-07-31).
#
# ПРИНЯТОЕ состояние (дневник сессии аудита лоадеров: «RingBasePos (сырой мир ×scale) в рантайме»;
# эталонный сдвиг rainbow2 в памяти type2-chirality: [38.37,126.83,7.04] ≈ swap(bp)·s):
#   вклад basePos = swap(bp) · |actorScale| в МИРОВЫХ осях (БЕЗ поворота актором/костью).
# МЕТОД: для каждого репера спавнятся два двойника — A: bp как в карте; B: bp обнулён во всех нодах.
# Δ = centroid(A) − centroid(B) — чистый вклад bp, геометрия кольца сокращается.
# Предсказание: pred = swap(bp)·s. |Δ−pred| ≤ 0.5 юн. Save не делается, двойники удаляются.
import sys, math
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

REPERS = ("eff_rainbow_a_00_01", "eff_rainbow_a_00_02", "eff_rainbow_a_00_03",
          "eff_rainbow2_a_00_01", "eff_alpenredlight_a_00_01", "eff_alpenbluelight_a_00_01",
          "eff_alpengreenlight_a_00_01", "eff_alpenyellowlight_a_00_01",
          "eff_noeaplhentower00_a_00_01")


def log(m): unreal.log_warning("[ringbpW] %s" % m)


def centroid(a, i):
    pmc = a.get_component_by_class(unreal.ProceduralMeshComponent)
    r = unreal.ProceduralMeshLibrary.get_section_from_procedural_mesh(pmc, i)
    V = r[0]
    if not V: return None
    c = [0.0, 0.0, 0.0]
    for v in V: c[0] += v.x/len(V); c[1] += v.y/len(V); c[2] += v.z/len(V)
    return c


def spawn_twin(eas, src, xf, ems):
    b = eas.spawn_actor_from_class(unreal.AsdaEffectActor, xf.translation)
    b.set_actor_transform(xf, False, False)
    for pn in ("chiral_normal", "holder_yaw_start", "holder_yaw_speed", "holder_pos_vel",
               "b_base_already_ue", "effect_emitter_scale"):
        try: b.set_editor_property(pn, src.get_editor_property(pn))
        except Exception: pass
    b.set_editor_property("emitters", ems)
    return b


def main():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level("/Game/Map/alpen/alpen")
    bad = 0; total = 0; worst = 0.0
    for a in list(eas.get_all_level_actors()):
        if not isinstance(a, unreal.AsdaEffectActor): continue
        lbl = a.get_actor_label()
        if lbl[len("MapEffect_"):] not in REPERS: continue
        xf = a.get_actor_transform()
        s = abs(xf.scale3d.x)
        emsA = a.get_editor_property("emitters")
        for i in range(len(emsA)):
            if not emsA[i].get_editor_property("ring"): continue
            nd = emsA[i].get_editor_property("nodes")
            if not nd: continue
            bp = nd[0].get_editor_property("ring_base_pos")
            if abs(bp.x) + abs(bp.y) + abs(bp.z) < 1e-3: continue
            pred = unreal.Vector(-bp.z * s, bp.x * s, bp.y * s)
            A = spawn_twin(eas, a, xf, a.get_editor_property("emitters"))
            cA = centroid(A, i)
            emsB = a.get_editor_property("emitters")
            e = emsB[i]
            ndB = e.get_editor_property("nodes")
            for k in range(len(ndB)):
                n = ndB[k]; n.set_editor_property("ring_base_pos", unreal.Vector(0, 0, 0)); ndB[k] = n
            e.set_editor_property("nodes", ndB); emsB[i] = e
            B = spawn_twin(eas, a, xf, emsB)
            cB = centroid(B, i)
            eas.destroy_actor(A); eas.destroy_actor(B)
            if not cA or not cB: log("%s em%d: нет секций" % (lbl, i)); bad += 1; continue
            meas = [cA[k] - cB[k] for k in range(3)]
            err = math.sqrt(sum((meas[k] - [pred.x, pred.y, pred.z][k])**2 for k in range(3)))
            total += 1; worst = max(worst, err)
            ok = err <= 0.5
            if not ok: bad += 1
            log("%-42s em%d s=%.2f bp=(%.2f,%.2f,%.2f) Δ=(%.1f,%.1f,%.1f) pred=(%.1f,%.1f,%.1f) err=%.3f %s" %
                (lbl, i, s, bp.x, bp.y, bp.z, meas[0], meas[1], meas[2], pred.x, pred.y, pred.z, err,
                 "OK" if ok else "FAIL"))
    log("=== ИТОГ: колец %d, несовпадений %d, макс.ошибка %.3f юн" % (total, bad, worst))
    log("ВЕРДИКТ: %s" % ("FAIL" if bad else "ОК — вклад basePos = swap(bp)·s в МИРЕ (принятая семантика)"))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
