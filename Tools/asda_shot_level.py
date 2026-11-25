# Copyright (c) 2026 @sitxovski (Discord)
# asda_shot_level.py — headless-скриншот собранного уровня карты (визуальная сверка).
# Запуск БЕЗ -NullRHI (нужен реальный RHI/DX11). env ASDA_MAP (по умолч alpen), ASDA_SHOT (путь png).
import os, sys, unreal
MAP = os.environ.get("ASDA_MAP", "alpen")
OUT = os.environ.get("ASDA_SHOT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/ue_%s.png" % MAP)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
# bounds по всем акторам
lo = unreal.Vector(1e9,1e9,1e9); hi = unreal.Vector(-1e9,-1e9,-1e9)


def _map_asset_name(name):
    """Имя карты → имя ассета уровня (★ ревизия 15.09: единое правило реестра карт; было сырое имя — «Mountain Flamion» промахивался)."""
    here = os.path.dirname(os.path.abspath(__file__))
    if here not in sys.path:
        sys.path.insert(0, here)
    import asda_maps
    return asda_maps.map_asset_name(name)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    _mn = _map_asset_name(MAP)
    les.load_level("/Game/Map/%s/%s" % (_mn, _mn))
    for a in eas.get_all_level_actors():
        cn = a.get_class().get_name()
        if "Light" in cn or "Sky" in cn or "Atmosphere" in cn or "Fog" in cn:
            continue   # свет/небо на z=30000 раздувают bounds -> камера улетает
        try:
            org, ext = a.get_actor_bounds(False)
        except Exception:
            continue
        for ax in ("x","y","z"):
            setattr(lo, ax, min(getattr(lo,ax), getattr(org,ax)-getattr(ext,ax)))
            setattr(hi, ax, max(getattr(hi,ax), getattr(org,ax)+getattr(ext,ax)))
    cx=(lo.x+hi.x)*0.5; cy=(lo.y+hi.y)*0.5
    span=max(hi.x-lo.x, hi.y-lo.y, 1.0)
    # наклонная облётная камера (как эталон Map Viewer): сбоку-сверху, смотрит на центр.
    eye = unreal.Vector(cx - span*0.55, cy - span*0.55, hi.z + span*0.55)
    look = unreal.Vector(cx, cy, (lo.z+hi.z)*0.5)
    rot = unreal.MathLibrary.find_look_at_rotation(eye, look)
    unreal.log("BOUNDS lo=(%.0f,%.0f,%.0f) hi=(%.0f,%.0f,%.0f) eye=(%.0f,%.0f,%.0f)" %
               (lo.x,lo.y,lo.z, hi.x,hi.y,hi.z, eye.x,eye.y,eye.z))
    cam_actor = eas.spawn_actor_from_class(unreal.CameraActor, eye, rot)
    try:
        cam_actor.camera_component.set_editor_property("field_of_view", 75.0)
    except Exception:
        pass
    unreal.AutomationLibrary.take_high_res_screenshot(1600, 900, OUT, camera=cam_actor)
    unreal.log("SHOT requested (camera actor) -> %s" % OUT)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
