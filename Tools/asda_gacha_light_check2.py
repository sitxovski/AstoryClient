# Copyright (c) 2026 @sitxovski (Discord)
# ⚠⚠⚠ УСТАРЕЛ 2026-08-18. Этот зонд мерил освещение по формуле `.fx`-пути
#   (`EmptyProject.fx` `VShade`), а сцена гачи этот путь ВЫКЛЮЧАЕТ (`g_bUseEffectPath = 0`).
#   Автоматы рисуются НЕОСВЕЩЁННЫМ вершинным шейдером (`mr+0x004 & 0x8 == 0` у всех 100
#   подсеток), множитель к текстуре тождественно 1.0. Варианты `before/after` и параметры
#   `Ambient`/`LightColor`/`LightDir` у мастеров гачи БОЛЬШЕ НЕ СУЩЕСТВУЮТ — их запись
#   в MID теперь ничего не делает, и цифры этого зонда бессмысленны.
#   Актуальный замер — `asda_gacha_color_check.py`; разбор — `Reports/gacha_color_fix.md`.
#   Файл оставлен как история метода (абсолютный замер через байты текстуры).
# asda_gacha_light_check2.py — АБСОЛЮТНЫЙ замер яркости сцены гачи (жалоба «тень серая,
# лутбоксы пересвечены», ПОВТОРНАЯ).
#
# ★ ЧЕМ ОТЛИЧАЕТСЯ ОТ asda_gacha_light_check.py. Прежний зонд мерил ОТНОШЕНИЕ двух своих же
#   кадров (эталон / альбедо) и потому не мог поймать ошибку в САМОЙ формуле: любое неверное
#   направление света даёт «отношение в коридоре», если коридор считать по той же формуле.
#   Здесь мерится АБСОЛЮТ: цвет квада тени в кадре сравнивается с ЧИСЛОМ ИЗ ТЕКСТУРЫ
#   (`RawAssets/Textures/Data/Character/Texture/gachya_shadow.png`, ядро a>=0.75: RGB (34,33,33)).
#
#   Эталон оригинала для квада тени: квад лежит в плоскости пола, его нормаль = (0,1,0) в
#   КЛИЕНТСКИХ осях (проверено по нормалям .glb). Направление НА СВЕТ в шейдере — глобал
#   `g_vLightDir` (flt_3528D74), init движка (`model_anim.c:24334`) = (0,0,-1), горизонтально.
#   => `dot(N, lhtDir) = 0` => множитель = только `m_MatAmbient` = **0.6** (техника `tSkin`,
#   `VShade`: `MaterialAmbient + max(0,N*lhtDir)*MaterialDiffuse`, живой дамп 0.6 и 0.8).
#   Значит ядро тени обязано выйти 34 * 0.6 ~= 20 из 255 — тёмным.
#
#   Перевод осей `AsdaCoord::GameToUe3D(x,y,z) = (-z, x, y)` даёт «на свет» в UE = (1,0,0);
#   материал считает `max(0, dot(N, -LightDir))`, поэтому `LightDir` в UE = **(-1,0,0)**.
#   В коде до правки стояло (0,0,-1) — «свет сверху», отчего горизонтальный квад тени получал
#   полный диффуз (множитель 1.28 вместо 0.6).
#
# Варианты кадра:
#   albedo — LightDir = (0,0,0), LightColor = 1, Ambient = 1 => `lite = 1*1 + 1*max(0, dot(N,0)) = 1`
#            ТОЖДЕСТВЕННО НА ВСЕХ поверхностях => на выходе ДОЛЖНА быть ровно текстура. Это
#            и замер ПЕРЕДАТОЧНОЙ ФУНКЦИИ цепочки (экспозиция + тонмаппер + sRGB + RT), и база
#            для поэлементного отношения «кадр / альбедо» = фактический множитель.
#            ★ Восстанавливать собственный цвет квада из кадра надо ЧЕРЕЗ sRGB, а не делением:
#            смешивание идёт в ЛИНЕЙНОМ scene color до тонмаппера, поэтому `C = enc(dec(RT)/a)`.
#   before — как в коде до правки: LightDir = (0,0,-1) («свет сверху»).
#   after  — исправленное: LightDir = (1,0,0) (горизонтально, из-за камеры).
#
# Для каждого варианта снимается ДВА набора: полный кадр и кадр с ИЗОЛИРОВАННЫМ слотом тени
# (остальные слоты подменены полностью прозрачным материалом), чтобы цвет тени мерился начисто.
#
# ★★★ ГРАБЛЯ: при headless-запуске движок не тикает во время скрипта => машина состояний на
#   `register_slate_post_tick_callback`, БЕЗ `-NullRHI`, перед экспортом обязательный
#   `capture_scene()`. Запуск: run_headless_rhi_tick.bat <этот файл>
import os

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/gacha_3d"
# ★ Уровень PIE, а не пустая сцена: прошлый заход мерил в `/Temp/L_*`, где нет ни объёмов
#   пост-обработки, ни света уровня, — то есть НЕ в том, что видит владелец.
# ★ 16.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 17): БЫЛО умолчание "/Game/Map/Login" — зонд ставил акторы в боевой уровень
#   логина (свет владельца). Теперь без GACHA_PROBE_LEVEL=<путь уровня> — явная ошибка; уровень логина — только с ASDA_ALLOW_LOGIN_WRITE=1.
LEVEL = os.environ.get("GACHA_PROBE_LEVEL", "")
MACHINES = 1                 # один автомат: два квада тени перекрываются и портят замер
WARMUP_TICKS = 480
SETTLE_TICKS = 150
MAX_TICKS = 200000
TRANSPARENT_MASTER = "/Game/Materials/M_AsdaGachaGlass"   # Opacity = Tex.A * GlassOpacity => 0 = невидимо
SHADOW_SLOT_KEY = "gachya_shadow"

# (имя, Ambient, LightColor, LightDir)
VARIANTS = [
    ("albedo", 1.00, 1.0, ( 0.0, 0.0,  0.0)),
    ("before", 0.60, 0.8, ( 0.0, 0.0, -1.0)),
    ("after",  0.75, 0.8, (-1.0, 0.0,  0.0)),
]


def main():
    import unreal
    # ★ 16.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 17): уровень — только явный; логин — только с разрешения владельца
    #   (зонд ставит акторы в уровень). Как у asda_gacha_color_check.py / asda_gacha_shadow_check.py (раздел 16).
    if not LEVEL:
        raise RuntimeError("[гача-свет2] не задан уровень: GACHA_PROBE_LEVEL=<путь уровня> (умолчания /Game/Map/Login больше нет)")
    import sys
    if os.path.dirname(os.path.abspath(__file__)) not in sys.path:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # Tools — для asda_login_guard
    import asda_login_guard as _LG
    if _LG.is_login_owned_package(LEVEL):
        _LG.forbid_login_write("asda_gacha_light_check2.py", "акторы зонда в уровень %s" % LEVEL)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if not os.path.isdir(OUT_DIR):
        os.makedirs(OUT_DIR)

    if not les.load_level(LEVEL):
        unreal.log_error("[гача-свет2] не открылся уровень %s" % LEVEL)
        # ★ 16.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 18): старт сорван — колбэк тика не зарегистрирован, редактор гасится здесь.
        #   БЫЛО: return без quit_editor — редактор висел процессом (раздел 17.9, п.3).
        unreal.SystemLibrary.quit_editor()
        return
    world = ues.get_editor_world()
    for cmd in ("r.TextureStreaming 0", "r.Streaming.FullyLoadUsedTextures 1", "r.ScreenPercentage 100"):
        unreal.SystemLibrary.execute_console_command(world, cmd)

    # ★ Тот же самый спаун, что делает подсистема (`EnsureSceneActor`): далеко от игрока.
    act = eas.spawn_actor_from_class(unreal.AsdaGachaSceneActor, unreal.Vector(0, 0, 200000))
    act.set_actor_label("GachaLight2")
    act.build_machines(MACHINES)
    act.play_clip_all(1, True)

    comps = list(act.get_components_by_class(unreal.SkeletalMeshComponent))
    for c in comps:
        try:
            c.set_update_animation_in_editor(True)
        except Exception:
            pass
    slot_names = []
    if comps:
        for i in range(comps[0].get_num_materials()):
            mi = comps[0].get_material(i)
            slot_names.append(mi.get_name() if mi else "ПУСТО")
    unreal.log("[гача-свет2] слоты: %s" % ", ".join("%d:%s" % (i, n) for i, n in enumerate(slot_names)))
    shadow_slots = [i for i, n in enumerate(slot_names) if SHADOW_SLOT_KEY in n.lower()]
    unreal.log("[гача-свет2] слот(ы) тени: %s" % shadow_slots)

    master_inv = unreal.load_asset(TRANSPARENT_MASTER)
    orig = [[c.get_material(i) for i in range(c.get_num_materials())] for c in comps]

    st = {"tick": 0, "step": -1, "next_at": WARMUP_TICKS, "ok": 0, "handle": None, "iso": False}

    def set_light(amb, lcol, ldir):
        """То же, что `AAsdaGachaSceneActor::ApplyLightParams`, но без пересборки автоматов:
        пересборка снесла бы изоляцию слотов."""
        for c in comps:
            for i in range(c.get_num_materials()):
                mid = c.create_dynamic_material_instance(i)
                if not mid:
                    continue
                mid.set_vector_parameter_value("LightDir", unreal.LinearColor(ldir[0], ldir[1], ldir[2], 0.0))
                mid.set_vector_parameter_value("LightColor", unreal.LinearColor(lcol, lcol, lcol, 0.0))
                mid.set_scalar_parameter_value("Ambient", amb)

    def isolate(on):
        """Оставить видимым ТОЛЬКО слот тени (или вернуть всё как было)."""
        for ci, c in enumerate(comps):
            for i in range(c.get_num_materials()):
                if on and i not in shadow_slots:
                    inv = unreal.MaterialLibrary.create_dynamic_material_instance(act, master_inv)
                    inv.set_scalar_parameter_value("GlassOpacity", 0.0)
                    c.set_material(i, inv)
                elif not on:
                    c.set_material(i, orig[ci][i])
        st["iso"] = on

    def grab(name):
        for c in act.get_components_by_class(unreal.SceneCaptureComponent2D):
            c.capture_scene()
        # ★ RT ОДИН: альфа лежит в нём же (капчер-маска убран, см. gacha_alpha_composite.md).
        for tgt, suffix in ((act.get_render_target(), "color"),):
            fn = "lc2_%s_%s.png" % (name, suffix)
            dst = os.path.join(OUT_DIR, fn)
            if os.path.exists(dst):
                os.remove(dst)
            unreal.RenderingLibrary.export_render_target(world, tgt, OUT_DIR, fn)
            st["ok"] += 1 if os.path.exists(dst) else 0
            unreal.log("[гача-свет2] кадр %s: %s" % (fn, "ok" if os.path.exists(dst) else "НЕ СОХРАНЁН"))

    # план: для каждого варианта — полный кадр и изолированная тень
    PLAN = []
    for name, amb, lcol, ldir in VARIANTS:
        PLAN.append((name + "_full", amb, lcol, ldir, False))
        PLAN.append((name + "_shadow", amb, lcol, ldir, True))

    def finish():
        if st["handle"] is not None:
            unreal.unregister_slate_post_tick_callback(st["handle"])
            st["handle"] = None
        unreal.log("=== гача-свет2: сохранено %d из %d кадров ===" % (st["ok"], 2 * len(PLAN)))
        unreal.SystemLibrary.quit_editor()

    def step_tick():
        st["tick"] += 1
        if st["tick"] > MAX_TICKS:
            unreal.log_error("[гача-свет2] предел тиков")
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
        name, amb, lcol, ldir, iso = PLAN[st["step"]]
        if iso != st["iso"]:
            isolate(iso)
        set_light(amb, lcol, ldir)
        act.play_clip_all(1, True)
        unreal.log("[гача-свет2] вариант %s: Ambient=%.2f LightColor=%.2f LightDir=%s изоляция=%s"
                   % (name, amb, lcol, ldir, iso))
        st["next_at"] = st["tick"] + SETTLE_TICKS

    def on_tick(_dt):
        try:
            step_tick()
        except Exception:
            import traceback
            unreal.log_error("[гача-свет2] ошибка:\n" + traceback.format_exc())
            finish()

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    unreal.log("=== гача-свет2: цикл запущен, шагов %d, уровень %s ===" % (len(PLAN), LEVEL))


if __name__ == "__main__":
    try:
        main()
    except Exception:
        import traceback
        import unreal
        unreal.log_error("[гача-свет2] ошибка старта:\n" + traceback.format_exc())
        # ★ 16.09.2026 (раздел 17): редактор гасится и при ошибке старта (нет GACHA_PROBE_LEVEL и т.п.). БЫЛО: висел процессом.
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
