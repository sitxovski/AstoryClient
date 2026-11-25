# Copyright (c) 2026 @sitxovski (Discord)
# read-only: текущие свойства фонарей
import sys
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    les.load_level("/Game/Map/alpen/alpen")
    for a in list(eas.get_all_level_actors()):
        if not isinstance(a, unreal.AsdaEffectActor): continue
        lbl = a.get_actor_label()
        if "light_a_00" not in lbl and "rainbow" not in lbl and "tower" not in lbl: continue
        cn = a.get_editor_property("chiral_normal")
        ems = a.get_editor_property("emitters")
        for i in range(len(ems)):
            if not ems[i].get_editor_property("ring"): continue
            n0 = ems[i].get_editor_property("nodes")[0]
            bp = n0.get_editor_property("ring_base_pos"); po = n0.get_editor_property("ring_pos_ofs")
            unreal.log_warning("[chk] %s em%d chiral=(%.3f,%.3f,%.3f) rBase=(%.3f,%.3f,%.3f) rPos=(%.2f,%.2f,%.2f)" %
                (lbl, i, cn.x, cn.y, cn.z, bp.x, bp.y, bp.z, po.x, po.y, po.z))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
