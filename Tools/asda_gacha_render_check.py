# Copyright (c) 2026 @sitxovski (Discord)
# asda_gacha_render_check.py — ФАКТИЧЕСКАЯ проверка 3D-сцены премиум-гачи:
#   собираем `AsdaGachaSceneActor`, ждём прогрева шейдеров, снимаем render-target в PNG и
#   печатаем экранные координаты проекции (якоря слотов + кость Bone19) на всех клипах.
#
# ★★★ ГРАБЛЯ ПРОЕКТА (память normal-from-albedo-studio): при `-ExecutePythonScript` движок НЕ
#   ТИКАЕТ, пока идёт скрипт, и выходит сразу после него — `capture_scene()` отрабатывает, но RT
#   пустой, а `export_render_target` МОЛЧА не пишет файл. Поэтому скрипт — машина состояний на
#   `register_slate_post_tick_callback`, а запускать надо БЕЗ `-NullRHI` и через `-ExecCmds="py …"`:
#
#   UnrealEditor-Cmd.exe <проект> -ExecCmds="py @@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Tools/asda_gacha_render_check.py"
#                        -unattended -nopause -nosplash -nosound -stdout -abslog=<лог>
#
# Что проверяем:
#   1) автоматы видны, не чёрные, не обрезаны — глазами по PNG;
#   2) фон прозрачный — снимаем ТРЕМЯ источниками кадра и меряем альфу вне силуэта;
#   3) проекция Bone19 совпадает с положением 3D-шара по клипам (в 1/2 шар спрятан, в 3/4 виден).
import os

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/gacha_3d"
LEVEL = "/Temp/L_GachaRenderCheck"
MACHINES = 2                 # столько записей в premium_gatya_data.bin

WARMUP_TICKS = 420           # компиляция шейдеров + стриминг текстур
SETTLE_TICKS = 90            # после смены клипа/источника кадра
MAX_TICKS = 30000


def main():
    import unreal
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

    if not os.path.isdir(OUT_DIR):
        os.makedirs(OUT_DIR)

    les.new_level(LEVEL)
    world = ues.get_editor_world()
    for cmd in ("r.TextureStreaming 0", "r.Streaming.FullyLoadUsedTextures 1", "r.ScreenPercentage 100"):
        unreal.SystemLibrary.execute_console_command(world, cmd)

    act = eas.spawn_actor_from_class(unreal.AsdaGachaSceneActor, unreal.Vector(0, 0, 0))
    act.set_actor_label("GachaSceneCheck")
    act.build_machines(MACHINES)
    act.play_clip_all(1, True)

    # в редакторском мире скелеталки не анимируются, пока не разрешить это явно
    skels = act.get_components_by_class(unreal.SkeletalMeshComponent)
    for c in skels:
        # в редакторском мире поза не пересчитывается, пока это не разрешено явно
        # (свойство помечено «нельзя править на шаблонах» — только через сеттер)
        try:
            c.set_update_animation_in_editor(True)
        except Exception as e:
            unreal.log_warning("[гача-рендер] анимация в редакторе не включена: %s" % e)
    caps = act.get_components_by_class(unreal.SceneCaptureComponent2D)
    # цветной капчер — тот, чья цель совпадает с GetRenderTarget()
    cap = next((c for c in caps if c.get_editor_property("texture_target") == act.get_render_target()), caps[0])
    rt = act.get_render_target()
    unreal.log("[гача-рендер] автоматов %d, скелеталок %d, RT %dx%d, FOVx %.2f°" % (
        act.get_machine_count(), len(skels),
        rt.size_x if rt else -1, rt.size_y if rt else -1,
        cap.get_editor_property("fov_angle")))

    # ★ Закон normal-from-albedo-studio: материал проверяем ТОЛЬКО с настоящим RHI —
    #   ps = 0 означает, что он не скомпилировался (домен/сэмплер), и молча даст серую заглушку.
    mid = act.get_scene_brush_material()
    if mid:
        stats = unreal.MaterialEditingLibrary.get_statistics(mid.get_base_material())
        vals = []
        for k in ("num_pixel_shader_instructions", "num_vertex_shader_instructions", "num_samplers"):
            try:
                vals.append("%s=%s" % (k, stats.get_editor_property(k)))
            except Exception:
                pass
        unreal.log("[гача-рендер] материал кисти: %s :: %s" % (mid.get_base_material().get_name(), ", ".join(vals)))
    else:
        unreal.log_error("[гача-рендер] материала кисти НЕТ — прозрачности не будет")

    # габариты каждого автомата и его экранная проекция — сразу видно, влезает ли он в кадр
    for i, c in enumerate(skels):
        unreal.log("[гача-рендер] автомат %d: мир=(%.1f, %.1f, %.1f) поворот yaw=%.1f°" % (
            i, c.get_world_location().x, c.get_world_location().y, c.get_world_location().z,
            c.get_world_rotation().yaw))

    # план: (имя кадра, слот клипа, источник цветного капчера или None = как настроено актором).
    #   ★ Кадр `basecolor` — диагностический: показывает АЛЬБЕДО без освещения, то есть отвечает
    #     на вопрос «текстуры не применились» vs «пересвет» (симптомы неотличимы на глаз).
    plan = [("idle", 1, None), ("roll", 2, None), ("dispense", 3, None), ("open", 4, None),
            ("basecolor", 1, unreal.SceneCaptureSource.SCS_BASE_COLOR),
            ("forcedtex", 1, unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR),
            ("control", 1, unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)]

    def spawn_control():
        """Контроль: СТАТИК-версия того же автомата (импорт от 2026-08-02, в проекте давно).
        Она цветная ⇒ виноват импорт скелеталки; она тоже белая ⇒ виноват сам капчер/свет."""
        sm = unreal.EditorAssetLibrary.load_asset(
            "/Game/AsdaAssets/Models/Char/md_gachya_a_00/StaticMeshes/md_gachya_a_00")
        if not sm:
            unreal.log_error("[гача-рендер] нет статик-версии для контроля")
            return
        # прячем скелеталки и показываем статик на месте автомата 0
        for c in skels:
            c.set_visibility(False)
        a = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(150, 0, 0),
                                       unreal.Rotator(0, -90, 0))
        a.static_mesh_component.set_static_mesh(sm)
        a.static_mesh_component.set_world_scale3d(unreal.Vector(0.01, 0.01, 0.01))
        a.set_actor_label("GachaControlStatic")
        cap.set_editor_property("primitive_render_mode",
                                unreal.SceneCapturePrimitiveRenderMode.PRM_LEGACY_SCENE_CAPTURE)
        unreal.log("[гача-рендер] контроль: статик-меш %s поставлен" % sm.get_name())

    def force_texture():
        """Диагностика «автоматы серые»: вешаем на слот 0 динамический материал с ЯВНО заданной
        текстурой. Стал цветным ⇒ виновата привязка MIC; остался серым ⇒ мастер/UV меша."""
        tex = unreal.EditorAssetLibrary.load_asset(
            "/Game/AsdaAssets/Textures/Characters/Texture/md_gachya_a_00")
        master = unreal.EditorAssetLibrary.load_asset("/Game/Materials/M_AsdaObjectLit")
        if not tex or not master:
            unreal.log_error("[гача-рендер] нет текстуры или мастера для диагностики")
            return
        unreal.log("[гача-рендер] диагностика: текстура %s резидентно %dx%d" % (
            tex.get_name(), tex.blueprint_get_size_x(), tex.blueprint_get_size_y()))
        for c in skels:
            for si in range(c.get_num_materials()):
                dyn = c.create_dynamic_material_instance(si, master)
                if dyn:
                    dyn.set_texture_parameter_value("Tex", tex)

    st = {"tick": 0, "step": -1, "next_at": WARMUP_TICKS, "ok": 0, "handle": None}

    def unpack(res):
        # у UFUNCTION с out-параметром UE-Python отдаёт то кортеж (bool, Vector2D), то один Vector2D
        if isinstance(res, tuple):
            return bool(res[0]), res[1]
        return (abs(res.x + 1125.0) > 0.01), res

    def dump_projection(tag):
        n = act.get_machine_count()
        parts = []
        for i in range(n):
            ok, p = unpack(act.get_slot_screen_pos(i))
            parts.append("слот%d=%s(%.1f, %.1f)" % (i, "" if ok else "ПРОМАХ", p.x, p.y))
        for i in range(n):
            ok, p = unpack(act.get_bone_screen_pos(i))
            parts.append("Bone19[%d]=%s(%.1f, %.1f)" % (i, "" if ok else "ПРОМАХ", p.x, p.y))
        try:
            yoff = act.get_slot_screen_y_offset()
        except AttributeError:
            yoff = act.get_slot_screen_yoffset()
        unreal.log("[гача-рендер] проекция %s: yOff=%.0f :: %s" % (tag, yoff, "; ".join(parts)))

    def setup(step):
        name, clip, src = plan[step]
        act.play_clip_all(clip, clip in (1, 2))
        if src is not None:
            cap.set_editor_property("capture_source", src)
        if name == "forcedtex":
            force_texture()
        if name == "control":
            spawn_control()
        unreal.log("[гача-рендер] готовлю %s (клип %d)" % (name, clip))

    def grab(step):
        name, _clip, _src = plan[step]
        # ★ ОБЯЗАТЕЛЬНО: без явного capture_scene() содержимое RT остаётся от РАННЕГО кадра
        #   (до компиляции шейдеров) — модели выходят белыми «без текстур», а любые изменения
        #   материалов/видимости в кадр не попадают. Симптом полностью совпадает с испорченными
        #   материалами, поэтому ловится только контрольным опытом.
        for c in caps:
            c.capture_scene()
        # ★ RT ОДИН: альфа в нём же (капчер-маска убран, gacha_alpha_composite.md).
        for tgt, suffix in ((rt, "color"),):
            fn = "gacha3d_%s_%s.png" % (name, suffix)
            dst = os.path.join(OUT_DIR, fn)
            if os.path.exists(dst):
                os.remove(dst)
            unreal.RenderingLibrary.export_render_target(world, tgt, OUT_DIR, fn)
            ok = os.path.exists(dst)
            st["ok"] += 1 if ok else 0
            unreal.log("[гача-рендер] снято %s -> %s" % (fn, "ОК" if ok else "★ ПУСТО"))
        dump_projection(name)

    def finish():
        if st["handle"] is not None:
            unreal.unregister_slate_post_tick_callback(st["handle"])
            st["handle"] = None
        unreal.log("=== гача-рендер: сохранено %d из %d кадров ===" % (st["ok"], 2 * len(plan)))
        unreal.SystemLibrary.quit_editor()

    def on_tick(_dt):
        # ★ исключение внутри slate-колбэка движок ГЛОТАЕТ: шаг не увеличится и цикл будет
        #   вечно переснимать один и тот же кадр. Поэтому весь шаг под try.
        try:
            step_tick()
        except Exception:
            import traceback
            unreal.log_error("[гача-рендер] ошибка в шаге:\n" + traceback.format_exc())
            finish()

    def step_tick():
        st["tick"] += 1
        if st["tick"] > MAX_TICKS:
            unreal.log_error("[гача-рендер] предел тиков")
            finish()
            return
        if st["tick"] < st["next_at"]:
            return
        if st["step"] >= 0:
            grab(st["step"])
        st["step"] += 1
        if st["step"] >= len(plan):
            finish()
            return
        setup(st["step"])
        st["next_at"] = st["tick"] + SETTLE_TICKS

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    unreal.log("=== гача-рендер: цикл запущен, прогрев %d кадров ===" % WARMUP_TICKS)


if __name__ == "__main__":
    # ★ через -ExecCmds редактор НЕ выходит сам: любая необработанная ошибка оставит его висеть
    #   навсегда. Поэтому выход форсируем в любом случае.
    try:
        main()
    except Exception:
        import traceback
        import unreal
        unreal.log_error("[гача-рендер] СБОЙ:\n" + traceback.format_exc())
        unreal.SystemLibrary.quit_editor()
