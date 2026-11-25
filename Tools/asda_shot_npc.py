# Copyright (c) 2026 @sitxovski (Discord)
# asda_shot_npc.py — верификация NPC: дамп трансформов + ближний скриншот к выбранному NPC.
# Запуск БЕЗ -NullRHI (реальный RHI). env ASDA_MAP, ASDA_NPC_IDX (индекс NPC для кадра, по умолч 0),
#   ASDA_SHOT (png).
import os, unreal
MAP = os.environ.get("ASDA_MAP", "alpen")
SEL = int(os.environ.get("ASDA_NPC_IDX", "0"))
OUT = os.environ.get("ASDA_SHOT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/ue_npc_%s.png" % MAP)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    les.load_level("/Game/Map/%s/%s" % (MAP, MAP))
    npcs=[a for a in eas.get_all_level_actors() if a.actor_has_tag("AsdaNPC")]
    npcs.sort(key=lambda a: a.get_actor_label())
    unreal.log("=== NPC найдено: %d ===" % len(npcs))
    for a in npcs[:60]:
        t=a.get_actor_transform(); loc=t.translation; rot=t.rotation.rotator(); sc=t.scale3d
        try: org,ext=a.get_actor_bounds(False)
        except Exception: org=ext=unreal.Vector()
        unreal.log("  %-40s loc=(%.0f,%.0f,%.0f) rot(P=%.1f Y=%.1f R=%.1f) sc=(%.3f,%.3f,%.3f) z[%.0f..%.0f]" %
                   (a.get_actor_label(), loc.x,loc.y,loc.z, rot.pitch,rot.yaw,rot.roll,
                    sc.x,sc.y,sc.z, org.z-ext.z, org.z+ext.z))
    if not npcs:
        unreal.log_error("NPC в уровне нет"); raise SystemExit
    sel = npcs[min(SEL, len(npcs)-1)]
    t=sel.get_actor_transform(); c=t.translation
    try: org,ext=sel.get_actor_bounds(False)
    except Exception: org=c; ext=unreal.Vector(100,100,100)
    # камера: сбоку-спереди, на высоте груди, дистанция ~ 3× высоты модели
    h=max(ext.z*2, 150.0)
    eye=unreal.Vector(c.x - h*1.6, c.y - h*1.6, org.z + h*0.4)
    look=unreal.Vector(org.x, org.y, org.z)
    rot=unreal.MathLibrary.find_look_at_rotation(eye, look)
    unreal.log("SHOT NPC '%s' eye=(%.0f,%.0f,%.0f) look=(%.0f,%.0f,%.0f)" %
               (sel.get_actor_label(), eye.x,eye.y,eye.z, look.x,look.y,look.z))
    cam=eas.spawn_actor_from_class(unreal.CameraActor, eye, rot)
    try: cam.camera_component.set_editor_property("field_of_view", 60.0)
    except Exception: pass
    unreal.AutomationLibrary.take_high_res_screenshot(1280, 1280, OUT, camera=cam)
    unreal.log("SHOT -> %s" % OUT)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
