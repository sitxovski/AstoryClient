# Copyright (c) 2026 @sitxovski (Discord)
# _verify_fx_runtime_vs_baked.py — РЕШАЮЩАЯ проверка C++-рантайма эффектов против ЗАПЕЧЁННОЙ
# геометрии уровня (регрессия №2, 2026-07-30).
#
# ИДЕЯ. В alpen.umap (восстановлен из копии 29.07 — ДО правок) PMC-секции эффектов сериализованы
# такими, какими их построила ТОГДАШНЯЯ (визуально подтверждённая юзером) DLL. Детерминированные
# эмиттеры (кольца 0xE, квады 0xB, меши 0x12 — без FRand) при OnConstruction прогреваются
# фиксированные 90×(1/30)=3.0 с ⇒ их вершины воспроизводимы бит-в-бит. Значит:
#   as-loaded вершины (старая DLL) ↔ вершины после rerun_construction_scripts (ТЕКУЩАЯ DLL)
# расходятся РОВНО настолько, насколько текущий C++ уехал от калиброванного. Это ground truth,
# не зависящий ни от effects.json, ни от питон-интерпретаций (некруговая проверка).
# БЕЗ save — уровень не трогается.
import os, sys, math, json
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

MAP_NAME = os.environ.get("ASDA_MAP", "alpen")
TOL = 0.05          # юниты; детерминированные секции обязаны сойтись «в ноль»
REPERS = ("eff_rainbow_a_00", "eff_rainbow2_a_00", "eff_alpenredlight_a_00", "eff_alpenbluelight_a_00",
          "eff_alpengreenlight_a_00", "eff_alpenyellowlight_a_00", "eff_alpenjewels_a_00",
          "eff_alpenwave_a_00", "eff_mdel_alpen_pado-00", "eff_alpenflooer_a_00",
          "eff_noeaplhentower00_a_00", "eff_mdel_soulidolbase-a_00")


def log(m): unreal.log_warning("[fxbake] %s" % m)


def read_sections(act):
    """{idx: [вершины]} детерминированных секций (кольцо/квад/меш; частицы пропускаем — FRand)."""
    pmc = act.get_component_by_class(unreal.ProceduralMeshComponent)
    ems = act.get_editor_property("emitters")
    out = {}
    if not pmc: return out
    for i, em in enumerate(ems):
        deterministic = em.get_editor_property("ring") or em.get_editor_property("single_quad") \
                        or em.get_editor_property("mesh_emitter")
        if not deterministic: continue
        # ★ квады с billboardMode≠0 зависят от камеры вьюпорта — недетерминированы в headless, пропуск
        if em.get_editor_property("single_quad") and em.get_editor_property("billboard_mode") != 0:
            continue
        try:
            sec = unreal.ProceduralMeshLibrary.get_section_from_procedural_mesh(pmc, i)
            verts = sec[0] if isinstance(sec, tuple) else sec
        except Exception as ex:
            log("  get_section %s sec%d: %s" % (act.get_actor_label(), i, ex))
            continue
        if verts: out[i] = [(v.x, v.y, v.z) for v in verts]
    return out


def main():
    lvl = "/Game/Map/%s/%s" % (MAP_NAME, MAP_NAME)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level(lvl)
    actors = []
    for a in eas.get_all_level_actors():
        if not isinstance(a, unreal.AsdaEffectActor): continue
        lbl = a.get_actor_label()
        if not lbl.startswith("MapEffect_"): continue
        stem = lbl[len("MapEffect_"):]
        stem = stem.rsplit("_", 1)[0] if stem.rsplit("_", 1)[-1].isdigit() else stem
        if stem in REPERS: actors.append(a)
    log("реперных акторов: %d" % len(actors))

    baked = {a.get_actor_label(): read_sections(a) for a in actors}     # as-loaded = старая DLL
    n_baked = sum(len(v) for v in baked.values())
    log("запечённых детерминированных секций: %d" % n_baked)

    worst = 0.0; bad = 0; total = 0; report = []
    for a in actors:
        lbl = a.get_actor_label()
        # пересборка ТЕКУЩЕЙ DLL: set_editor_property → PostEditChange → RerunConstructionScripts →
        # OnConstruction (ResetStates + прогрев 90×1/30). Тот же механизм, на который опирается
        # asda_place_effects («emitters ПОСЛЕ attach: set_editor_property → прогрев печёт вершины»).
        a.set_editor_property("cull_distance", a.get_editor_property("cull_distance"))
        now = read_sections(a)
        for i, old_v in baked[lbl].items():
            new_v = now.get(i)
            if not new_v or len(new_v) != len(old_v):
                bad += 1
                report.append("%s sec%d: ТОПОЛОГИЯ %s -> %s" % (lbl, i, len(old_v), len(new_v or [])))
                continue
            dmax = 0.0; centro_old = [0, 0, 0]; centro_new = [0, 0, 0]
            for (ox, oy, oz), (nx, ny, nz) in zip(old_v, new_v):
                dmax = max(dmax, math.sqrt((ox-nx)**2 + (oy-ny)**2 + (oz-nz)**2))
                for j, c in enumerate((ox, oy, oz)): centro_old[j] += c/len(old_v)
                for j, c in enumerate((nx, ny, nz)): centro_new[j] += c/len(new_v)
            total += 1; worst = max(worst, dmax)
            if dmax > TOL:
                bad += 1
                dc = [round(centro_new[j]-centro_old[j], 1) for j in range(3)]
                report.append("%s sec%d: dMax=%.1f юн, сдвиг центроида=%s" % (lbl, i, dmax, dc))
    for r in report[:30]: log("  РАСХОЖДЕНИЕ " + r)
    log("=== ИТОГ: секций сверено %d, расхождений >%s юн — %d, максимум %.3f юн" % (total, TOL, bad, worst))
    log("ВЕРДИКТ: %s" % ("РАНТАЙМ УЕХАЛ ОТ КАЛИБРОВАННОГО (старая DLL ≠ текущая)" if bad else
                         "ОК — текущая DLL воспроизводит запечённую (калиброванную) геометрию бит-в-бит"))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
