# Copyright (c) 2026 @sitxovski (Discord)
# _probe_capture.py — минимальная диагностика SceneCapture2D в headless: почему кадр чёрный.
import os
OUT = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/blend_rollout"


def main():
    import unreal
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    les.load_level("/Game/Map/alpen/alpen")
    world = ues.get_editor_world()
    unreal.log("[проба] мир=%s" % (world.get_name() if world else "НЕТ"))

    cube = unreal.load_asset("/Engine/BasicShapes/Cube")
    a = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, 12000))
    a.static_mesh_component.set_static_mesh(cube)
    a.set_actor_scale3d(unreal.Vector(20, 20, 20))
    sun = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 12500))
    sun.set_actor_rotation(unreal.Rotator(-40, -35, 0), False)
    sun.light_component.set_intensity(6.0)
    sky = eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 12500))
    sky.light_component.set_intensity(1.0)

    rt = unreal.RenderingLibrary.create_render_target2d(world, 256, 256,
                                                        unreal.TextureRenderTargetFormat.RTF_RGBA8)
    cap = eas.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(0, -6000, 12500))
    cap.set_actor_rotation(unreal.Rotator(-10, 90, 0), False)
    cc = cap.capture_component2d
    cc.set_editor_property("texture_target", rt)
    cc.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    for prop in ("b_always_persist_rendering_state", "always_persist_rendering_state"):
        try: cc.set_editor_property(prop, True)
        except Exception: pass
    for prop in ("b_capture_every_frame", "capture_every_frame"):
        try: cc.set_editor_property(prop, True)
        except Exception: pass

    st = {"t": 0, "h": None}

    def grab(tag):
        cc.capture_scene()
        fn = "probe_%s.png" % tag
        dst = os.path.join(OUT, fn)
        if os.path.exists(dst):
            os.remove(dst)
        unreal.RenderingLibrary.export_render_target(world, rt, OUT, fn)
        try:
            px = unreal.RenderingLibrary.read_render_target_pixel(world, rt, 128, 128)
            unreal.log("[проба] %s центр=%s файл=%s" % (tag, px, os.path.exists(dst)))
        except Exception as e:
            unreal.log("[проба] %s чтение пикселя не вышло: %s" % (tag, e))

    def on_tick(_dt):
        st["t"] += 1
        if st["t"] in (60, 180, 360, 600):
            grab("t%d" % st["t"])
        if st["t"] > 620:
            unreal.unregister_slate_post_tick_callback(st["h"])
            unreal.SystemLibrary.quit_editor()

    st["h"] = unreal.register_slate_post_tick_callback(on_tick)
    unreal.log("[проба] цикл запущен")


# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
