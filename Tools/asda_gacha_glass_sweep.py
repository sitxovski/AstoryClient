# Copyright (c) 2026 @sitxovski (Discord)
# asda_gacha_glass_sweep.py — ЗАМЕР непрозрачности стеклянного купола автомата гачи.
#
# Зачем. Реверс (см. шапку `asda_build_gacha_glass_material.py` и раздел «Непрозрачность купола»
# в `Reports/gacha_materials_fix.md`) доказал: альфа помеченного меша в оригинале =
# альфа текстуры × фейд объекта, у купола это 1×1 = 1.0, то есть ОРИГИНАЛ РИСУЕТ ЕГО
# НЕПРОЗРАЧНЫМ, и девять шаров `Object01…Object10` внутри не видны никогда — дефект оригинала.
# Числа для исправления в данных нет, поэтому величину выбираем ЗАМЕРОМ по кадру, а не на глаз.
#
# Что снимаем: один и тот же кадр при `GlassOpacity` = 1.00 / 0.75 / 0.50 / 0.25.
# Что меряем потом (скриптом сравнения, кадры в `Reports/gacha_3d/glass_*.png`):
#   · ВИДИМОСТЬ ШАРОВ — доля пикселей области купола, отличающихся от непрозрачного эталона;
#   · СОХРАННОСТЬ СОБСТВЕННОГО РИСУНКА КУПОЛА — контраст (СКО яркости) в той же области:
#     у стекла обязаны остаться нарисованные вертикальные блики, иначе купол «исчезает».
#
# ★★★ ГРАБЛЯ ПРОЕКТА: при `-ExecutePythonScript` движок не тикает — машина состояний на
# `register_slate_post_tick_callback`, запуск через `-ExecCmds="py …"` БЕЗ `-NullRHI`,
# перед каждым экспортом обязателен явный `capture_scene()`.
import os

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/gacha_3d"
LEVEL = "/Temp/L_GachaGlass"
MACHINES = 2
GACHYA_ROOT = "/Game/AsdaAssets/Models/Skeletal/Gachya"
VALUES = [1.00, 0.75, 0.50, 0.25]

WARMUP_TICKS = 420
SETTLE_TICKS = 90
MAX_TICKS = 40000


def main():
    import unreal
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    MEL = unreal.MaterialEditingLibrary
    AR = unreal.AssetRegistryHelpers.get_asset_registry()
    if not os.path.isdir(OUT_DIR):
        os.makedirs(OUT_DIR)

    # все MIC купола (имя слота содержит `_blend`, но это не тень)
    glass = []
    for ad in AR.get_assets_by_path(unreal.Name(GACHYA_ROOT), recursive=True):
        if str(ad.asset_class_path.asset_name) != "MaterialInstanceConstant":
            continue
        nm = str(ad.asset_name).lower()
        if "_blend" in nm and not nm.startswith("gachya_shadow"):
            a = unreal.load_asset(str(ad.package_name))
            if a:
                glass.append(a)
    unreal.log("[гача-стекло-замер] MIC купола: %d" % len(glass))

    les.new_level(LEVEL)
    world = ues.get_editor_world()
    for cmd in ("r.TextureStreaming 0", "r.Streaming.FullyLoadUsedTextures 1", "r.ScreenPercentage 100"):
        unreal.SystemLibrary.execute_console_command(world, cmd)

    act = eas.spawn_actor_from_class(unreal.AsdaGachaSceneActor, unreal.Vector(0, 0, 0))
    act.set_actor_label("GachaGlassSweep")
    act.build_machines(MACHINES)
    act.play_clip_all(1, True)
    for c in act.get_components_by_class(unreal.SkeletalMeshComponent):
        try:
            c.set_update_animation_in_editor(True)
        except Exception:
            pass

    st = {"tick": 0, "step": -1, "next_at": WARMUP_TICKS, "ok": 0, "handle": None}

    def apply(step):
        v = VALUES[step]
        for m in glass:
            MEL.set_material_instance_scalar_parameter_value(m, "GlassOpacity", v)
        unreal.log("[гача-стекло-замер] GlassOpacity = %.2f" % v)

    def grab(step):
        v = VALUES[step]
        for c in act.get_components_by_class(unreal.SceneCaptureComponent2D):
            c.capture_scene()          # ★ иначе экспортируется кадр ДО правки материала
        fn = "glass_%03d.png" % int(round(v * 100))
        dst = os.path.join(OUT_DIR, fn)
        if os.path.exists(dst):
            os.remove(dst)
        unreal.RenderingLibrary.export_render_target(world, act.get_render_target(), OUT_DIR, fn)
        ok = os.path.exists(dst)
        st["ok"] += 1 if ok else 0
        unreal.log("[гача-стекло-замер] снято %s -> %s" % (fn, "ОК" if ok else "★ ПУСТО"))

    def finish():
        if st["handle"] is not None:
            unreal.unregister_slate_post_tick_callback(st["handle"])
            st["handle"] = None
        unreal.log("=== гача-стекло-замер: сохранено %d из %d ===" % (st["ok"], len(VALUES)))
        unreal.SystemLibrary.quit_editor()

    def step_tick():
        st["tick"] += 1
        if st["tick"] > MAX_TICKS:
            unreal.log_error("[гача-стекло-замер] предел тиков")
            finish(); return
        if st["tick"] < st["next_at"]:
            return
        if st["step"] >= 0:
            grab(st["step"])
        st["step"] += 1
        if st["step"] >= len(VALUES):
            finish(); return
        apply(st["step"])
        st["next_at"] = st["tick"] + SETTLE_TICKS

    def on_tick(_dt):
        try:
            step_tick()
        except Exception:
            import traceback
            unreal.log_error("[гача-стекло-замер] ошибка в шаге:\n" + traceback.format_exc())
            finish()

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    unreal.log("=== гача-стекло-замер: цикл запущен, прогрев %d кадров ===" % WARMUP_TICKS)


if __name__ == "__main__":
    try:
        main()
    except Exception:
        import traceback
        import unreal
        unreal.log_error("[гача-стекло-замер] СБОЙ:\n" + traceback.format_exc())
        unreal.SystemLibrary.quit_editor()
