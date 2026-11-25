# Copyright (c) 2026 @sitxovski (Discord)
# asda_gacha_texture_check.py — ДОКАЗАТЕЛЬСТВО привязки текстур пяти автоматов премиум-гачи.
#
# ★ ЗАЧЕМ. В оригинальных данных `md_gachya_d_00.jbm` четыре записи matTable, и шесть подсеток
#   корпуса/купола ссылаются на ЧУЖУЮ `md_Gachya_e_00.TEX` — четвёртый автомат носил раскраску
#   пятого. Разбор и исправление — `Reports/gacha_model_d_texture.md`. Зонд закрывает проверку
#   двумя способами сразу:
#     1) ЧИСЛА: для каждой скелеталки печатает слот → MIC → реально назначенная текстура «Tex»
#        и ругается, если слот `md_gachya_*` несёт текстуру ЧУЖОЙ модели;
#     2) КАДР: сцена строится НАСТОЯЩИМ `AsdaGachaSceneActor` на все ПЯТЬ типов автомата
#        (`BuildMachines(5)` грузит `md_gachya_a..e` по индексу) и снимается его же капчером
#        ДВАЖДЫ — `gacha_tex_row_before.png` (дефект оригинала воспроизведён ДИНАМИЧЕСКИМ
#        материалом, ассеты не трогаются) и `gacha_tex_row_after.png` (как теперь).
#
# ★ ПОЧЕМУ КАДР СНИМАЕТ АКТОР, А НЕ СВОЙ `SceneCapture2D`. Свой капчер в пустом `/Temp`-уровне
#   давал ЧЁРНЫЙ кадр при любом кадрировании: у актора капчер настроен в C++
#   (`bCaptureEveryFrame`, `PRM_UseShowOnlyList` со списком автоматов, прозрачный ClearColor,
#   выключенные атмосфера/туман, `AlwaysTickPoseAndRefreshBones`, `bForceMipStreaming`), и
#   воспроизводить это из Python значило бы держать вторую копию настроек.
#
# ★★★ ГРАБЛЯ ПРОЕКТА (как и у остальных гача-зондов): при `-ExecutePythonScript` движок НЕ ТИКАЕТ
#   во время скрипта и выходит сразу после него — `capture_scene()` отработает, но RT останется
#   от раннего кадра (до компиляции шейдеров), а `export_render_target` МОЛЧА не запишет файл.
#   Поэтому здесь машина состояний на `register_slate_post_tick_callback`, и запускать надо
#   БЕЗ `-NullRHI`:  Tools\run_headless_rhi_tick.bat Tools\asda_gacha_texture_check.py
import os

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/gacha_3d"
# Уровень тот же, что у замера освещения: реальная карта проекта, а не пустышка.
# ★ 16.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 17): БЫЛО умолчание "/Game/Map/Login" — зонд ставил акторы в боевой уровень
#   логина (свет владельца). Теперь без GACHA_PROBE_LEVEL=<путь уровня> — явная ошибка; уровень логина — только с ASDA_ALLOW_LOGIN_WRITE=1.
LEVEL = os.environ.get("GACHA_PROBE_LEVEL", "")
DEST = "/Game/AsdaAssets/Models/Skeletal/Gachya"
MODELS = ["md_gachya_%s_00" % c for c in "abcde"]
# Два кадра: «как в оригинале» (дефект воспроизведён динамическим материалом) и «как теперь».
SHOT_BEFORE = "gacha_tex_row_before.png"
SHOT_AFTER = "gacha_tex_row_after.png"
DEFECT_MODEL = "md_gachya_d_00"      # автомат, у которого дефект в оригинальных данных
DEFECT_TEX = "/Game/AsdaAssets/Textures/Characters/Texture/md_gachya_e_00"   # чужая текстура
MACHINES = 5                 # ВСЕ пять типов: BuildMachines грузит модель по индексу автомата
CLIP_SLOT = 1                # клип «покой» — корпус и купол на виду, шары внутри
WARMUP_TICKS = 600           # компиляция шейдеров + стриминг текстур
SETTLE_TICKS = 90            # после подмены материала — дать кадру осесть
MAX_TICKS = 200000


def main():
    import unreal
    # ★ 16.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 17): уровень — только явный; логин — только с разрешения владельца
    #   (зонд ставит акторы в уровень). Как у asda_gacha_color_check.py / asda_gacha_shadow_check.py (раздел 16).
    if not LEVEL:
        raise RuntimeError("[гача-тек] не задан уровень: GACHA_PROBE_LEVEL=<путь уровня> (умолчания /Game/Map/Login больше нет)")
    import sys
    if os.path.dirname(os.path.abspath(__file__)) not in sys.path:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # Tools — для asda_login_guard
    import asda_login_guard as _LG
    if _LG.is_login_owned_package(LEVEL):
        _LG.forbid_login_write("asda_gacha_texture_check.py", "акторы зонда в уровень %s" % LEVEL)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if not os.path.isdir(OUT_DIR):
        os.makedirs(OUT_DIR)

    if not les.load_level(LEVEL):
        unreal.log_error("[гача-тек] не открылся уровень %s" % LEVEL)
        # ★ 16.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 18): старт сорван — колбэк тика не зарегистрирован, редактор гасится здесь.
        #   БЫЛО: return без quit_editor — редактор висел процессом (раздел 17.9, п.3).
        unreal.SystemLibrary.quit_editor()
        return
    world = ues.get_editor_world()
    for cmd in ("r.TextureStreaming 0", "r.Streaming.FullyLoadUsedTextures 1", "r.ScreenPercentage 100"):
        unreal.SystemLibrary.execute_console_command(world, cmd)

    # ---------------------------------------------------------------- ЧИСЛА: слот → MIC → текстура
    bad = 0
    for m in MODELS:
        sk = unreal.load_asset("%s/%s/SkeletalMeshes/%s" % (DEST, m, m))
        if not sk:
            unreal.log_error("[гача-тек] НЕТ скелеталки %s" % m)
            bad += 1
            continue
        parts = []
        for i, sl in enumerate(sk.get_editor_property("materials") or []):
            mi = sl.get_editor_property("material_interface")
            if not mi:
                parts.append("%d:ПУСТО" % i)
                bad += 1
                continue
            t = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(mi, "Tex")
            tn = t.get_name() if t else "(нет)"
            parts.append("%d:%s→%s" % (i, mi.get_name(), tn))
            # ★ ПРАВИЛО: слот, чьё имя начинается с `md_gachya_`, обязан нести текстуру СВОЕЙ модели
            if mi.get_name().lower().startswith("md_gachya_") and tn.lower() != m.lower():
                unreal.log_error("[гача-тек] %s слот %d (%s): текстура %s, ждали %s"
                                 % (m, i, mi.get_name(), tn, m))
                bad += 1
        unreal.log("[гача-тек] %s :: %s" % (m, " | ".join(parts)))
    unreal.log("[гача-тек] несоответствий «текстура своей модели»: %d" % bad)

    # ---------------------------------------------------------------- КАДР: все пять автоматов
    # Спаун как у подсистемы (`EnsureSceneActor`): далеко от игрока, транзиентный актор.
    act = eas.spawn_actor_from_class(unreal.AsdaGachaSceneActor, unreal.Vector(0, 0, 200000))
    act.set_actor_label("GachaTexCheck")
    act.build_machines(MACHINES)
    act.play_clip_all(CLIP_SLOT, True)
    comps = list(act.get_components_by_class(unreal.SkeletalMeshComponent))
    for c in comps:
        try:
            c.set_update_animation_in_editor(True)   # в редакторском мире поза иначе не считается
        except Exception:
            pass
    caps = act.get_components_by_class(unreal.SceneCaptureComponent2D)
    rt = act.get_render_target()
    cap = next((c for c in caps if c.get_editor_property("texture_target") == rt), caps[0])
    unreal.log("[гача-тек] автоматов %d, скелеталок %d, RT %dx%d"
               % (act.get_machine_count(), len(comps), rt.size_x if rt else -1, rt.size_y if rt else -1))

    # ★ ВОСПРОИЗВЕДЕНИЕ ДЕФЕКТА ОРИГИНАЛА ДЛЯ КАДРА «ДО». Ассеты НЕ трогаем: на слоты корпуса
    #   автомата `d` вешается ДИНАМИЧЕСКИЙ инстанс с чужой текстурой — ровно то, что делали
    #   исходные данные (`matIdx` шести подсеток указывал на `md_Gachya_e_00.TEX`).
    # ★ Слоты корпуса ищем по МЕШУ, а не по материалу компонента: `ApplyLightParams` уже подменил
    #   материалы компонента динамическими инстансами (`MaterialInstanceDynamic_N`), и проверка
    #   «имя начинается с md_gachya_» на компоненте не срабатывает — первая версия зонда так и
    #   получила «слотов 0» и два одинаковых кадра.
    defect_comp = next((c for c in comps
                        if c.get_skinned_asset() and c.get_skinned_asset().get_name() == DEFECT_MODEL), None)
    defect_slots = []
    if defect_comp:
        for i, sl in enumerate(defect_comp.get_skinned_asset().get_editor_property("materials") or []):
            mi = sl.get_editor_property("material_interface")
            if mi and mi.get_name().lower().startswith("md_gachya_"):
                defect_slots.append(i)

    def defect_on(on):
        """on=True — наложить дефект оригинала (чужая текстура), on=False — вернуть свою.
        Меняется ТОЛЬКО параметр `Tex` динамического инстанса ⇒ параметры света не теряются."""
        if not defect_comp:
            return 0
        tex = unreal.load_asset(DEFECT_TEX if on else
                                "/Game/AsdaAssets/Textures/Characters/Texture/" + DEFECT_MODEL)
        n = 0
        for i in defect_slots:
            mid = defect_comp.create_dynamic_material_instance(i)
            if mid:
                mid.set_texture_parameter_value("Tex", tex)
                n += 1
        return n

    st = {"tick": 0, "handle": None, "ok": 0, "step": 0}

    def finish():
        if st["handle"] is not None:
            unreal.unregister_slate_post_tick_callback(st["handle"])
            st["handle"] = None
        unreal.log("=== гача-тек: кадров сохранено %d, несоответствий %d ===" % (st["ok"], bad))
        unreal.SystemLibrary.quit_editor()

    def dump_screen_x():
        """Кто где на кадре: экранный X якоря каждого слота (веер 0°, +27°, −27°, +54°, −54°)."""
        parts = []
        for i in range(act.get_machine_count()):
            res = act.get_slot_screen_pos(i)
            p = res[1] if isinstance(res, tuple) else res
            parts.append("%s x=%.0f" % (MODELS[i], p.x))
        unreal.log("[гача-тек] экранные позиции слева направо: %s"
                   % "; ".join(sorted(parts, key=lambda s: float(s.rsplit("=", 1)[1]))))

    def shoot(fn):
        # ★ ОБЯЗАТЕЛЬНО: без явного capture_scene() в RT остаётся ранний кадр (до компиляции
        #   шейдеров) — модели выходят белыми, симптом неотличим от испорченных материалов.
        for c in caps:
            c.capture_scene()
        dst = os.path.join(OUT_DIR, fn)
        if os.path.exists(dst):
            os.remove(dst)
        unreal.RenderingLibrary.export_render_target(world, rt, OUT_DIR, fn)
        got = os.path.exists(dst)
        st["ok"] += 1 if got else 0
        unreal.log("[гача-тек] кадр %s -> %s" % (fn, "ОК" if got else "★ ПУСТО"))

    def step_tick():
        st["tick"] += 1
        if st["tick"] < WARMUP_TICKS:
            return
        if st["tick"] > MAX_TICKS:
            finish()
            return
        if st["step"] == 0:                       # кадр «ДО»: дефект оригинала воспроизведён
            unreal.log("[гача-тек] дефект оригинала наложен на %s: слотов %d"
                       % (DEFECT_MODEL, defect_on(True)))
            st["step"] = 1
            st["tick"] = WARMUP_TICKS - SETTLE_TICKS   # дать материалу осесть
            return
        if st["step"] == 1:
            shoot(SHOT_BEFORE)
            unreal.log("[гача-тек] дефект снят: слотов возвращено %d" % defect_on(False))
            st["step"] = 2
            st["tick"] = WARMUP_TICKS - SETTLE_TICKS
            return
        shoot(SHOT_AFTER)
        dump_screen_x()
        finish()

    def on_tick(_dt):
        # ★ исключение внутри slate-колбэка движок ГЛОТАЕТ: без try зонд зациклится молча
        try:
            step_tick()
        except Exception:
            import traceback
            unreal.log_error("[гача-тек] ошибка:\n" + traceback.format_exc())
            finish()

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)


# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    # ★ 16.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 17): при исключении старта (нет GACHA_PROBE_LEVEL, стоп защиты логина
    #   SystemExit(3)) редактор гасится; исключение пробрасывается как раньше (код выхода сохраняется).
    try:
        main()
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
