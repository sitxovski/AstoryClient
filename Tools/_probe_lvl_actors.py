# Copyright (c) 2026 @sitxovski (Discord)
import os, unreal, collections
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

# Уровень задаётся переменной окружения (было жёстко alpen). Пример: ASDA_LEVEL=/Game/Map/Login
LEVEL = os.environ.get("ASDA_LEVEL", "/Game/Map/alpen/alpen")


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    les.load_level(LEVEL)
    unreal.log("[LA] уровень: %s" % LEVEL)
    acts = eas.get_all_level_actors()
    cnt = collections.Counter(a.get_class().get_name() for a in acts)
    for k,v in cnt.most_common(25): unreal.log("[LA] %-40s %d" % (k,v))
    tags = collections.Counter()
    for a in acts:
        for t in a.tags: tags[str(t)] += 1
    unreal.log("[LA] теги: %s" % tags.most_common(15))
    # ★ Mobility источников света и мешей: от неё зависит, участвует ли объект в ЗАПЕЧЁННОМ свете.
    #   Нужна, когда что-то переводится в Movable (напр. элементы задника под анимацию) — сразу видно,
    #   теряется ли при этом запечённое освещение или свет в сцене и так динамический.
    lights = collections.Counter()
    meshmob = collections.Counter()
    for a in acts:
        rc = a.get_editor_property("root_component")
        if not rc:
            continue
        mob = str(rc.get_editor_property("mobility")).split(".")[-1]
        cls = a.get_class().get_name()
        if "Light" in cls:
            lights["%s/%s" % (cls, mob)] += 1
        elif "MeshActor" in cls:
            meshmob["%s/%s" % (cls, mob)] += 1
    unreal.log("[LA] СВЕТ: %s" % (lights.most_common(20) or "источников света нет"))
    unreal.log("[LA] МЕШИ по Mobility: %s" % meshmob.most_common(10))
    unreal.log("[LA] ЗАВЕРШЕНО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
