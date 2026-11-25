# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# Дамп крупнейших акторов alpen: класс/метка/центр/экстент — чтобы поставить камеру скриншота.
import unreal
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
rows = []


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    assert les.load_level("/Game/Map/alpen/alpen")
    for a in eas.get_all_level_actors():
        try:
            o, e = a.get_actor_bounds(False)
            vol = e.x * e.y * max(e.z, 1.0)
            rows.append((vol, a.get_class().get_name(), a.get_actor_label(),
                         (round(o.x), round(o.y), round(o.z)), (round(e.x), round(e.y), round(e.z))))
        except Exception:
            pass
    rows.sort(reverse=True)
    with open(r"@@ASTORY_DEV_ROOT@@\Reports\alpen_actors.txt", "w", encoding="utf-8") as f:
        for r in rows[:25]:
            f.write("%s | %s | origin=%s ext=%s\n" % (r[1], r[2], r[3], r[4]))
    unreal.log("ALPEN_ACTORS_DONE")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
