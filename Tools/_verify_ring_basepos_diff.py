# Copyright (c) 2026 @sitxovski (Discord)
# _verify_ring_basepos_diff.py — дифференциальное доказательство фикса bStrictRingBasePos.
#
# ЧТО ДОКАЗЫВАЕМ. (а) При дефолте (false) рантайм даёт МИРОВУЮ (историческую) семантику basePos.
# (б) При true — локальную; центроид кольца сдвигается ровно на Δ = R_actor·(swap(bp))·s − swap(bp),
# предсказанное аналитически. Совпадение замера с предсказанием = обе ветки реализованы точно.
#
# МЕТОД. Для каждого реперного кольца спавнятся ДВА временных актора-двойника (тот же мировой
# трансформ + ChiralNormal + holder-параметры + те же эмиттеры), у одного флаг false, у другого true.
# Спавн гарантирует OnConstruction (прогрев 90×1/30) ТЕКУЩЕЙ DLL — set_editor_property в headless
# конструкцию НЕ перезапускает (проверено probe: хэш частиц не меняется). Двойники удаляются, save нет.
import os, sys, math
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

REPERS = ("eff_rainbow_a_00_01", "eff_rainbow2_a_00_01", "eff_alpenredlight_a_00_01",
          "eff_alpenbluelight_a_00_01", "eff_alpengreenlight_a_00_01", "eff_alpenyellowlight_a_00_01",
          "eff_noeaplhentower00_a_00_01")


def log(m): unreal.log_warning("[ringbp] %s" % m)


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
    b.set_editor_property("emitters", ems)          # спавн+set до первого чтения секций; OnConstruction прошёл
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
        ems = a.get_editor_property("emitters")
        for i in range(len(ems)):
            if not ems[i].get_editor_property("ring"): continue
            nd = ems[i].get_editor_property("nodes")
            if not nd: continue
            bp = nd[0].get_editor_property("ring_base_pos")
            ue = unreal.Vector(-bp.z, bp.x, bp.y)
            pred = unreal.MathLibrary.transform_direction(xf, ue) * abs(xf.scale3d.x) - ue
            # двойник A: флаг false (дефолт, как в массиве карты)
            emsA = a.get_editor_property("emitters")
            e = emsA[i]; e.set_editor_property("strict_ring_base_pos", False); emsA[i] = e
            A = spawn_twin(eas, a, xf, emsA)
            cA = centroid(A, i)
            # двойник B: флаг true
            emsB = a.get_editor_property("emitters")
            e = emsB[i]; e.set_editor_property("strict_ring_base_pos", True); emsB[i] = e
            B = spawn_twin(eas, a, xf, emsB)
            cB = centroid(B, i)
            eas.destroy_actor(A); eas.destroy_actor(B)
            if not cA or not cB: log("%s em%d: нет секций у двойников" % (lbl, i)); bad += 1; continue
            meas = [cB[k] - cA[k] for k in range(3)]
            err = math.sqrt(sum((meas[k] - [pred.x, pred.y, pred.z][k])**2 for k in range(3)))
            total += 1; worst = max(worst, err)
            ok = err <= 0.5
            if not ok: bad += 1
            log("%-42s em%d bp=(%.1f,%.1f,%.1f) Δзамер=(%.1f,%.1f,%.1f) Δпредсказ=(%.1f,%.1f,%.1f) |ошибка|=%.3f %s" %
                (lbl, i, bp.x, bp.y, bp.z, meas[0], meas[1], meas[2], pred.x, pred.y, pred.z, err,
                 "OK" if ok else "FAIL"))
    log("=== ИТОГ: колец %d, несовпадений %d, макс.ошибка %.3f юн" % (total, bad, worst))
    log("ВЕРДИКТ: %s" % ("FAIL" if bad else
        "ОК — дефолт(false)=историческая МИРОВАЯ семантика basePos; true=локальная, сдвиг ровно предсказанный"))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
