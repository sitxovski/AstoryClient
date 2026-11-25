# Copyright (c) 2026 @sitxovski (Discord)
# asda_gacha_shadow_check.py — замер ТЕНИ и НАСЫЩЕННОСТИ сцены премиум-гачи.
#
# ★ ЧТО ИМЕННО ЗАМЕРЯЕТСЯ (жалоба: «тени СЕРЫЕ, должны быть ЧЁРНЫЕ; лутбоксы СВЕТЛЫЕ»).
#   1) `calib` — КАЛИБРОВКА РЕЖИМА АЛЬФЫ. Три плашки с ИЗВЕСТНЫМИ линейным цветом и ЗАДАННОЙ
#      непрозрачностью (0.25 / 0.50 / 0.80) на СВОЁМ мастере `M_AsdaAlphaCalibProbe`
#      (Unlit + Translucent, `Emissive = CalibColor`, `Opacity = CalibOpacity`, two-sided).
#      Прежняя мишень-текстура `T_GachaAlphaCalib` в кадр не попадала (MID не подхватывал
#      текстуру), и вопрос «прямая альфа или предумноженная» оставался открытым — теперь
#      он решается байтами: при ПРЯМОЙ альфе кадр вернёт sRGB(CalibColor) НЕЗАВИСИМО от
#      непрозрачности, при ПРЕДУМНОЖЕННОЙ — sRGB(CalibColor · op). От этого зависит блендинг
#      UI-мастера `M_AsdaGachaScene3D`: Translucent (прямая) или AlphaComposite (предумноженная).
#
#   2) `m1` / `m5` — сцена с ОДНИМ и с ПЯТЬЮ автоматами. В PIE у владельца автоматов ПЯТЬ,
#      камера дальше, автомат на экране меньше — проверяем, не в размере ли (мип-уровне) дело.
#   3) `m1_nobowl` / `m5_nobowl` — то же с ПОГАШЕННЫМ куполом. В оригинале прозрачные подсетки
#      рисуются ПЕРВЫМ проходом без записи Z (`meshNodeRenderDispatch@0x111E110`,
#      `g_bForceShadowRender = 1`, `byte_3528C46 = 0`), а непрозрачные — вторым, поверх;
#      значит купол в оригинале НЕ ЛОЖИТСЯ на шары и корпус. Кадр без купола = эталон этого
#      порядка, и разница «с куполом / без» показывает, сколько бледности даёт наш порядок.
#
# Уровень — БОЕВОЙ `/Game/Map/Login`, спаун — как у подсистемы. Ничего не сохраняет на диск,
# кроме кадров. Запуск: run_headless_rhi_tick.bat asda_gacha_shadow_check.py
#   ★ 15.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 16): умолчания /Game/Map/Login больше нет — уровень только явно (GACHA_PROBE_LEVEL).
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/gacha_3d"
# ★ 15.09.2026 (раздел 16): БЫЛО умолчание "/Game/Map/Login" — зонд ставил акторы в боевой уровень логина (свет владельца).
#   Теперь без GACHA_PROBE_LEVEL=<путь уровня> — явная ошибка; уровень логина — только с ASDA_ALLOW_LOGIN_WRITE=1.
LEVEL = os.environ.get("GACHA_PROBE_LEVEL", "")
WARMUP_TICKS = 480
SETTLE_TICKS = 150
MAX_TICKS = 200000

SHADOW_MASTER = "/Game/Materials/M_AsdaGachaShadow"
PLANE_MESH = "/Engine/BasicShapes/Plane"
CALIB_MASTER = "/Game/Materials/M_AsdaAlphaCalibProbe"
CALIB_DIST = 260.0            # плашка перед камерой, в единицах UE
CALIB_SCALE = 0.6
# Линейный цвет плашки. sRGB-байт этого линейного значения = 128 (проверка ниже — по байтам).
CALIB_COLOR_LIN = 0.21586
CALIB_OPACITIES = (0.25, 0.50, 0.80)   # 0.80 — ровно альфа ядра тени `gachya_shadow` (204/255)



def main():
    import unreal
    # ★ 15.09.2026 (раздел 16): уровень — только явный; логин — только с разрешения владельца (зонд ставит акторы в уровень).
    if not LEVEL:
        raise RuntimeError("[гача-тень] не задан уровень: GACHA_PROBE_LEVEL=<путь уровня> (умолчания /Game/Map/Login больше нет)")
    import asda_login_guard as _LG   # Tools уже в sys.path (строка у импортов модуля)
    if _LG.is_login_owned_package(LEVEL):
        _LG.forbid_login_write("asda_gacha_shadow_check.py", "акторы зонда в уровень %s" % LEVEL)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if not os.path.isdir(OUT_DIR):
        os.makedirs(OUT_DIR)
    if not les.load_level(LEVEL):
        unreal.log_error("[гача-тень] не открылся уровень %s" % LEVEL)
        # ★ 16.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 18): старт сорван — колбэк тика не зарегистрирован, редактор гасится здесь.
        #   БЫЛО: return без quit_editor — редактор висел процессом (раздел 17.9, п.3).
        unreal.SystemLibrary.quit_editor()
        return
    world = ues.get_editor_world()
    for cmd in ("r.TextureStreaming 0", "r.Streaming.FullyLoadUsedTextures 1", "r.ScreenPercentage 100"):
        unreal.SystemLibrary.execute_console_command(world, cmd)

    # ── мастер калибровки: Unlit + Translucent, цвет и непрозрачность — ПАРАМЕТРЫ ──────────
    #   Строится скриптом, а не берётся с диска: он нужен только зонду и ничем не связан со
    #   сценой гачи. Прежняя мишень-текстура выбыла — её MID не подхватывал текстуру.
    MEL = unreal.MaterialEditingLibrary
    if unreal.EditorAssetLibrary.does_asset_exist(CALIB_MASTER):
        unreal.EditorAssetLibrary.delete_asset(CALIB_MASTER)
    pkg, name = CALIB_MASTER.rsplit("/", 1)
    cal_mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, pkg, unreal.Material, unreal.MaterialFactoryNew())
    cal_mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    cal_mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    cal_mat.set_editor_property("two_sided", True)   # ориентация плашки не должна влиять на замер
    c_col = MEL.create_material_expression(cal_mat, unreal.MaterialExpressionVectorParameter, -400, -120)
    c_col.set_editor_property("parameter_name", "CalibColor")
    c_col.set_editor_property("default_value", unreal.LinearColor(CALIB_COLOR_LIN, CALIB_COLOR_LIN, CALIB_COLOR_LIN, 1.0))
    c_op = MEL.create_material_expression(cal_mat, unreal.MaterialExpressionScalarParameter, -400, 120)
    c_op.set_editor_property("parameter_name", "CalibOpacity")
    c_op.set_editor_property("default_value", 0.5)
    if not MEL.connect_material_property(c_col, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError("калибровка: цвет не подключился к Emissive")
    if not MEL.connect_material_property(c_op, "", unreal.MaterialProperty.MP_OPACITY):
        raise RuntimeError("калибровка: непрозрачность не подключилась к Opacity")
    MEL.recompile_material(cal_mat)
    unreal.EditorAssetLibrary.save_loaded_asset(cal_mat)
    cal_stats = MEL.get_statistics(cal_mat)
    unreal.log("[гача-тень] мастер калибровки готов: ps=%s"
               % cal_stats.get_editor_property("num_pixel_shader_instructions"))

    act = eas.spawn_actor_from_class(unreal.AsdaGachaSceneActor, unreal.Vector(0, 0, 200000))
    act.set_actor_label("GachaShadowProbe")
    caps = list(act.get_components_by_class(unreal.SceneCaptureComponent2D))
    unreal.log("[гача-тень] капчеров у актора: %d (ожидается 1 — маска убрана)" % len(caps))

    # ── три калибровочные плашки: рядом по горизонтали, с РАЗНОЙ заданной непрозрачностью ───
    cam = caps[0]
    eye = cam.get_world_location()
    fwd = cam.get_forward_vector()
    right = cam.get_right_vector()
    calib_smcs = []
    for i, op in enumerate(CALIB_OPACITIES):
        pos = eye + fwd * CALIB_DIST + right * ((i - 1) * 70.0)
        pa = eas.spawn_actor_from_class(unreal.StaticMeshActor, pos, unreal.MathLibrary.make_rot_from_z(fwd))
        pa.set_actor_label("GachaAlphaCalib_%.2f" % op)
        smc = pa.static_mesh_component
        smc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        smc.set_static_mesh(unreal.load_asset(PLANE_MESH))
        smc.set_world_scale3d(unreal.Vector(CALIB_SCALE, CALIB_SCALE, CALIB_SCALE))
        smc.set_cast_shadow(False)
        mid = smc.create_dynamic_material_instance(0, cal_mat)
        mid.set_vector_parameter_value("CalibColor",
                                       unreal.LinearColor(CALIB_COLOR_LIN, CALIB_COLOR_LIN, CALIB_COLOR_LIN, 1.0))
        mid.set_scalar_parameter_value("CalibOpacity", op)
        smc.set_visibility(False)
        for cp in caps:
            cp.show_only_component(smc)
        calib_smcs.append(smc)

    st = {"tick": 0, "step": -1, "next_at": WARMUP_TICKS, "ok": 0, "handle": None, "busy": False,
          "count": 0}

    def set_machines(n):
        act.build_machines(n)
        act.play_clip_all(1, True)
        for c in act.get_components_by_class(unreal.SkeletalMeshComponent):
            try:
                c.set_update_animation_in_editor(True)
            except Exception:
                pass
        # плашки заново дописать в show-only списки: build_machines их обнуляет
        for cp in list(act.get_components_by_class(unreal.SceneCaptureComponent2D)):
            for c in calib_smcs:
                cp.show_only_component(c)
        st["count"] = n

    def set_bowl(visible):
        """Гасит/возвращает КУПОЛ: слот с материалом `*_blend` (`Bowl` из .jbm)."""
        n = 0
        for c in act.get_components_by_class(unreal.SkeletalMeshComponent):
            mats = c.get_materials()
            for i, m in enumerate(mats):
                if not m:
                    continue
                nm = m.get_name().lower()
                # купол = слот `md_Gachya_?_00_blend`; тень (`gachya_shadow_blend1`) — не он
                if "_blend" not in nm or "shadow" in nm:
                    continue
                mid = c.create_dynamic_material_instance(i)
                # ★ «Виден» = ЭТАЛОННОЕ значение из сборщика, а не подставленное здесь число:
                #   иначе зонд молча мерил бы старую половинчатую плёнку вместо того, что на диске.
                import asda_build_gacha_glass_material as GLASS
                mid.set_scalar_parameter_value("GlassOpacity", GLASS.DEFAULT_OPACITY if visible else 0.0)
                n += 1
        unreal.log("[гача-тень] купол %s: слотов %d" % ("виден" if visible else "погашен", n))

    def show(machines_on, chart_on):
        for c in act.get_components_by_class(unreal.SkeletalMeshComponent):
            c.set_visibility(machines_on)
        for c in calib_smcs:
            c.set_visibility(chart_on)

    def grab(name):
        for cp in list(act.get_components_by_class(unreal.SceneCaptureComponent2D)):
            cp.capture_scene()
        tgt = act.get_render_target()
        fn = "sb_%s_color.png" % name
        dst = os.path.join(OUT_DIR, fn)
        if os.path.exists(dst):
            os.remove(dst)
        unreal.RenderingLibrary.export_render_target(world, tgt, OUT_DIR, fn)
        st["ok"] += 1 if os.path.exists(dst) else 0
        unreal.log("[гача-тень] кадр %s: %s" % (fn, "ok" if os.path.exists(dst) else "НЕ СОХРАНЁН"))

    def p_calib():
        set_machines(1)
        show(False, True)

    def p_m1():
        set_machines(1)
        set_bowl(True)
        show(True, False)

    def p_m1_nobowl():
        set_bowl(False)
        show(True, False)

    def p_m5():
        set_machines(5)
        set_bowl(True)
        show(True, False)

    def p_m5_nobowl():
        set_bowl(False)
        show(True, False)

    PLAN = [("calib", p_calib), ("m1", p_m1), ("m1_nobowl", p_m1_nobowl),
            ("m5", p_m5), ("m5_nobowl", p_m5_nobowl)]

    def finish():
        if st["handle"] is not None:
            unreal.unregister_slate_post_tick_callback(st["handle"])
            st["handle"] = None
        for c in calib_smcs:
            try:
                eas.destroy_actor(c.get_owner())
            except Exception:
                pass
        unreal.log("=== гача-тень: сохранено %d из %d кадров ===" % (st["ok"], len(PLAN)))
        unreal.SystemLibrary.quit_editor()

    def step_tick():
        st["tick"] += 1
        if st["tick"] > MAX_TICKS:
            unreal.log_error("[гача-тень] предел тиков")
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
        name, pre = PLAN[st["step"]]
        pre()
        unreal.log("[гача-тень] шаг %s" % name)
        st["next_at"] = st["tick"] + SETTLE_TICKS

    def on_tick(_dt):
        if st["busy"]:
            return
        st["busy"] = True
        try:
            step_tick()
        except Exception:
            import traceback
            unreal.log_error("[гача-тень] ошибка:\n" + traceback.format_exc())
            finish()
        finally:
            st["busy"] = False

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    unreal.log("=== гача-тень: цикл запущен, шагов %d, уровень %s ===" % (len(PLAN), LEVEL))


if __name__ == "__main__":
    try:
        main()
    except Exception:
        import traceback
        import unreal
        unreal.log_error("[гача-тень] ошибка старта:\n" + traceback.format_exc())
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
