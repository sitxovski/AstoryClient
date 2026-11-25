# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_check_propagate_alpha.py — А/Б-проверка: не портит ли `r.PostProcessing.PropagateAlpha`
# КАРТИНКУ МИРА (не сцены гачи).
#
# ★ Зачем. Настройка включена проектно (`Config/DefaultEngine.ini`,
#   `[/Script/Engine.RendererSettings] r.PostProcessing.PropagateAlpha=1`) ради альфы кадра
#   премиум-гачи. В движке она описана как «Enforce alpha in scene color (overriding
#   r.SceneColorFormat if necessary) and propagate it through the renderer's post-processing
#   chain» (`PostProcessing.cpp:189-193`) — то есть может СМЕНИТЬ формат scene color. Значит
#   обязана быть проверена на обычном мире, а не только на гаче.
#
# ★ Переменная НЕ read-only (флаги `ECVF_RenderThreadSafe`), поэтому А/Б делается в ОДНОМ
#   прогоне: кадр при 1, кадр при 0, попиксельное сравнение RGB — офлайн, `Tools/asda_gacha_*`.
#
# Запуск: run_headless_rhi_tick.bat asda_check_propagate_alpha.py
import os

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/gacha_3d"
LEVEL = os.environ.get("GACHA_PROBE_LEVEL", "/Game/Map/Login")
WARMUP_TICKS = 600
SETTLE_TICKS = 200
MAX_TICKS = 200000
RT_SIZE = (1280, 720)


def main():
    import unreal
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if not os.path.isdir(OUT_DIR):
        os.makedirs(OUT_DIR)
    if not les.load_level(LEVEL):
        unreal.log_error("[альфа-мир] не открылся уровень %s" % LEVEL)
        return
    world = ues.get_editor_world()
    for cmd in ("r.TextureStreaming 0", "r.Streaming.FullyLoadUsedTextures 1", "r.ScreenPercentage 100"):
        unreal.SystemLibrary.execute_console_command(world, cmd)

    # ★ ГРАБЛЯ: по умолчанию create_render_target2d делает RGBA16f, и export_render_target
    #   пишет EXR под именем .png. Для побайтового сравнения нужен RTF_RGBA8.
    rt = unreal.RenderingLibrary.create_render_target2d(
        world, RT_SIZE[0], RT_SIZE[1], unreal.TextureRenderTargetFormat.RTF_RGBA8)

    # Камера: берём первую камеру уровня, иначе — вид с высоты на центр сцены (позиция уровня,
    # а не выдуманная: центр берётся по габаритам всех статик-мешей).
    cams = list(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CameraActor))
    if cams:
        loc, rot = cams[0].get_actor_location(), cams[0].get_actor_rotation()
    else:
        acts = list(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.StaticMeshActor))
        if not acts:
            unreal.log_error("[альфа-мир] на уровне нет ни камер, ни статик-мешей — сравнивать нечего")
            return
        c = unreal.Vector(0, 0, 0)
        for a in acts:
            c = c + a.get_actor_location()
        c = c / float(len(acts))
        loc = c + unreal.Vector(-1200, 0, 700)
        rot = unreal.Rotator(0, -20, 0)

    cap_actor = eas.spawn_actor_from_class(unreal.SceneCapture2D, loc, rot)
    cap_actor.set_actor_label("PropagateAlphaProbe")
    cap = cap_actor.capture_component2d
    cap.set_editor_property("texture_target", rt)
    cap.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    cap.set_editor_property("capture_every_frame", True)
    unreal.log("[альфа-мир] камера (%.0f, %.0f, %.0f), актор-камер %d" % (loc.x, loc.y, loc.z, len(cams)))

    # ★ Третий шаг — КОНТРОЛЬ: то же значение, что на первом. Без него разницу кадров нельзя
    #   отличить от обычного дрожания рендера между двумя захватами (TAA, тени, стриминг).
    PLAN = [("pa1", 1), ("pa0", 0), ("pa1b", 1)]
    st = {"tick": 0, "step": -1, "next_at": WARMUP_TICKS, "ok": 0, "handle": None, "busy": False}

    def finish():
        if st["handle"] is not None:
            unreal.unregister_slate_post_tick_callback(st["handle"])
            st["handle"] = None
        # ★ вернуть проектное значение, чтобы прогон не оставил редактор в чужом состоянии
        unreal.SystemLibrary.execute_console_command(world, "r.PostProcessing.PropagateAlpha 1")
        try:
            eas.destroy_actor(cap_actor)
        except Exception:
            pass
        unreal.log("=== альфа-мир: сохранено %d из %d кадров ===" % (st["ok"], len(PLAN)))
        unreal.SystemLibrary.quit_editor()

    def step_tick():
        st["tick"] += 1
        if st["tick"] > MAX_TICKS:
            unreal.log_error("[альфа-мир] предел тиков")
            finish()
            return
        if st["tick"] < st["next_at"]:
            return
        if st["step"] >= 0:
            name = PLAN[st["step"]][0]
            cap.capture_scene()
            fn = "pa_%s_color.png" % name
            dst = os.path.join(OUT_DIR, fn)
            if os.path.exists(dst):
                os.remove(dst)
            unreal.RenderingLibrary.export_render_target(world, rt, OUT_DIR, fn)
            st["ok"] += 1 if os.path.exists(dst) else 0
            unreal.log("[альфа-мир] кадр %s: %s" % (fn, "ok" if os.path.exists(dst) else "НЕ СОХРАНЁН"))
        st["step"] += 1
        if st["step"] >= len(PLAN):
            finish()
            return
        name, val = PLAN[st["step"]]
        unreal.SystemLibrary.execute_console_command(world, "r.PostProcessing.PropagateAlpha %d" % val)
        unreal.log("[альфа-мир] шаг %s: r.PostProcessing.PropagateAlpha %d" % (name, val))
        st["next_at"] = st["tick"] + SETTLE_TICKS

    def on_tick(_dt):
        if st["busy"]:
            return
        st["busy"] = True
        try:
            step_tick()
        except Exception:
            import traceback
            unreal.log_error("[альфа-мир] ошибка:\n" + traceback.format_exc())
            finish()
        finally:
            st["busy"] = False

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    unreal.log("=== альфа-мир: цикл запущен, уровень %s ===" % LEVEL)


if __name__ == "__main__":
    try:
        main()
    except Exception:
        import traceback
        import unreal
        unreal.log_error("[альфа-мир] ошибка старта:\n" + traceback.format_exc())
        unreal.SystemLibrary.quit_editor()
