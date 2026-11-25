# Copyright (c) 2026 @sitxovski (Discord)
# Диагностика «сгруппированности» login-сцены: UE-группы (GroupActor), attach-иерархия, папки Outliner.
# Цель: понять, что мешает править отдельные акторы, и как расгруппировать/разложить.
import unreal

LEVEL = "/Game/Map/Login"
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    les.load_level(LEVEL)
    acts = eas.get_all_level_actors()
    print("=== LOGIN: акторов=%d ===" % len(acts))
    groups = 0
    for a in acts:
        cn = a.get_class().get_name()
        lbl = a.get_actor_label()
        par = a.get_attach_parent_actor()
        folder = ""
        try: folder = str(a.get_folder_path())
        except Exception: pass
        # состояние выделяемости/локов
        flags = []
        try:
            if not a.get_editor_property("is_editor_only_actor"): pass
        except Exception: pass
        is_group = ("GroupActor" in cn)
        if is_group: groups += 1
        print("%-26s cls=%-22s parent=%-24s folder=%s%s" % (
            lbl, cn, par.get_actor_label() if par else "-", folder, "  <<GROUP>>" if is_group else ""))
    print("=== GroupActor'ов в уровне: %d ===" % groups)
    # для backdrop — из чего состоит (один меш или много секций/компонент)
    for a in acts:
        if a.get_actor_label().startswith("LoginBackdrop"):
            comps = a.get_components_by_class(unreal.SkeletalMeshComponent)
            for c in comps:
                m = c.get_skeletal_mesh_asset()
                if m:
                    nmat = len(c.get_materials())
                    print("BACKDROP mesh=%s материалов(секций)=%d" % (m.get_name(), nmat))
    print("=== DONE ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
