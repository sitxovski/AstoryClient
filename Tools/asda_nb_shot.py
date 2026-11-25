# Copyright (c) 2026 @sitxovski (Discord)
# asda_nb_shot.py — ШАГ 4 превью «нормаль из альбедо»: крупный план каждого образца, «до» и «после».
#
# ★★★ ГЛАВНАЯ ГРАБЛЯ (проверено на этом же скрипте): при запуске через -ExecutePythonScript движок
#   НЕ ТИКАЕТ, пока идёт скрипт, и выходит сразу после него. Поэтому:
#     • take_high_res_screenshot ставит запрос на конец кадра — кадра не будет, файла тоже (0 файлов);
#     • SceneCapture2D.capture_scene() отрабатывает, но ресурс рендер-таргета пустой, а
#       export_render_target молча не пишет ничего (тоже 0 файлов, БЕЗ ошибки в логе).
#   Ничего не «успевает загрузиться» — ни шейдеры, ни стриминг текстур, ни захват неба у SkyLight.
#
#   ⇒ Скрипт работает как МАШИНА СОСТОЯНИЙ на колбэке post-tick: движок крутит кадры, мы считаем
#     их и по счётчику меняем материал/камеру и снимаем. В конце — quit_editor().
#     Запускать НЕ через -ExecutePythonScript, а через -ExecCmds="py <файл>" и БЕЗ -NullRHI:
#
#     UnrealEditor-Cmd.exe <проект> -ExecCmds="py D:/.../asda_nb_shot.py" -nosplash -nosound -stdout
#
# ★ ЧЕСТНОСТЬ СРАВНЕНИЯ: оба кадра образца снимаются ОДНОЙ камерой в ОДНОМ запуске; меняются ровно
#   два числа материала (Flatness / UseRoughMap — см. asda_nb_build_mats.py).
import os

PKG = "/Game/_PreviewNB"
LEVEL = "/Game/_PreviewNB/L_NBPreview"
OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Tools/_out/nb_preview"
CELL_PX = 760

WARMUP_TICKS = 240   # прогрев: компиляция шейдеров, стриминг текстур, захват неба
SETTLE_TICKS = 45    # после смены материала/ракурса — дать пайплайну устояться
MAX_TICKS = 20000    # аварийный предел, чтобы редактор не завис навсегда

SAMPLES = [
    ("stone", "/Game/AsdaAssets/Models/NPC/mdel_stoneman-a_00/SkeletalMeshes/mdel_stoneman-a_00", True),
    ("build", "/Game/AsdaAssets/Models/NPC/mdel_spabuild-a_00/SkeletalMeshes/mdel_spabuild-a_00", True),
    ("tree",  "/Game/AsdaAssets/Models/NPC/mdel_oldtree-a_00/SkeletalMeshes/mdel_oldtree-a_00", True),
    ("armor", "/Game/AsdaAssets/Models/Char/ch1_f_00_chst103_00_00_0/StaticMeshes/ch1_f_00_chst103_00_00_0", False),
    ("mob",   "/Game/AsdaAssets/Models/Mob/mon_b_3_firegolem-a_00/StaticMeshes/mon_b_3_firegolem-a_00", False),
]


def main():
    import unreal
    eal = unreal.EditorAssetLibrary
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

    les.new_level(LEVEL)
    world = ues.get_editor_world()

    # ── свет. Скользящий сверху-сбоку: при плоской нормали даёт ровную заливку, при рельефе — тени.
    sun = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500),
                                     unreal.Rotator(-35.0, 35.0, 0.0))
    sun.light_component.set_editor_property("intensity", 5.0)
    sun.light_component.set_editor_property("light_color", unreal.Color(255, 247, 235, 255))
    eas.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0))
    sky = eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 400))
    sky.light_component.set_editor_property("intensity", 1.0)
    sky.light_component.set_editor_property("real_time_capture", True)

    # стриминг выключен целиком: текстуры сразу в полном разрешении, «размытого» кадра не будет
    for cmd in ("r.TextureStreaming 0", "r.Streaming.FullyLoadUsedTextures 1", "r.ScreenPercentage 100"):
        unreal.SystemLibrary.execute_console_command(world, cmd)

    rt = unreal.RenderingLibrary.create_render_target2d(
        world, CELL_PX, CELL_PX, unreal.TextureRenderTargetFormat.RTF_RGBA8)

    cap = eas.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(0, 0, 0))
    cc = cap.capture_component2d
    cc.set_editor_property("texture_target", rt)
    cc.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    cc.set_editor_property("fov_angle", 42.0)
    cc.set_editor_property("capture_every_frame", True)    # ★ пусть копит кадры сам, пока мы тикаем
    cc.set_editor_property("capture_on_movement", True)

    # ── все образцы расставлены сразу, но видим по одному: лишние скрыты
    actors = {}
    for key, mesh_path, is_skel in SAMPLES:
        mesh = eal.load_asset(mesh_path)
        if not mesh:
            unreal.log_error("нет меша %s" % mesh_path)
            continue
        if is_skel:
            act = eas.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(0, 0, 0))
            comp = act.skeletal_mesh_component
            comp.set_skinned_asset_and_update(mesh)
        else:
            act = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, 0))
            comp = act.static_mesh_component
            comp.set_static_mesh(mesh)
        act.set_actor_label("NB_%s" % key)
        act.set_actor_hidden_in_game(True)
        act.set_is_temporarily_hidden_in_editor(True)
        actors[key] = (act, comp)

    # план съёмки: (ключ, режим)
    plan = [(k, m) for k, _p, _s in SAMPLES for m in ("before", "after")]

    st = {"tick": 0, "step": -1, "next_at": WARMUP_TICKS, "done": 0, "handle": None}

    def show_only(key):
        for k, (a, _c) in actors.items():
            vis = (k == key)
            a.set_actor_hidden_in_game(not vis)
            a.set_is_temporarily_hidden_in_editor(not vis)

    def setup(step):
        key, mode = plan[step]
        act, comp = actors[key]
        show_only(key)
        mic = eal.load_asset("%s/MI_%s_%s" % (PKG, key, mode))
        if not mic:
            unreal.log_error("нет материала MI_%s_%s" % (key, mode))
            return
        for si in range(comp.get_num_materials()):
            comp.set_material(si, mic)
        org, ext = act.get_actor_bounds(False)
        radius = max(ext.x, ext.y, ext.z, 1.0)
        eye = unreal.Vector(org.x - radius * 2.6, org.y - radius * 1.5, org.z + radius * 1.1)
        cap.set_actor_location(eye, False, False)
        cap.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(eye, org), False)
        unreal.log("[nb] готовлю %s/%s габарит=(%.0f,%.0f,%.0f)" % (key, mode, ext.x, ext.y, ext.z))

    def grab(step):
        key, mode = plan[step]
        name = "cell_%s_%s.png" % (key, mode)
        dst = os.path.join(OUT_DIR, name)
        if os.path.exists(dst):
            os.remove(dst)
        cc.capture_scene()
        unreal.RenderingLibrary.export_render_target(world, rt, OUT_DIR, name)
        ok = os.path.exists(dst)
        st["done"] += 1 if ok else 0
        unreal.log("[nb] снято %s -> %s" % (name, "ОК" if ok else "ПУСТО"))

    def finish():
        if st["handle"] is not None:
            unreal.unregister_slate_post_tick_callback(st["handle"])
            st["handle"] = None
        unreal.log("=== nb_shot: кадров сохранено %d из %d ===" % (st["done"], len(plan)))
        unreal.SystemLibrary.quit_editor()

    def on_tick(_dt):
        st["tick"] += 1
        if st["tick"] > MAX_TICKS:
            unreal.log_error("[nb] предел тиков — выхожу")
            finish()
            return
        if st["tick"] < st["next_at"]:
            return
        # снять предыдущий шаг (он уже устоялся) и подготовить следующий
        if st["step"] >= 0:
            grab(st["step"])
        st["step"] += 1
        if st["step"] >= len(plan):
            finish()
            return
        setup(st["step"])
        st["next_at"] = st["tick"] + SETTLE_TICKS

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    unreal.log("=== nb_shot: цикл запущен, прогрев %d кадров ===" % WARMUP_TICKS)


if __name__ == "__main__":
    main()
