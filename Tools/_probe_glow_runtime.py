# Copyright (c) 2026 @sitxovski (Discord)
# _probe_glow_runtime.py — РАНТАЙМ-проба: реально ли BP свечения рисует геометрию.
# Спавним актор BP_Fx на пустой сцене, прогреваем тики и смотрим ФАКТИЧЕСКИЕ секции PMC:
# число вершин, габариты, назначен ли материал. Если вершин 0 / габариты 0 — эффект невидим.
import sys
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

CASES = ["ohs_a_co_ma_03", "ths_a_co_ma_03", "bow_a_co_ma_03", "shd_a_co_ma_03", "test-000"]


def log(m): unreal.log_warning("[glowRT] %s" % m)


def main():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.new_level("/Game/Temp/_glow_probe")
    for name in CASES:
        path = "/Game/AsdaAssets/Effects/BP_Fx_%s.BP_Fx_%s_C" % (name, name)
        cls = unreal.load_object(None, path)
        if not cls:
            log("%-20s BP НЕ ЗАГРУЖЕН (%s)" % (name, path)); continue
        a = eas.spawn_actor_from_class(cls, unreal.Vector(0, 0, 0))
        if not a:
            log("%-20s спавн не удался" % name); continue
        ems = a.get_editor_property("emitters")
        pmc = a.get_component_by_class(unreal.ProceduralMeshComponent)
        nsec = pmc.get_num_sections() if pmc else -1
        tot = 0; bmin = [1e9]*3; bmax = [-1e9]*3; withmat = 0
        for i in range(len(ems)):
            try:
                r = unreal.ProceduralMeshLibrary.get_section_from_procedural_mesh(pmc, i)
                V = r[0]
            except Exception:
                V = []
            if V:
                tot += len(V)
                for v in V:
                    for k, c in enumerate((v.x, v.y, v.z)):
                        bmin[k] = min(bmin[k], c); bmax[k] = max(bmax[k], c)
            if ems[i].get_editor_property("material"): withmat += 1
        size = [bmax[k]-bmin[k] for k in range(3)] if tot else [0, 0, 0]
        log("%-20s эмиттеров=%d секций=%d вершин=%d размер=(%.1f,%.1f,%.1f) с_материалом=%d"
            % (name, len(ems), nsec, tot, size[0], size[1], size[2], withmat))
        eas.destroy_actor(a)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
