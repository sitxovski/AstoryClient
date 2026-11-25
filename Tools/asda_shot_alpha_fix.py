# Copyright (c) 2026 @sitxovski (Discord)
# asda_shot_alpha_fix.py — кадры «ДО/ПОСЛЕ» для предмета, которому вернули альфа-канал.
#
# Что доказываем: у `itm1_n_98_othr207_00` мастер `M_AsdaObjectLit` — Masked с порогом 0.5, и
# восстановленная альфа текстуры доходит до отсечения. «До» — та же текстура БЕЗ альфы (файл
# `RawAssets/Data/_alpha_check/itm207_before_rgb.png`, это ровно то, что лежало в проекте до
# правки: цвет конвейер не трогал, менялся только четвёртый канал).
#
# ★★★ ГРАБЛИ, УЖЕ ОПЛАЧЕННЫЕ (см. asda_shot_blend_models.py):
#   • запускать ТОЛЬКО через `-ExecCmds="py <файл>"` и БЕЗ `-NullRHI` — при `-ExecutePythonScript`
#     редактор закрывается сразу после возврата и ни одного тика не происходит;
#   • перед каждым экспортом обязательный явный `capture_scene()`;
#   • bool-UPROPERTY в UE-python идут БЕЗ префикса `b`: `capture_every_frame`.
#
# Запуск:  Tools/run_headless_rhi_tick.bat  Tools/asda_shot_alpha_fix.py
# Выход:   Reports/alpha_fix/item207_before.png, item207_after.png
import os

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/alpha_fix"
LEVEL = "/Temp/L_AlphaFixShot"
TMP_PKG = "/Game/_AlphaCheck"                     # временная папка, удаляется в конце
BEFORE_PNG = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/RawAssets/Data/_alpha_check/itm207_before_rgb.png"
MESH = "/Game/AsdaAssets/Models/Items/itm1_n_98_othr207_00/StaticMeshes/itm1_n_98_othr207_00"
MASTER = "/Game/Materials/M_AsdaObjectLit"

WARMUP_TICKS = 700     # компиляция шейдеров + стриминг текстур (300 не хватало: кадр выходил чёрным)
SETTLE_TICKS = 400     # сходимость автоэкспозиции и временного сглаживания
MAX_TICKS = 60000
RT_SIZE = 768
FRAME_R = 1500.0
DIST = float(os.environ.get("ASDA_SHOT_DIST", "3.2"))   # дистанция камеры в радиусах кадра


def main():
    import unreal
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    EAL = unreal.EditorAssetLibrary
    MEL = unreal.MaterialEditingLibrary
    if not os.path.isdir(OUT_DIR):
        os.makedirs(OUT_DIR)

    # ── текстура «до»: тот же цвет, но без альфы. Импортируем во ВРЕМЕННУЮ папку и удаляем в конце.
    before_asset = TMP_PKG + "/itm207_before"
    if not EAL.does_asset_exist(before_asset):
        task = unreal.AssetImportTask()
        task.filename = BEFORE_PNG
        task.destination_path = TMP_PKG
        task.destination_name = "itm207_before"
        task.replace_existing = True
        task.automated = True
        task.save = True
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex_before = EAL.load_asset(before_asset)
    tex_after = EAL.load_asset("/Game/AsdaAssets/Textures/Items/Texture/itm1_n_98_othr207_00")
    master = EAL.load_asset(MASTER)
    mesh = EAL.load_asset(MESH)
    unreal.log("[альфа] тексуры: до=%s после=%s мастер=%s меш=%s"
               % (bool(tex_before), bool(tex_after), bool(master), bool(mesh)))

    les.new_level(LEVEL)
    world = ues.get_editor_world()
    for cmd in ("r.TextureStreaming 0", "r.Streaming.FullyLoadUsedTextures 1", "r.ScreenPercentage 100"):
        unreal.SystemLibrary.execute_console_command(world, cmd)

    # фон: контрастная плоскость — просвет виден и на тёмном, и на цветном
    bgm = EAL.load_asset("/Game/Materials/M_BlendShotBG")
    plane = unreal.load_asset("/Engine/BasicShapes/Plane")

    sun = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500))
    sun.set_actor_rotation(unreal.Rotator(-40, -35, 0), False)
    sun.light_component.set_intensity(6.0)
    sky = eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 500))
    # ★ в пустом уровне неба нет, и SkyLight «захватывает» черноту — задаём кубмап явно
    try:
        sky.light_component.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    except Exception:
        pass
    sky.light_component.set_intensity(1.0)

    cap = eas.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(0, -500, 100))
    cc = cap.capture_component2d
    rt = unreal.RenderingLibrary.create_render_target2d(world, RT_SIZE, RT_SIZE,
                                                        unreal.TextureRenderTargetFormat.RTF_RGBA8)
    cc.set_editor_property("texture_target", rt)
    cc.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    for prop in ("capture_every_frame", "capture_on_movement"):
        try:
            cc.set_editor_property(prop, True)
        except Exception as e:
            unreal.log_warning("[альфа] свойство %s не выставлено: %s" % (prop, e))
    try:
        cc.set_editor_property("show_flag_settings",
                               [unreal.EngineShowFlagsSetting(show_flag_name="TemporalAA", enabled=True)])
    except Exception as e:
        unreal.log_warning("[альфа] TemporalAA у капчера не включён: %s" % e)

    pps = unreal.PostProcessSettings()
    for prop, val in (("auto_exposure_bias", 0.0), ("auto_exposure_speed_up", 100.0),
                      ("auto_exposure_speed_down", 100.0), ("auto_exposure_min_brightness", 0.03),
                      ("auto_exposure_max_brightness", 2.0), ("bloom_intensity", 0.0),
                      ("motion_blur_amount", 0.0)):
        try:
            pps.set_editor_property("override_" + prop, True)
            pps.set_editor_property(prop, val)
        except Exception:
            pass
    cc.set_editor_property("post_process_settings", pps)

    st = {"tick": 0, "i": -1, "next_at": WARMUP_TICKS, "ok": 0, "handle": None,
          "actor": None, "bg": []}
    SHOTS = [("item207_before", tex_before), ("item207_after", tex_after)]

    def clear():
        if st["actor"]:
            try:
                eas.destroy_actor(st["actor"])
            except Exception:
                pass
            st["actor"] = None
        for a in st["bg"]:
            try:
                eas.destroy_actor(a)
            except Exception:
                pass
        st["bg"] = []

    def place(idx):
        name, tex = SHOTS[idx]
        act = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, 0))
        comp = act.static_mesh_component
        comp.set_static_mesh(mesh)
        st["actor"] = act
        # ★ Кадрируем по МИРОВЫМ габаритам актора (`get_actor_bounds`), а не по `mesh.get_bounds()`:
        #   у меша свои границы в локальных единицах, и при 9·r по ним предмет всё равно не влезал
        #   в кадр — камера стояла внутри модели. Мировые границы дают дистанцию сразу верно.
        b = mesh.get_bounds()
        org, ext = b.origin, b.box_extent
        r0 = max(ext.x, ext.y, ext.z, 1.0)
        sc = FRAME_R / r0
        act.set_actor_scale3d(unreal.Vector(sc, sc, sc))
        act.set_actor_location(unreal.Vector(0, 0, 0), False, False)
        worg, wext = act.get_actor_bounds(False)
        act.set_actor_location(unreal.Vector(-worg.x, -worg.y, -worg.z), False, False)
        r = max(wext.x, wext.y, wext.z, 1.0)
        unreal.log("[альфа] габарит меша r0=%.1f масштаб=%.3f мировой габарит=(%.1f,%.1f,%.1f)"
                   % (r0, sc, wext.x, wext.y, wext.z))
        # ★ обе версии рисуем ОДНИМ мастером (Masked, clip 0.5) — разница только в текстуре
        for i in range(comp.get_num_materials()):
            mid = comp.create_dynamic_material_instance(i, master)
            if mid and tex:
                mid.set_texture_parameter_value("Tex", tex)
        for c, dz in ((unreal.LinearColor(0.02, 0.02, 0.02, 1), r * 0.6),
                      (unreal.LinearColor(0.85, 0.15, 0.65, 1), -r * 0.6)):
            if not plane:
                break
            p = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, r * (DIST * 0.9), dz))
            smc = p.static_mesh_component
            smc.set_static_mesh(plane)
            if bgm:
                m = smc.create_dynamic_material_instance(0, bgm)
                if m:
                    m.set_vector_parameter_value("Color", c)
            p.set_actor_rotation(unreal.Rotator(90, 0, 0), False)
            p.set_actor_scale3d(unreal.Vector(r * DIST / 12.0, r * DIST / 12.0, 1))
            st["bg"].append(p)
        # ★ 1.6·r (как в asda_shot_blend_models) для этого предмета — крупный план: при FOV 55°
        #   на такой дистанции в кадр влезает 2500 юнитов, а модель после нормировки 3000.
        #   Берём 3.2·r — предмет целиком, вокруг него виден фон, и просвет читается.
        cap.set_actor_location(unreal.Vector(0, -r * DIST, r * 0.30), False, False)
        cap.set_actor_rotation(unreal.Rotator(-6, 90, 0), False)
        cc.set_editor_property("fov_angle", 55.0)
        unreal.log("[альфа] сцена %s: габарит меша=(%.1f,%.1f,%.1f) масштаб=%.3f слотов=%d "
                   "актор=%s камера=%s"
                   % (name, ext.x, ext.y, ext.z, sc, comp.get_num_materials(),
                      act.get_actor_location(), cap.get_actor_location()))
        return True

    def grab(idx):
        name = SHOTS[idx][0]
        cc.capture_scene()
        fn = name + ".png"
        dst = os.path.join(OUT_DIR, fn)
        if os.path.exists(dst):
            os.remove(dst)
        unreal.RenderingLibrary.export_render_target(world, rt, OUT_DIR, fn)
        try:
            for u, v in ((0.5, 0.5), (0.2, 0.5), (0.5, 0.85)):
                px = unreal.RenderingLibrary.read_render_target_raw_uv(world, rt, u, v)
                unreal.log("[альфа]   пиксель (%.2f,%.2f) RGB=(%d,%d,%d)" % (u, v, px.r, px.g, px.b))
        except Exception as e:
            unreal.log_warning("[альфа] пиксели не прочитаны: %s" % e)
        st["ok"] += 1 if os.path.exists(dst) else 0
        unreal.log("[альфа] снято %s (дистанция %.1f·r, FOV %.0f) -> %s"
                   % (fn, DIST, cc.get_editor_property("fov_angle"), "ОК" if os.path.exists(dst) else "★ ПУСТО"))

    def finish():
        if st["handle"] is not None:
            unreal.unregister_slate_post_tick_callback(st["handle"])
            st["handle"] = None
        try:
            if EAL.does_directory_exist(TMP_PKG):
                EAL.delete_directory(TMP_PKG)        # временную текстуру «до» за собой убираем
                unreal.log("[альфа] временная папка %s удалена" % TMP_PKG)
        except Exception as e:
            unreal.log_warning("[альфа] временная папка не удалена: %s" % e)
        unreal.log("=== кадры альфы: сохранено %d из %d ===" % (st["ok"], len(SHOTS)))
        unreal.SystemLibrary.quit_editor()

    def step():
        st["tick"] += 1
        if st["tick"] > MAX_TICKS:
            unreal.log_error("[альфа] предел тиков")
            finish()
            return
        if st["tick"] < st["next_at"]:
            return
        if st["i"] >= 0:
            grab(st["i"])
            clear()
        st["i"] += 1
        while st["i"] < len(SHOTS) and not place(st["i"]):
            st["i"] += 1
        if st["i"] >= len(SHOTS):
            finish()
            return
        st["next_at"] = st["tick"] + SETTLE_TICKS

    def on_tick(_dt):
        try:
            step()
        except Exception:
            import traceback
            unreal.log_error("[альфа] ошибка в шаге:\n" + traceback.format_exc())
            finish()

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    unreal.log("=== кадры альфы: цикл запущен, прогрев %d ===" % WARMUP_TICKS)


if __name__ == "__main__":
    try:
        main()
    except Exception:
        import traceback
        import unreal
        unreal.log_error("[альфа] СБОЙ:\n" + traceback.format_exc())
        unreal.SystemLibrary.quit_editor()
