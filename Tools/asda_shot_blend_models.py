# Copyright (c) 2026 @sitxovski (Discord)
# asda_shot_blend_models.py — КАДРЫ «ДО/ПОСЛЕ» для раскатки прозрачности помеченных подсеток.
#
# Снимает по одному кадру на модель: меш на КОНТРАСТНОМ фоне (две полосы) + свет, через
# SceneCapture2D. Фон нужен, чтобы прозрачность была видна: `SCS_FINAL_COLOR_LDR` пишет альфу 255
# у всего кадра (замер 2026-08-17), поэтому «прозрачно ли» читается только по просвечивающему фону.
#
# ★★★ ГРАБЛЯ (память normal-from-albedo-studio, gacha_3d_impl.md): при `-ExecutePythonScript`
#   движок НЕ тикает, пока идёт скрипт — кадр остаётся от раннего тика, модели выходят белыми.
#   Отсюда: машина состояний на `register_slate_post_tick_callback`, запуск БЕЗ `-NullRHI`,
#   и перед КАЖДЫМ экспортом обязательный явный `capture_scene()`.
# ★ Мастер прозрачности — Masked + DitherOpacityMask: покрытие набирается ВРЕМЕННЫМ сглаживанием,
#   поэтому у капчера включён show-flag `temporal_aa` и дан запас тиков на сходимость (SETTLE_TICKS).
#
# Запуск:
#   set ASDA_SHOT_TAG=before   (или after)
#   Tools/run_headless_rhi_tick.bat  Tools/asda_shot_blend_models.py
# ★★★ ЗАПУСКАТЬ ТОЛЬКО через `-ExecCmds="py <файл>"`, а НЕ через `-ExecutePythonScript`: второй
#   завершает редактор сразу после возврата из скрипта, и машина состояний на
#   `register_slate_post_tick_callback` не успевает сделать НИ ОДНОГО тика (кадры выходят пустыми).
# Выход: Reports/blend_rollout/<модель>_<tag>.png
import os
import sys as _sys
_sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/blend_rollout"
LEVEL = "/Temp/L_BlendShot"
TAG = os.environ.get("ASDA_SHOT_TAG", "before")
WARMUP_TICKS = 300     # компиляция шейдеров + стриминг текстур
SETTLE_TICKS = 260     # сходимость временного сглаживания на dither-маске + автоэкспозиции
MAX_TICKS = 60000
RT_SIZE = 768
FRAME_R = 1500.0       # к этому габариту приводится ЛЮБАЯ модель (см. комментарий в place())

M = "/Game/AsdaAssets/Models"
# (имя кадра, путь ассета, класс)  — по 2-3 характерных модели на каждую из трёх групп
SUBJECTS = [
    # ДЕКОР КАРТ: карточки листвы, дымка (91 % полутона), квады пара
    ("decor_mdel_oldtree",    M + "/NPC/mdel_oldtree-a_00/SkeletalMeshes/mdel_oldtree-a_00",       "skel"),
    ("decor_mdel_buryd",      M + "/NPC/mdel_buryd-a_00/SkeletalMeshes/mdel_buryd-a_00",           "skel"),
    ("decor_mdel_csomachine", M + "/NPC/mdel_csomachine-a_00/SkeletalMeshes/mdel_csomachine-a_00", "skel"),
    # СЦЕНА СОЗДАНИЯ/ВЫБОРА ПЕРСОНАЖА: задник (667 помеченных подсеток) и шкатулка (ореол c-04)
    ("scene_cre_creback",     M + "/Char/cre_creback-a_00/SkeletalMeshes/cre_creback-a_00",        "skel"),
    ("scene_cre_creation01",  M + "/Char/cre_creation01-a_00/SkeletalMeshes/cre_creation01-a_00",  "skel"),
    # ПРЕДМЕТЫ: 82 % полутона, ожерелье (3 из 4 подсеток помечены), колчан стрел
    ("item_othr126",          M + "/Items/itm1_n_98_othr126_00/StaticMeshes/itm1_n_98_othr126_00", "static"),
    ("item_neck009",          M + "/Items/itm1_n_00_neck009_00/StaticMeshes/itm1_n_00_neck009_00", "static"),
    ("item_arrw001",          M + "/Items/itm1_n_00_arrw001_00/StaticMeshes/itm1_n_00_arrw001_00", "static"),
]


def main():
    import unreal
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    EAL = unreal.EditorAssetLibrary
    ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
    MEL = unreal.MaterialEditingLibrary
    import asda_material_policy as POL
    if not os.path.isdir(OUT_DIR):
        os.makedirs(OUT_DIR)

    les.new_level(LEVEL)
    world = ues.get_editor_world()
    for cmd in ("r.TextureStreaming 0", "r.Streaming.FullyLoadUsedTextures 1", "r.ScreenPercentage 100"):
        unreal.SystemLibrary.execute_console_command(world, cmd)

    # ---- фон: две контрастные полосы (прозрачность видна как просвет) ----
    # ★ Материал создаём в /Game/Materials, а НЕ в /Temp: `create_asset` в /Temp возвращает None
    #   (замер 2026-08-18, первый прогон упал на `NoneType.set_editor_property`). Ассет служебный,
    #   но постоянный — иначе кадр «до/после» невоспроизводим.
    bgm_path = "/Game/Materials/M_BlendShotBG"
    bgm = EAL.load_asset(bgm_path) if EAL.does_asset_exist(bgm_path) else None
    if not bgm:
        bgm = ATOOLS.create_asset("M_BlendShotBG", "/Game/Materials", unreal.Material,
                                  unreal.MaterialFactoryNew())
        if not bgm:
            unreal.log_error("[кадр] не создан %s — фон будет дефолтным" % bgm_path)
        else:
            bgm.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
            col = MEL.create_material_expression(bgm, unreal.MaterialExpressionVectorParameter, -300, 0)
            col.set_editor_property("parameter_name", "Color")
            MEL.connect_material_property(col, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
            MEL.recompile_material(bgm)
            EAL.save_loaded_asset(bgm)

    plane = unreal.load_asset("/Engine/BasicShapes/Plane")
    if not plane:
        unreal.log_error("[кадр] нет /Engine/BasicShapes/Plane — фон не построить")

    # свет: направленный + небесный (мастера Lit — без света кадр чёрный)
    sun = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500))
    sun.set_actor_rotation(unreal.Rotator(-40, -35, 0), False)
    sun.light_component.set_intensity(6.0)
    sky = eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 500))
    try:
        sky.light_component.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    except Exception:
        pass
    sky.light_component.set_intensity(1.0)

    # капчер
    cap = eas.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(0, -500, 100))
    cc = cap.capture_component2d
    rt = unreal.RenderingLibrary.create_render_target2d(world, RT_SIZE, RT_SIZE, unreal.TextureRenderTargetFormat.RTF_RGBA8)
    cc.set_editor_property("texture_target", rt)
    cc.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    # ★ ГРАБЛЯ ИМЕНОВАНИЯ (замер 2026-08-18, первый прогон упал): UE-python отдаёт bool-UPROPERTY
    #   БЕЗ префикса `b` — `bCaptureEveryFrame` → `capture_every_frame`. С `b_capture_every_frame`
    #   движок кидает «Failed to find property». Та же грабля, что `bBaseAlreadyUE`→`base_already_ue`.
    for prop in ("capture_every_frame", "capture_on_movement"):
        try:
            cc.set_editor_property(prop, True)   # даём временному сглаживанию накопиться
        except Exception as e:
            unreal.log_warning("[кадр] свойство %s не выставлено: %s" % (prop, e))
    # ★ У `SceneCaptureComponent2D` в UE-python НЕТ атрибута `show_flags` (замер 2026-08-18:
    #   «object has no attribute 'show_flags'»). Переопределение флагов идёт списком
    #   `show_flag_settings` из FEngineShowFlagsSetting — включаем временное сглаживание им:
    #   без TAA dither-маска мастера прозрачности осталась бы шумом, а не полутоном.
    try:
        cc.set_editor_property("show_flag_settings",
                               [unreal.EngineShowFlagsSetting(show_flag_name="TemporalAA", enabled=True)])
    except Exception as e:
        unreal.log_warning("[кадр] TemporalAA у капчера не включён: %s" % e)

    # ★ ФИКСИРОВАННАЯ ЭКСПОЗИЦИЯ. `SCS_FINAL_COLOR_LDR` идёт через тонемаппинг с АВТОЭКСПОЗИЦИЕЙ,
    #   а она адаптируется от кадра к кадру: в замере 2026-08-18 серия из восьми кадров подряд
    #   уезжала по средней яркости 11.6 → 2.9, и сравнивать «до/после» было нельзя. Ставим ручной
    #   режим с фиксированной яркостью — тогда единственная разница между кадрами это материал.
    #   ★ Ручной режим (AEM_MANUAL) НЕ подходит: он считает экспозицию по параметрам камеры
    #     (ISO/диафрагма/выдержка), и кадр уходит в ЧЁРНОЕ (замер: яркость 0.09 у всех восьми).
    #     Рабочий вариант — оставить автоэкспозицию, но сделать её адаптацию МГНОВЕННОЙ
    #     (speed_up/down = 100): тогда она успевает сойтись за отведённые тики и одинакова
    #     для «до» и «после», а не «уплывает» по серии.
    pps = unreal.PostProcessSettings()
    for prop, val in (("auto_exposure_bias", 0.0),
                      ("auto_exposure_speed_up", 100.0),
                      ("auto_exposure_speed_down", 100.0),
                      ("auto_exposure_min_brightness", 0.03),
                      ("auto_exposure_max_brightness", 2.0),
                      ("bloom_intensity", 0.0),
                      ("motion_blur_amount", 0.0)):
        try:
            pps.set_editor_property("override_" + prop, True)
            pps.set_editor_property(prop, val)
        except Exception as e:
            unreal.log_warning("[кадр] пост-настройка %s не выставлена: %s" % (prop, e))
    cc.set_editor_property("post_process_settings", pps)

    # ★ КАДР «ДО» БЕЗ ОТКАТА АССЕТОВ. Ассеты уже переведены на прозрачный мастер, поэтому «до»
    #   воспроизводим ОВЕРРАЙДОМ НА КОМПОНЕНТЕ: слот, сидящий на `M_AsdaObjectBlendLit`, временно
    #   подменяем динамическим материалом от `M_AsdaObjectLit` (masked, clip 0.5) с ТОЙ ЖЕ
    #   текстурой. Это ровно прежнее поведение конвейера, и ни один ассет не портится.
    masked_master = EAL.load_asset(POL.MASTER_MASKED)
    blend_name = POL.MASTER_BLEND.rsplit("/", 1)[-1]

    def force_masked(comp):
        n = 0
        for i in range(comp.get_num_materials()):
            mi = comp.get_material(i)
            par = mi.get_editor_property("parent") if isinstance(mi, unreal.MaterialInstanceConstant) else None
            if not par or par.get_name() != blend_name or not masked_master:
                continue
            tex = MEL.get_material_instance_texture_parameter_value(mi, "Tex")
            mid = comp.create_dynamic_material_instance(i, masked_master)
            if mid and tex:
                mid.set_texture_parameter_value("Tex", tex)
            n += 1
        return n

    st = {"tick": 0, "i": -1, "next_at": WARMUP_TICKS, "ok": 0, "handle": None, "actor": None, "bg": []}

    def clear_subject():
        if st["actor"]:
            try: eas.destroy_actor(st["actor"])
            except Exception: pass
            st["actor"] = None
        for a in st["bg"]:
            try: eas.destroy_actor(a)
            except Exception: pass
        st["bg"] = []

    def place(idx):
        name, path, kind = SUBJECTS[idx]
        asset = EAL.load_asset(path)
        if not asset:
            unreal.log_warning("[кадр] НЕТ ассета %s" % path); return False
        if kind == "skel":
            act = eas.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(0, 0, 0))
            comp = act.skeletal_mesh_component
            try:
                comp.set_skeletal_mesh_asset(asset)     # UE5-имя; старое set_skeletal_mesh устарело
            except Exception:
                comp.set_skeletal_mesh(asset)
            b = asset.get_bounds()
        else:
            act = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, 0))
            comp = act.static_mesh_component
            comp.set_static_mesh(asset)
            b = asset.get_bounds()
        st["actor"] = act
        org = b.origin; ext = b.box_extent
        r = max(ext.x, ext.y, ext.z, 1.0)
        # ★ ЕДИНЫЙ ГАБАРИТ В КАДРЕ. Модели приходят в РАЗНЫХ масштабах: glTF-Interchange импортирует
        #   ×100 (задник — 394 003 юнита), а мелкие предметы остаются в сотнях. При «камера на 3·r»
        #   кадр зависел от габарита и у части моделей выходил ПОЛНОСТЬЮ ЧЁРНЫМ (замер 2026-08-18:
        #   `mdel_csomachine` r=398 и `cre_creation01` r=529 — 0 ненулевых пикселей, при этом
        #   `mdel_buryd` r=1025 снимался нормально). Поэтому масштабируем КАЖДУЮ модель к общему
        #   габариту FRAME_R — тогда камера, фон и свет у всех кадров одинаковы и сравнимы.
        sc = FRAME_R / r
        act.set_actor_scale3d(unreal.Vector(sc, sc, sc))
        org = unreal.Vector(org.x * sc, org.y * sc, org.z * sc)
        r = FRAME_R
        # ★ UE-python: у `set_actor_location` НЕТ значений по умолчанию — обязательны sweep и teleport
        act.set_actor_location(unreal.Vector(-org.x, -org.y, -org.z), False, False)
        # фон: две полосы позади модели (тёмная и светлая) — просвет читается на обеих
        for k, (c, dz) in enumerate(((unreal.LinearColor(0.02, 0.02, 0.02, 1), r * 0.6),
                                     (unreal.LinearColor(0.85, 0.15, 0.65, 1), -r * 0.6))):
            if not plane:
                break
            p = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, r * 1.6, dz))
            smc = p.static_mesh_component
            smc.set_static_mesh(plane)
            if bgm:
                mid = smc.create_dynamic_material_instance(0, bgm)
                if mid:
                    mid.set_vector_parameter_value("Color", c)
            p.set_actor_rotation(unreal.Rotator(90, 0, 0), False)
            p.set_actor_scale3d(unreal.Vector(r / 12.0, r / 12.0, 1))
            st["bg"].append(p)
        # ★ Камера ближе (1.6·r вместо 3·r): при 3·r модель занимала считанные пиксели и разница
        #   «до/после» тонула — замер показал 0.0 % изменившихся пикселей при видимом фоне.
        cap.set_actor_location(unreal.Vector(0, -r * 1.6, r * 0.30), False, False)
        cap.set_actor_rotation(unreal.Rotator(-6, 90, 0), False)
        cc.set_editor_property("fov_angle", 55.0)
        forced = force_masked(comp) if TAG == "before" else 0
        unreal.log("[кадр] %s: радиус=%.1f масштаб=%.3f слотов=%d%s"
                   % (name, r, sc, comp.get_num_materials(),
                      (" подменено на masked=%d" % forced) if forced else ""))
        return True

    def grab(idx):
        name = SUBJECTS[idx][0]
        cc.capture_scene()                       # ★ без этого экспортируется старый кадр
        fn = "%s_%s.png" % (name, TAG)
        dst = os.path.join(OUT_DIR, fn)
        if os.path.exists(dst):
            os.remove(dst)
        unreal.RenderingLibrary.export_render_target(world, rt, OUT_DIR, fn)
        ok = os.path.exists(dst)
        # ★ Диагностика «попала ли модель в кадр»: без неё легко снять восемь кадров ЧИСТОГО ФОНА
        #   и не заметить (замер 2026-08-18: пара до/после отличалась на 0.0 % пикселей).
        try:
            px = unreal.RenderingLibrary.read_render_target_raw_uv(world, rt, 0.5, 0.5)
            unreal.log("[кадр]   центр кадра RGBA=(%d,%d,%d)" % (px.r, px.g, px.b))
        except Exception:
            pass
        st["ok"] += 1 if ok else 0
        unreal.log("[кадр] снято %s -> %s" % (fn, "ОК" if ok else "★ ПУСТО"))

    def finish():
        if st["handle"] is not None:
            unreal.unregister_slate_post_tick_callback(st["handle"]); st["handle"] = None
        unreal.log("=== кадры blend (%s): сохранено %d из %d ===" % (TAG, st["ok"], len(SUBJECTS)))
        unreal.SystemLibrary.quit_editor()

    def step():
        st["tick"] += 1
        if st["tick"] > MAX_TICKS:
            unreal.log_error("[кадр] предел тиков"); finish(); return
        if st["tick"] < st["next_at"]:
            return
        if st["i"] >= 0:
            grab(st["i"]); clear_subject()
        st["i"] += 1
        while st["i"] < len(SUBJECTS) and not place(st["i"]):
            st["i"] += 1
        if st["i"] >= len(SUBJECTS):
            finish(); return
        st["next_at"] = st["tick"] + SETTLE_TICKS

    def on_tick(_dt):
        # ★ исключение внутри slate-колбэка движок ГЛОТАЕТ: шаг не увеличится и цикл зациклится.
        try:
            step()
        except Exception:
            import traceback
            unreal.log_error("[кадр] ошибка в шаге:\n" + traceback.format_exc())
            finish()

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    unreal.log("=== кадры blend (%s): цикл запущен, прогрев %d ===" % (TAG, WARMUP_TICKS))


if __name__ == "__main__":
    try:
        main()
    except Exception:
        import traceback, unreal
        unreal.log_error("[кадр] СБОЙ:\n" + traceback.format_exc())
        unreal.SystemLibrary.quit_editor()
