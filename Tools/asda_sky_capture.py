# Copyright (c) 2026 @sitxovski (Discord)
# asda_sky_capture.py — SceneCapture-скрин неба уровня (самопроверка облаков без участия юзера)
# env: ASDA_MAP, ASDA_CAP_OUT, ASDA_CAP_PITCH (град, + вверх), ASDA_CAP_YAW
import os, re, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_maps as _MAPS
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)
MAP = os.environ.get("ASDA_MAP", "alpen")
OUT = os.environ.get("ASDA_CAP_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/sky_capture.png")
PITCH = float(os.environ.get("ASDA_CAP_PITCH", "18"))
YAW   = float(os.environ.get("ASDA_CAP_YAW", "0"))
_asset = _MAPS.map_asset_name  # ★ единый санитайзер имени карты (asda_maps.map_asset_name)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
# центр карты: среднее bounds всех StaticMeshActor (терраин+объекты)
mn = unreal.Vector(1e12,1e12,1e12); mx = unreal.Vector(-1e12,-1e12,-1e12)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    lvl = _U.level_path(MAP)
    les.load_level(lvl)
    for a in eas.get_all_level_actors():
        if a.get_class().get_name() != "StaticMeshActor": continue
        o, e = a.get_actor_bounds(False)
        mn.x=min(mn.x,o.x-e.x); mn.y=min(mn.y,o.y-e.y); mn.z=min(mn.z,o.z-e.z)
        mx.x=max(mx.x,o.x+e.x); mx.y=max(mx.y,o.y+e.y); mx.z=max(mx.z,o.z+e.z)
    center = unreal.Vector((mn.x+mx.x)/2, (mn.y+mx.y)/2, (mn.z+mx.z)/2 + 2500)
    cap = eas.spawn_actor_from_class(unreal.SceneCapture2D, center)
    cap.set_actor_rotation(unreal.Rotator(0.0, PITCH, YAW), False)   # (roll, pitch, yaw)!
    cc = cap.capture_component2d
    rt = unreal.RenderingLibrary.create_render_target2d(unreal.EditorLevelLibrary.get_editor_world(), 1280, 720, unreal.TextureRenderTargetFormat.RTF_RGBA8)
    cc.set_editor_property("texture_target", rt)
    cc.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    cc.set_editor_property("capture_every_frame", False)
    pps = cc.get_editor_property("post_process_settings")
    pps.set_editor_property("override_auto_exposure_min_brightness", True)
    pps.set_editor_property("override_auto_exposure_max_brightness", True)
    pps.set_editor_property("auto_exposure_min_brightness", 1.0)
    pps.set_editor_property("auto_exposure_max_brightness", 1.0)
    cc.set_editor_property("post_process_settings", pps)
    cc.capture_scene()
    unreal.RenderingLibrary.export_render_target(unreal.EditorLevelLibrary.get_editor_world(), rt, os.path.dirname(OUT), os.path.basename(OUT))
    eas.destroy_actor(cap)
    unreal.log("[CAP] %s -> %s (pos=%s pitch=%.0f yaw=%.0f)" % (MAP, OUT, center, PITCH, YAW))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
