# Copyright (c) 2026 @sitxovski (Discord)
# Инвентаризация уровня /Game/Map/Login: все акторы (класс, метка, компоненты, ассеты).
# Для ретрофита в Editor-Friendly: понять, что уже placed-актор, а что рождается/форсится из C++.
import unreal

LEVEL = "/Game/Map/Login"
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

byclass = {}


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    les.load_level(LEVEL)
    acts = eas.get_all_level_actors()
    print("=== LOGIN LEVEL: акторов=%d ===" % len(acts))
    for a in acts:
        cn = a.get_class().get_name()
        byclass.setdefault(cn, []).append(a)
    for cn in sorted(byclass):
        lst = byclass[cn]
        print("--- %s ×%d" % (cn, len(lst)))
        for a in lst[:12]:
            lbl = a.get_actor_label()
            extra = ""
            smc = a.get_component_by_class(unreal.StaticMeshComponent)
            skc = a.get_component_by_class(unreal.SkeletalMeshComponent)
            if smc:
                m = smc.get_editor_property("static_mesh")
                if m: extra = "SM=%s" % m.get_name()
            if skc:
                m = skc.get_skeletal_mesh_asset()
                if m: extra = "SK=%s" % m.get_name()
            xf = a.get_actor_transform()
            print("    %-28s %s" % (lbl, extra))
    print("=== DONE ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
