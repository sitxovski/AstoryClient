# Copyright (c) 2026 @sitxovski (Discord)
# asda_gacha_quality_check.py — ФАКТИЧЕСКАЯ проверка КАЧЕСТВА кадра автоматов гачи:
#   «серое» (доля пикселей с низкой насыщенностью) и «мыльность» (резкость по Лапласиану).
#
# ★★★ ГРАБЛЯ ПРОЕКТА (память normal-from-albedo-studio, повторно подтверждена в gacha_3d_impl.md):
#   при `-ExecutePythonScript` движок НЕ тикает, пока идёт скрипт, поэтому кадр остаётся от
#   РАННЕГО тика (до компиляции шейдеров) — модели выходят белыми «без текстур». Отсюда:
#   машина состояний на `register_slate_post_tick_callback`, запуск БЕЗ `-NullRHI`, и перед
#   КАЖДЫМ экспортом обязательный явный `capture_scene()`.
#
# Снимаемые варианты (переключаем свойства самого актора — они UPROPERTY):
#   ss1_taa — супер-сэмплинг 1× и временное сглаживание ВКЛ (состояние до правки);
#   ss1     — 1×, временное сглаживание ВЫКЛ;
#   ss2     — 2× (дефолт после правки), временное сглаживание ВЫКЛ.
#
# Запуск: run_headless_rhi.bat @@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Tools/asda_gacha_quality_check.py
#   (через -ExecutePythonScript БЕЗ -NullRHI: тик даёт как раз slate-колбэк).
import os

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/gacha_3d"
LEVEL = "/Temp/L_GachaQuality"
MACHINES = 2                 # столько записей в premium_gatya_data.bin
WARMUP_TICKS = 420           # компиляция шейдеров + стриминг текстур
SETTLE_TICKS = 120           # после смены настроек капчера / пересоздания RT
MAX_TICKS = 40000

VARIANTS = [("ss1_taa", 1.0, True), ("ss1", 1.0, False), ("ss2", 2.0, False)]
# (замер 2026-08-17: детализация узора Lap² = 3976 / 6455 / 3299 ⇒ дефолт актора — 1× без TAA)


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
    act.set_actor_label("GachaQuality")
    act.build_machines(MACHINES)
    act.play_clip_all(1, True)
    for c in act.get_components_by_class(unreal.SkeletalMeshComponent):
        try:
            c.set_update_animation_in_editor(True)
        except Exception as e:
            unreal.log_warning("[гача-качество] анимация в редакторе не включена: %s" % e)

    # состав слотов материалов — печатаем, чтобы «серое» было видно ещё до картинки
    for c in act.get_components_by_class(unreal.SkeletalMeshComponent):
        names = []
        for i in range(c.get_num_materials()):
            mi = c.get_material(i)
            par = mi.get_editor_property("parent") if isinstance(mi, unreal.MaterialInstanceConstant) else None
            names.append("%d:%s(%s)" % (i, mi.get_name() if mi else "НЕТ",
                                        par.get_name() if par else "—"))
        unreal.log("[гача-качество] %s слоты: %s" % (c.get_name(), "; ".join(names)))

    st = {"tick": 0, "step": -1, "next_at": WARMUP_TICKS, "ok": 0, "handle": None}

    def apply(step):
        name, ss, taa = VARIANTS[step]
        act.set_editor_property("render_supersample", ss)
        act.set_editor_property("temporal_aa_in_capture", taa)   # UPROPERTY bTemporalAAInCapture
        act.sync_render_target_to_viewport()      # пересоздаст RT под новую кратность
        act.build_machines(MACHINES)              # переприменит ShowFlags/камеру на оба капчера
        act.play_clip_all(1, True)
        for c in act.get_components_by_class(unreal.SkeletalMeshComponent):
            try:
                c.set_update_animation_in_editor(True)
            except Exception:
                pass
        rt = act.get_render_target()
        unreal.log("[гача-качество] вариант %s: супер-сэмплинг ×%.1f, временное сглаживание=%s, RT %dx%d"
                   % (name, ss, taa, rt.size_x if rt else -1, rt.size_y if rt else -1))

    def grab(step):
        name = VARIANTS[step][0]
        for c in act.get_components_by_class(unreal.SceneCaptureComponent2D):
            c.capture_scene()                     # ★ без этого экспортируется старый кадр
        # ★ Кадр-МАСКА снимается ОБЯЗАТЕЛЬНО: купол стал полупрозрачным, а маска непрозрачности
        #   у транслюцентной геометрии считается иначе — без проверки можно получить дыру в окне.
        # ★ RT ОДИН: альфа лежит в нём же (капчер-маска убран, см. gacha_alpha_composite.md).
        for tgt, suffix in ((act.get_render_target(), "color"),):
            fn = "quality_%s_%s.png" % (name, suffix)
            dst = os.path.join(OUT_DIR, fn)
            if os.path.exists(dst):
                os.remove(dst)
            unreal.RenderingLibrary.export_render_target(world, tgt, OUT_DIR, fn)
            ok = os.path.exists(dst)
            st["ok"] += 1 if ok else 0
            unreal.log("[гача-качество] снято %s -> %s" % (fn, "ОК" if ok else "★ ПУСТО"))

    def finish():
        if st["handle"] is not None:
            unreal.unregister_slate_post_tick_callback(st["handle"])
            st["handle"] = None
        unreal.log("=== гача-качество: сохранено %d из %d кадров ===" % (st["ok"], 2 * len(VARIANTS)))
        unreal.SystemLibrary.quit_editor()

    def step_tick():
        st["tick"] += 1
        if st["tick"] > MAX_TICKS:
            unreal.log_error("[гача-качество] предел тиков")
            finish()
            return
        if st["tick"] < st["next_at"]:
            return
        if st["step"] >= 0:
            grab(st["step"])
        st["step"] += 1
        if st["step"] >= len(VARIANTS):
            finish()
            return
        apply(st["step"])
        st["next_at"] = st["tick"] + SETTLE_TICKS

    def on_tick(_dt):
        # ★ исключение внутри slate-колбэка движок ГЛОТАЕТ: шаг не увеличится и цикл зациклится.
        try:
            step_tick()
        except Exception:
            import traceback
            unreal.log_error("[гача-качество] ошибка в шаге:\n" + traceback.format_exc())
            finish()

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    unreal.log("=== гача-качество: цикл запущен, прогрев %d кадров ===" % WARMUP_TICKS)


if __name__ == "__main__":
    try:
        main()
    except Exception:
        import traceback
        import unreal
        unreal.log_error("[гача-качество] СБОЙ:\n" + traceback.format_exc())
        unreal.SystemLibrary.quit_editor()
