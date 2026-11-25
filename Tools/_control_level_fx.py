# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""КОНТРОЛЬ 2: запечённые в уровень эффекты карт (alpen — радуга/фонари/башня) не затронуты:
   у КАЖДОГО актора проверяем strict_quad_attach=False и follow_skel_component=None + печатаем
   габариты PMC-секций (числовой снимок)."""
import sys
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal
def log(m): unreal.log_warning("[контроль2] %s" % m)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    les.load_level("/Game/Map/alpen/alpen")
    acts = unreal.GameplayStatics.get_all_actors_of_class(unreal.EditorLevelLibrary.get_editor_world(), unreal.AsdaEffectActor)
    log("эффект-акторов в уровне alpen: %d" % len(acts))
    bad = 0; shown = 0
    for a in acts:
        st = a.get_editor_property("strict_quad_attach")
        sk = a.get_editor_property("follow_skel_component")
        if st or sk is not None:
            bad += 1; log("!! %s strict=%s skel=%s" % (a.get_actor_label(), st, sk))
        if shown < 6:
            pmc = a.get_component_by_class(unreal.ProceduralMeshComponent)
            ems = a.get_editor_property("emitters")
            tot = 0; bmin=[1e9]*3; bmax=[-1e9]*3
            for i in range(len(ems)):
                try: V = unreal.ProceduralMeshLibrary.get_section_from_procedural_mesh(pmc, i)[0]
                except Exception: V = []
                for v in V:
                    tot += 1
                    for k,c in enumerate((v.x,v.y,v.z)): bmin[k]=min(bmin[k],c); bmax[k]=max(bmax[k],c)
            if tot:
                log("%-34s '%s' верш=%-5d габариты X[%.1f..%.1f] Y[%.1f..%.1f] Z[%.1f..%.1f]"
                    % (a.get_actor_label(), a.get_editor_property("effect_name"), tot,
                       bmin[0],bmax[0],bmin[1],bmax[1],bmin[2],bmax[2]))
                shown += 1
    log("акторов с включёнными НОВЫМИ ветками: %d (должно быть 0)" % bad)
    log("ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
