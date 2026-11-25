# Copyright (c) 2026 @sitxovski (Discord)
# asda_gacha_color_check.py — АБСОЛЮТНЫЙ замер ЦВЕТА сцены премиум-гачи
# (жалоба владельца: «бледные, пересвеченные, потеряна насыщенность и контраст»).
#
# ★ ЧЕМ ОТЛИЧАЕТСЯ ОТ `asda_gacha_light_check2.py`. Тот доказывал нейтральность цепочки
#   «SceneCapture → экспозиция → тонмаппер → RTF_RGBA8» по квадам ТЕНИ, то есть по ПОЧТИ СЕРОЙ
#   текстуре (`gachya_shadow`, ядро RGB 34,33,33). Любое искажение НАСЫЩЕННОСТИ на сером
#   невидимо по построению — именно эта дыра и осталась незакрытой.
#   Здесь в кадр ставится ЭТАЛОННАЯ МИШЕНЬ из 64 плашек с ИЗВЕСТНЫМИ sRGB-байтами
#   (`RawAssets/Textures/Probe/T_GachaColorChart.png`; в ней насыщенный оранжевый корпуса,
#   жёлтая окантовка и фиолетово-розовые цвета шара — взяты прямо из текстур автомата).
#   Сравнение «байты мишени ↔ байты кадра» — АБСОЛЮТНОЕ и НЕЗАВИСИМОЕ: коридор не считается
#   по той же формуле, что и кадр.
#
# ★ ЭТАЛОН ДЛЯ САМИХ АВТОМАТОВ. Множитель к текстуре у оригинала тождественно 1.0
#   (неосвещённый вершинный шейдер `g_pVtxShaderB/C`, `mov oD0.xyz, c95.w`; полный вывод —
#   в шапке `asda_gacha_light_node.py`). Значит кадр обязан быть РОВНО текстурой, и любая
#   разница «кадр ↔ мишень» — это ошибка цепочки, а не освещения.
#
# Уровень — БОЕВОЙ `/Game/Map/Login`, спаун — как у подсистемы (`EnsureSceneActor`).
#   ★ 15.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 16): умолчания /Game/Map/Login больше нет — уровень только явно (GACHA_PROBE_LEVEL).
#
# ★★★ ГРАБЛЯ: без RHI кадра нет, а при `-ExecutePythonScript` редактор гасится сразу после
#   скрипта. Запускать: run_headless_rhi_tick.bat <этот файл>
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # соседние asda_* модули

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/gacha_3d"
# ★ 15.09.2026 (раздел 16): БЫЛО умолчание "/Game/Map/Login" — зонд ставил акторы в боевой уровень логина (свет владельца).
#   Теперь без GACHA_PROBE_LEVEL=<путь уровня> — явная ошибка; уровень логина — только с ASDA_ALLOW_LOGIN_WRITE=1.
LEVEL = os.environ.get("GACHA_PROBE_LEVEL", "")
MACHINES = 1                 # один автомат: два квада тени перекрываются и портят замер
WARMUP_TICKS = 480
SETTLE_TICKS = 150
MAX_TICKS = 200000

CHART_PNG = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/RawAssets/Textures/Probe/T_GachaColorChart.png"
CHART_PKG = "/Game/AsdaAssets/Probe"
CHART_NAME = "T_GachaColorChart"
MACHINE_MASTER = "/Game/Materials/M_AsdaGachaMachine"
PLANE_MESH = "/Engine/BasicShapes/Plane"
CHART_DIST = 260.0           # мишень перед камерой; подобрано так, чтобы влезала целиком
CHART_SCALE = 1.4            # плоскость Engine/Plane = 100 ед. ⇒ 140 ед. стороны


def main():
    import unreal
    # ★ 15.09.2026 (раздел 16): уровень — только явный; логин — только с разрешения владельца (зонд ставит акторы в уровень).
    if not LEVEL:
        raise RuntimeError("[гача-цвет] не задан уровень: GACHA_PROBE_LEVEL=<путь уровня> (умолчания /Game/Map/Login больше нет)")
    import asda_login_guard as _LG   # Tools уже в sys.path (строка у импортов модуля)
    if _LG.is_login_owned_package(LEVEL):
        _LG.forbid_login_write("asda_gacha_color_check.py", "акторы зонда в уровень %s" % LEVEL)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if not os.path.isdir(OUT_DIR):
        os.makedirs(OUT_DIR)

    if not les.load_level(LEVEL):
        unreal.log_error("[гача-цвет] не открылся уровень %s" % LEVEL)
        # ★ 16.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 18): старт сорван — колбэк тика не зарегистрирован, редактор гасится здесь.
        #   БЫЛО: return без quit_editor — редактор висел процессом (раздел 17.9, п.3).
        unreal.SystemLibrary.quit_editor()
        return
    world = ues.get_editor_world()
    for cmd in ("r.TextureStreaming 0", "r.Streaming.FullyLoadUsedTextures 1", "r.ScreenPercentage 100"):
        unreal.SystemLibrary.execute_console_command(world, cmd)

    # ── мишень: импорт текстуры БЕЗ сжатия и БЕЗ мипов, sRGB ────────────────────────────────
    chart_path = "%s/%s" % (CHART_PKG, CHART_NAME)
    task = unreal.AssetImportTask()
    task.filename = CHART_PNG
    task.destination_path = CHART_PKG
    task.destination_name = CHART_NAME
    task.automated = True
    task.replace_existing = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    chart = unreal.load_asset(chart_path)
    if not chart:
        unreal.log_error("[гача-цвет] мишень не импортировалась: %s" % chart_path)
        # ★ 16.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 18): старт сорван — колбэк тика не зарегистрирован, редактор гасится здесь.
        #   БЫЛО: return без quit_editor — редактор висел процессом (раздел 17.9, п.3).
        unreal.SystemLibrary.quit_editor()
        return
    # TC_EDITOR_ICON = RGBA8 без сжатия; мипы выключены, иначе плашки размылись бы на границах.
    chart.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    chart.set_editor_property("srgb", True)
    chart.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    chart.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    chart.set_editor_property("never_stream", True)
    unreal.EditorAssetLibrary.save_loaded_asset(chart)

    # ── сцена гачи ──────────────────────────────────────────────────────────────────────────
    act = eas.spawn_actor_from_class(unreal.AsdaGachaSceneActor, unreal.Vector(0, 0, 200000))
    act.set_actor_label("GachaColorProbe")
    act.build_machines(MACHINES)
    act.play_clip_all(1, True)
    for c in act.get_components_by_class(unreal.SkeletalMeshComponent):
        try:
            c.set_update_animation_in_editor(True)
        except Exception:
            pass

    caps = list(act.get_components_by_class(unreal.SceneCaptureComponent2D))

    # ── плоскость с мишенью: строго перед камерой, нормалью в камеру ────────────────────────
    cam = caps[0]
    eye = cam.get_world_location()
    fwd = cam.get_forward_vector()
    chart_actor = eas.spawn_actor_from_class(
        unreal.StaticMeshActor, eye + fwd * CHART_DIST,
        unreal.MathLibrary.make_rot_from_z(fwd * -1.0))
    chart_actor.set_actor_label("GachaColorChart")
    smc = chart_actor.static_mesh_component
    # ★ StaticMeshActor спавнится СТАТИЧНЫМ: без Movable ни масштаб, ни видимость менять нельзя.
    smc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    smc.set_static_mesh(unreal.load_asset(PLANE_MESH))
    smc.set_world_scale3d(unreal.Vector(CHART_SCALE, CHART_SCALE, CHART_SCALE))
    smc.set_cast_shadow(False)
    mid = smc.create_dynamic_material_instance(0, unreal.load_asset(MACHINE_MASTER))
    mid.set_texture_parameter_value("Tex", chart)
    smc.set_visibility(False)
    unreal.log("[гача-цвет] мишень: глаз=%s вперёд=%s позиция=%s"
               % (eye, fwd, chart_actor.get_actor_location()))

    # ★★★ ГРАБЛЯ РЕ-ЭНТРАНТНОСТИ: `MaterialEditingLibrary.recompile_material` внутри показывает
    #   прогресс и ПРОКРУЧИВАЕТ Slate, из-за чего наш post-tick колбэк вызывается ПОВТОРНО прямо
    #   посреди пересборки — машина состояний уезжала на шаг вперёд и кадры сохранялись под
    #   чужими именами (в `cc_after_mach` оказывалась мишень). Флаг `busy` это запрещает.
    st = {"tick": 0, "step": -1, "next_at": WARMUP_TICKS, "ok": 0, "handle": None, "busy": False}
    machines = list(act.get_components_by_class(unreal.SkeletalMeshComponent))

    # ★ ГРАБЛЯ: `show_only_actors` — свойство ШАБЛОНА компонента, Python его править не даёт
    #   («cannot be edited on templates»). Правильный путь — метод `show_only_actor_components`,
    #   он дописывает компоненты актора в `ShowOnlyComponents`. Дальше переключаем ВИДИМОСТЬ.
    for cp in caps:
        cp.show_only_component(smc)

    def show(machine_on, chart_on):
        for c in machines:
            c.set_visibility(machine_on)
        smc.set_visibility(chart_on)

    def grab(name):
        for cp in caps:
            cp.capture_scene()
        # ★ RT ОДИН: альфа лежит в нём же (капчер-маска убран, см. gacha_alpha_composite.md).
        for tgt, suffix in ((act.get_render_target(), "color"),):
            fn = "cc_%s_%s.png" % (name, suffix)
            dst = os.path.join(OUT_DIR, fn)
            if os.path.exists(dst):
                os.remove(dst)
            unreal.RenderingLibrary.export_render_target(world, tgt, OUT_DIR, fn)
            st["ok"] += 1 if os.path.exists(dst) else 0
            unreal.log("[гача-цвет] кадр %s: %s" % (fn, "ok" if os.path.exists(dst) else "НЕ СОХРАНЁН"))

    # ★★★ ДВА СОСТОЯНИЯ В ОДНОМ ПРОГОНЕ.
    #   `before` — мастера как лежат на диске (у владельца сейчас плоский множитель ×0.6);
    #   `after`  — те же мастера, ПЕРЕСОБРАННЫЕ В ПАМЯТИ по эталону (`Emissive = Tex`).
    #   Пересборка в памяти нужна потому, что при открытом редакторе владельца `.uasset` занят
    #   и сохранить правку на диск нельзя (`MoveFile ... Error Code 32`), а померить — можно.
    def rebuild_masters():
        import asda_build_gacha_machine_material as MACHINE
        import asda_build_gacha_glass_material as GLASS
        import asda_build_gacha_shadow_material as SHADOW
        for mod in (MACHINE, GLASS, SHADOW):
            mod.build_master(save=False)
        # ★ ГРАБЛЯ: у мастера снесены и заново созданы все узлы, поэтому СТАРЫЙ MID держит
        #   протухший набор параметров и плашка перестаёт рисоваться вовсе (кадр пустой).
        #   MID пересоздаём и заново вешаем мишень.
        new_mid = smc.create_dynamic_material_instance(0, unreal.load_asset(MACHINE_MASTER))
        new_mid.set_texture_parameter_value("Tex", chart)
        # ★ Материалам автоматов пересоздание не нужно: там `MaterialInstanceConstant` с
        #   родителем-мастером, и кадр `after_mach` уже показывает изменение (замер в отчёте).
        unreal.log("[гача-цвет] мастера пересобраны В ПАМЯТИ (Emissive = Tex, купол/тень односторонние)")

    PLAN = [("before_mach", True, False, None),
            ("before_chart", False, True, None),
            ("after_mach", True, False, rebuild_masters),
            ("after_chart", False, True, None)]

    def finish():
        if st["handle"] is not None:
            unreal.unregister_slate_post_tick_callback(st["handle"])
            st["handle"] = None
        try:
            eas.destroy_actor(chart_actor)
        except Exception:
            pass
        unreal.log("=== гача-цвет: сохранено %d из %d кадров ===" % (st["ok"], 2 * len(PLAN)))
        unreal.SystemLibrary.quit_editor()

    def step_tick():
        st["tick"] += 1
        if st["tick"] > MAX_TICKS:
            unreal.log_error("[гача-цвет] предел тиков")
            finish()
            return
        if st["tick"] < st["next_at"]:
            return
        if st["step"] >= 0:
            grab(PLAN[st["step"]][0])
        st["step"] += 1
        if st["step"] >= len(PLAN):
            finish()
            return
        name, mach_on, chart_on, pre = PLAN[st["step"]]
        if pre:
            pre()
        show(mach_on, chart_on)
        if mach_on:
            act.play_clip_all(1, True)
        unreal.log("[гача-цвет] шаг %s (автоматы=%s мишень=%s)" % (name, mach_on, chart_on))
        st["next_at"] = st["tick"] + SETTLE_TICKS

    def on_tick(_dt):
        if st["busy"]:
            return
        st["busy"] = True
        try:
            step_tick()
        except Exception:
            import traceback
            unreal.log_error("[гача-цвет] ошибка:\n" + traceback.format_exc())
            finish()
        finally:
            st["busy"] = False

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    unreal.log("=== гача-цвет: цикл запущен, шагов %d, уровень %s ===" % (len(PLAN), LEVEL))


if __name__ == "__main__":
    try:
        main()
    except Exception:
        import traceback
        import unreal
        unreal.log_error("[гача-цвет] ошибка старта:\n" + traceback.format_exc())
        # ★ ОБЯЗАТЕЛЬНО: без этого редактор остаётся висеть процессом (движок тикает,
        #   а наш колбэк не зарегистрирован) — пришлось бы добивать по PID.
        unreal.SystemLibrary.quit_editor()
    except BaseException:
        # ★ 16.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 17): стоп защиты логина — SystemExit(3) из forbid_login_write —
        #   не Exception и шёл мимо обработчика выше. БЫЛО: quit_editor не вызывался, редактор висел процессом. Теперь редактор
        #   гасится при ЛЮБОМ исключении старта, а само исключение пробрасывается (код выхода 3 сохраняется).
        try:
            import unreal
            unreal.SystemLibrary.quit_editor()
        except Exception:   # нет модуля unreal / ошибка гашения — исходное исключение важнее
            pass
        raise
