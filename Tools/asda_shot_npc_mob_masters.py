# Copyright (c) 2026 @sitxovski (Discord)
# asda_shot_npc_mob_masters.py — КАДРЫ «ДО/ПОСЛЕ» для правки мастеров NPC и мобов.
#
# ★★★ ГРАБЛЯ ДВИЖКА (память normal-from-albedo-studio, повторно в gacha_3d_impl.md):
#   при `-ExecutePythonScript` движок НЕ тикает, пока идёт скрипт, поэтому SceneCapture отдаёт
#   кадр от РАННЕГО тика (до компиляции шейдеров) — модели выходят белыми. Отсюда: машина
#   состояний на `register_slate_post_tick_callback`, запуск БЕЗ `-NullRHI`
#   (run_headless_rhi.bat) и явный `capture_scene()` перед КАЖДЫМ экспортом.
#
# ★ Что снимаем: по одной модели на кадр, камера по габаритам меша, один DirectionalLight +
#   SkyLight с фиксированной интенсивностью, ручная экспозиция (авто-экспозиция выровняла бы
#   кадр и разница мастеров исчезла бы — грабля §7.4 weapon_texture_alpha_fix.md).
#
# Запуск: run_headless_rhi.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_shot_npc_mob_masters.py
#   ASDA_SHOT_LIMIT=N — снять только первые N моделей (быстрая проверка).
#   Каждая модель снимается ДВАЖДЫ за один прогон: `before_<тег>.png` (мастер принудительно
#   возвращён к masked `M_AsdaObjectLit` переопределением на компоненте) и `after_<тег>.png`
#   (штатные мастера ассетов). См. FORCE_BEFORE.
import os

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/blend_rollout"
LEVEL_MAP = "/Game/Map/alpen/alpen"   # готовая карта со своим светом (см. комментарий в main)
# ★ Сцена — В ВОЗДУХЕ над картой: рельеф и постройки в кадр не лезут, а свет карты работает.
#   Свежий уровень (`new_level`) не годится — там капчер отдаёт чёрный кадр (см. main).
STAGE_Z = 12000.0
RES = 768
WARMUP_TICKS = 360        # компиляция шейдеров + стриминг текстур
SETTLE_TICKS = 120        # после перестановки модели/камеры (меньше — вьюпорт отдаёт ПРЕДЫДУЩИЙ кадр:
                          #   замер: два разных NPC подряд дали побайтово одинаковую среднюю яркость 189.3)
SHOT_TICKS = 120          # ожидание асинхронного take_high_res_screenshot
MAX_TICKS = 60000

# (тег кадра, пакет меша, класс)  — характерные представители, см. отчёт blend_rollout_npc_mobs.md §7
SHOTS = [
    # ★ Порядок важен: запросы снимка изредка теряются движком, поэтому самые ценные модели —
    #   первыми. Состав отобран так, чтобы покрыть ОБА эффекта правки:
    #   (а) NPC/мобы техники tSkin/tRigid, у которых masked резал геометрию;
    #   (б) 5 из 6 моделей мобов с подсетками tNoSkin + рендер-бит (им включается прозрачность).
    ("mob_ruby02",    "/Game/AsdaAssets/Models/Mob/mon_s_0_ruby02-a_00/StaticMeshes/mon_s_0_ruby02-a_00"),
    ("npc_servjane",  "/Game/AsdaAssets/Models/NPC/npc_f_h_servjane-a_00/SkeletalMeshes/npc_f_h_servjane-a_00"),
    ("npc_ella",      "/Game/AsdaAssets/Models/NPC/npc_f_h_ella-a_00/SkeletalMeshes/npc_f_h_ella-a_00"),
    ("npc_yazark",    "/Game/AsdaAssets/Models/NPC/npc_m_m_yazark-a_00/SkeletalMeshes/npc_m_m_yazark-a_00"),
    ("mob_mosrd01",   "/Game/AsdaAssets/Models/MobSkel/mon_s_0_mosrd01-a_00/SkeletalMeshes/mon_s_0_mosrd01-a_00"),
    ("mob_rcrob01",   "/Game/AsdaAssets/Models/MobSkel/mon_s_0_rcrob01-a_00/SkeletalMeshes/mon_s_0_rcrob01-a_00"),
    ("mob_kcat03",    "/Game/AsdaAssets/Models/MobSkel/mon_s_0_kcat03-a_00/SkeletalMeshes/mon_s_0_kcat03-a_00"),
    ("mob_greengnom02", "/Game/AsdaAssets/Models/Mob/mon_s_0_greengnom02-a_00/StaticMeshes/mon_s_0_greengnom02-a_00"),
    ("mob_madgear02", "/Game/AsdaAssets/Models/Mob/mon_s_0_madgear02-a_00/StaticMeshes/mon_s_0_madgear02-a_00"),
    ("mob_knightho02", "/Game/AsdaAssets/Models/Mob/mon_s_0_knightho02-a_00/StaticMeshes/mon_s_0_knightho02-a_00"),
]

# ★★★ «ДО» СНИМАЕМ ПЕРЕОПРЕДЕЛЕНИЕМ МАСТЕРА НА КОМПОНЕНТЕ, А НЕ ОТКАТОМ АССЕТОВ.
#   `create_dynamic_material_instance(slot, master)` живёт только у заспавненного актора и НИЧЕГО
#   не пишет в ассеты, поэтому оба кадра снимаются в ОДНОМ прогоне и на ОДНОМ состоянии проекта:
#   различие между ними — РОВНО мастер материала, а не побочные факторы (свет, компиляция шейдеров,
#   версия меша). Раньше «до» снималось отдельным прогоном ДО правки, и часть кадров терялась
#   безвозвратно — пересобрать их потом было уже нельзя.
FORCE_BEFORE = "/Game/Materials/M_AsdaObjectLit"   # мастер, на котором всё сидело до правки
_LIM = os.environ.get("ASDA_SHOT_LIMIT")
if _LIM:
    SHOTS = SHOTS[:int(_LIM)]
# план: (имя файла, пакет меша, принудительный мастер или None)
PLAN = []
for _t, _p in SHOTS:
    PLAN.append(("before_%s" % _t, _p, FORCE_BEFORE))
    PLAN.append(("after_%s" % _t, _p, None))


def main():
    import unreal
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if not os.path.isdir(OUT_DIR):
        os.makedirs(OUT_DIR)

    # ★★★ УРОВЕНЬ БЕРЁМ ГОТОВЫЙ, А НЕ СОЗДАЁМ. Замер (`Tools/_probe_capture.py` и первые прогоны
    #   этого скрипта): в СВЕЖЕМ уровне (`new_level`) headless-редактор отдаёт ЧЁРНЫЙ кадр и через
    #   SceneCapture2D, и через `take_high_res_screenshot` — даже на `/Engine/BasicShapes/Cube` при
    #   свете (центр RT = (0,0,0) на тиках 60/180/360/600). На ЗАГРУЖЕННОЙ с диска карте тот же
    #   `take_high_res_screenshot` работает — это проверенный путь проекта
    #   (`asda_shot_npc.py` → `Reports/ue_npc_alpen.png`, средняя яркость 112.6).
    #   Поэтому грузим карту `alpen` и ставим модели В ВОЗДУХЕ над ней, чтобы в кадр не лез рельеф.
    les.load_level(LEVEL_MAP)
    world = ues.get_editor_world()
    if world is None:
        raise RuntimeError("нет редакторского мира после load_level(%s)" % LEVEL_MAP)
    unreal.log("[кадры] мир=%s" % world.get_name())
    for cmd in ("r.TextureStreaming 0", "r.Streaming.FullyLoadUsedTextures 1", "r.ScreenPercentage 100"):
        unreal.SystemLibrary.execute_console_command(world, cmd)

    STAGE = unreal.Vector(0.0, 0.0, STAGE_Z)
    # ★ Свой ключевой свет: направление солнца карты произвольно, и модель может оказаться целиком
    #   в тени — тогда «до» и «после» одинаково тёмные и сравнивать нечего.
    key = eas.spawn_actor_from_class(unreal.DirectionalLight, STAGE, unreal.Rotator(-35.0, 60.0, 0.0))
    key.light_component.set_editor_property("intensity", 8.0)
    try:
        key.light_component.set_editor_property("cast_shadows", False)
    except Exception:
        pass

    # ★★★ СНИМАЕМ SceneCapture2D, А НЕ take_high_res_screenshot.
    #   Замеры этой задачи:
    #   1) в СВЕЖЕМ уровне (`new_level`) капчер отдаёт ЧЁРНЫЙ кадр даже на `/Engine/BasicShapes/Cube`
    #      со светом (`Tools/_probe_capture.py`: центр RT (0,0,0) на тиках 60/180/360/600), а на
    #      ЗАГРУЖЕННОЙ с диска карте — нормальный (центр (139,213,221) = небо). Отсюда `load_level`.
    #   2) `take_high_res_screenshot` АСИНХРОНЕН и отстаёт: кадр `after_npc_yazark` показал МОБА
    #      вместо NPC — файл записался уже после того, как в сцену встала следующая модель. Плюс
    #      часть запросов терялась (4 из 13). `capture_scene()` + `export_render_target` пишут файл
    #      СИНХРОННО в том же вызове, поэтому кадр гарантированно принадлежит поставленной модели.
    rt = unreal.RenderingLibrary.create_render_target2d(world, RES, RES,
                                                        unreal.TextureRenderTargetFormat.RTF_RGBA8)
    cap = eas.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(0, 0, 12000))
    cc = cap.capture_component2d
    cc.set_editor_property("texture_target", rt)
    cc.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    cc.set_editor_property("fov_angle", 45.0)
    for prop, val in (("b_always_persist_rendering_state", True), ("always_persist_rendering_state", True),
                      ("b_capture_every_frame", False), ("capture_every_frame", False),
                      ("b_capture_on_movement", False), ("capture_on_movement", False)):
        try:
            cc.set_editor_property(prop, val)
        except Exception:
            pass   # имена UPROPERTY у капчера в 5.8 отличаются от 5.3 — перебираем оба написания

    # ★ ФОН. Просвет (вырезанную masked-мастером геометрию) видно только на контрастной подложке:
    #   позади модели ставим ярко-пурпурную плоскость — «дырки» читаются как пурпурные пятна.
    plane = unreal.load_asset("/Engine/BasicShapes/Plane")
    # ★ ОБЩАЯ ТОЧКА с соседним потоком: тот же служебный Unlit-материал фона `M_BlendShotBG`
    #   (asda_shot_blend_models.py). Второго не заводим — иначе кадры двух отчётов несравнимы.
    #   Создаём в /Game/Materials, а не в /Temp: create_asset в /Temp возвращает None.
    BGM_PATH = "/Game/Materials/M_BlendShotBG"
    EALib = unreal.EditorAssetLibrary
    bg_mat = unreal.load_asset(BGM_PATH) if EALib.does_asset_exist(BGM_PATH) else None
    if not bg_mat:
        bg_mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "M_BlendShotBG", "/Game/Materials", unreal.Material, unreal.MaterialFactoryNew())
        if bg_mat:
            MEL = unreal.MaterialEditingLibrary
            bg_mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
            col = MEL.create_material_expression(bg_mat, unreal.MaterialExpressionVectorParameter, -300, 0)
            col.set_editor_property("parameter_name", "Color")
            MEL.connect_material_property(col, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
            MEL.recompile_material(bg_mat)
            EALib.save_loaded_asset(bg_mat)

    st = {"tick": 0, "i": -1, "next_at": WARMUP_TICKS, "ok": 0, "handle": None, "actor": None, "bg": [],           "order": list(range(len(PLAN))), "oi": 0, "staged": None, "pass": 0}

    def place(i):
        tag, pkg, force = PLAN[i]
        if st["actor"]:
            eas.destroy_actor(st["actor"]); st["actor"] = None
        for p in st["bg"]:
            eas.destroy_actor(p)
        st["bg"] = []
        asset = unreal.load_asset(pkg)
        if not asset:
            unreal.log_warning("[кадры] НЕТ ассета %s" % pkg); return False
        if isinstance(asset, unreal.SkeletalMesh):
            a = eas.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(0, 0, 0))
            a.skeletal_mesh_component.set_skeletal_mesh(asset)
        else:
            a = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, 0))
            a.static_mesh_component.set_static_mesh(asset)
        st["actor"] = a
        # ★ Габариты берём У АССЕТА, а не через get_actor_bounds: сразу после set_*_mesh компонент
        #   ещё не пересчитал границы и отдаёт нули — камера уезжает, кадр выходит ЧЁРНЫМ.
        b = asset.get_bounds()
        org = b.origin; ext = b.box_extent
        r = max(ext.x, ext.y, ext.z, 1.0)
        base = unreal.Vector(STAGE.x - org.x, STAGE.y - org.y, STAGE.z - org.z)
        a.set_actor_location(base, sweep=False, teleport=False)
        # ★★★ КАМЕРУ ВЬЮПОРТА СТАВИМ ЯВНО. Замер: `take_high_res_screenshot(..., camera=cam)`
        #   аргумент `camera` НЕ применяет — кадр снимается с камеры перспективного вьюпорта.
        #   Доказательство: модель стояла ровно перед CameraActor (мировые габариты центр
        #   (0,479,11160), полуразмер 9120 при дистанции 23300 — она должна была заполнить кадр
        #   целиком), а в PNG было чистое небо с облаком. После `set_level_viewport_camera_info`
        #   модель появляется.
        eye = unreal.Vector(STAGE.x, STAGE.y - r * 4.2, STAGE.z + r * 0.15)
        look = unreal.MathLibrary.find_look_at_rotation(eye, STAGE)
        cap.set_actor_location(eye, sweep=False, teleport=False)
        cap.set_actor_rotation(look, False)
        # ★ ПУРПУРНАЯ ПОДЛОЖКА позади модели: вырезанные masked-мастером участки читаются как
        #   пурпурные просветы, непрозрачные — как обычная поверхность. Мастер Unlit ⇒ цвет фона
        #   не зависит от освещения и одинаков в кадрах «до» и «после».
        if plane:
            p = eas.spawn_actor_from_class(unreal.StaticMeshActor,
                                           unreal.Vector(STAGE.x, STAGE.y + r * 2.2, STAGE.z))
            smc = p.static_mesh_component
            smc.set_static_mesh(plane)
            if bg_mat:
                mid = smc.create_dynamic_material_instance(0, bg_mat)
                if mid:
                    mid.set_vector_parameter_value("Color", unreal.LinearColor(0.85, 0.10, 0.65, 1.0))
            p.set_actor_rotation(unreal.Rotator(90, 0, 0), False)
            p.set_actor_scale3d(unreal.Vector(r / 20.0, r / 20.0, 1.0))
            st["bg"].append(p)
        comp = a.skeletal_mesh_component if isinstance(asset, unreal.SkeletalMesh) else a.static_mesh_component
        if force:
            # ★ Переопределение ТОЛЬКО на компоненте: берём текстуру из штатного MIC и вешаем её на
            #   динамический инстанс принудительного мастера. Ассеты не меняются.
            fm = unreal.load_asset(force)
            for k in range(comp.get_num_materials()):
                cur = comp.get_material(k)
                tex = None
                try:
                    tex = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(cur, "Tex")
                except Exception:
                    pass
                mid = comp.create_dynamic_material_instance(k, fm)
                if mid and tex:
                    mid.set_texture_parameter_value("Tex", tex)
        slots = []
        for k in range(comp.get_num_materials()):
            mi = comp.get_material(k)
            par = mi.get_editor_property("parent") if isinstance(mi, unreal.MaterialInstanceConstant) else None
            slots.append("%d:%s(%s)" % (k, mi.get_name() if mi else "НЕТ", par.get_name() if par else "—"))
        wo, we = a.get_actor_bounds(False)
        got = None
        try:
            got = comp.get_skeletal_mesh_asset() if isinstance(asset, unreal.SkeletalMesh) else comp.get_editor_property("static_mesh")
        except Exception as e:
            got = "ошибка: %s" % e
        unreal.log("[кадры] %s меш в компоненте=%s мировые габариты центр=(%.0f,%.0f,%.0f) полураз=(%.0f,%.0f,%.0f)"
                   % (tag, (got.get_name() if hasattr(got, "get_name") else got),
                      wo.x, wo.y, wo.z, we.x, we.y, we.z))
        unreal.log("[кадры] %s r=%.0f модель=(%.0f,%.0f,%.0f) камера=(%.0f,%.0f,%.0f)"
                   % (tag, r, base.x, base.y, base.z,
                      cap.get_actor_location().x, cap.get_actor_location().y, cap.get_actor_location().z))
        unreal.log("[кадры] %s слоты: %s" % (tag, "; ".join(slots)))
        return True

    def grab(i):
        """СИНХРОННЫЙ снимок: capture_scene() рисует в RT, export_render_target пишет PNG тут же."""
        tag = PLAN[i][0]
        fn = "%s.png" % tag
        dst = os.path.join(OUT_DIR, fn)
        if os.path.exists(dst):
            os.remove(dst)
        cc.capture_scene()          # ★ без явного вызова экспортируется кадр от прошлого тика
        unreal.RenderingLibrary.export_render_target(world, rt, OUT_DIR, fn)
        ok = os.path.exists(dst)
        unreal.log("[кадры] снято %s -> %s" % (fn, "ОК" if ok else "★ ПУСТО"))
        return ok

    def finish():
        if st["handle"] is not None:
            unreal.unregister_slate_post_tick_callback(st["handle"]); st["handle"] = None
        unreal.log("=== кадры: сохранено %d из %d ===" % (st["ok"], len(PLAN)))
        unreal.SystemLibrary.quit_editor()

    def step():
        """Машина состояний: поставить модель → дать кадрам устояться → снять → следующая.

        ★ Снимок синхронный, поэтому «ожидания файла» нет. Осталась только пауза SETTLE_TICKS
          между постановкой модели и снимком: свежему мешу нужно время на компиляцию шейдеров и
          стриминг текстур, иначе он выходит белым (грабля normal-from-albedo-studio).
        ★ Финальная сверка с диском оставлена: если какой-то кадр всё же не записался, позиция
          ставится и снимается заново (до 3 проходов).
        """
        st["tick"] += 1
        if st["tick"] > MAX_TICKS:
            unreal.log_error("[кадры] предел тиков"); finish(); return
        if st["tick"] < st["next_at"]:
            return
        if st["staged"] is not None:
            idx = st["staged"]; st["staged"] = None
            if grab(idx):
                st["ok"] += 1
            st["next_at"] = st["tick"] + 2
            return
        while st["oi"] < len(st["order"]):
            idx = st["order"][st["oi"]]; st["oi"] += 1
            if place(idx):
                st["staged"] = idx
                st["next_at"] = st["tick"] + SETTLE_TICKS
                return
        missing = [k for k in range(len(PLAN))
                   if not os.path.exists(os.path.join(OUT_DIR, "%s.png" % PLAN[k][0]))]
        if missing and st["pass"] < 3:
            st["pass"] += 1
            unreal.log_warning("[кадры] проход %d: не записалось %d кадров, повторяю: %s"
                               % (st["pass"], len(missing), ", ".join(PLAN[k][0] for k in missing)))
            st["order"] = missing; st["oi"] = 0
            st["next_at"] = st["tick"] + SETTLE_TICKS
            return
        st["ok"] = len(PLAN) - len(missing)
        if missing:
            unreal.log_warning("[кадры] ★ ТАК И НЕ СНЯТЫ: %s" % ", ".join(PLAN[k][0] for k in missing))
        finish()

    def on_tick(_dt):
        # ★ исключение внутри slate-колбэка движок ГЛОТАЕТ ⇒ цикл зависнет: ловим сами.
        try:
            step()
        except Exception:
            import traceback
            unreal.log_error("[кадры] ошибка:\n" + traceback.format_exc())
            finish()

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    unreal.log("=== кадры: цикл запущен, прогрев %d тиков, кадров в плане %d ===" % (WARMUP_TICKS, len(PLAN)))


if __name__ == "__main__" or globals().get("__spec__") is None:
    try:
        main()
    except Exception:
        import traceback
        import unreal
        unreal.log_error("[кадры] СБОЙ:\n" + traceback.format_exc())
        unreal.SystemLibrary.quit_editor()
