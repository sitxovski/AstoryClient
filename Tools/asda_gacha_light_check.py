# Copyright (c) 2026 @sitxovski (Discord)
# asda_gacha_light_check.py — ЗАМЕР ЯРКОСТИ автоматов гачи против формулы оригинала.
#
# Жалоба владельца: «лутбоксы ярковатые».
#
# Эталон (`system/EmptyProject.fx`, `VShade` + `PShade`; сцена света не ставит —
# `PetWindow_RenderToTarget@0xB6D5B0` не содержит ни одного вызова источника):
#     RGB = Tex.xyz · LightColor · (Ambient + max(0, dot(N, −LightDir)))
# при эталонных m_MatAmbient = 0.6 и m_LightColor = 0.8 (живой дамп, lighting_analysis.md)
# ⇒ множитель к альбедо строго 0.48 … 1.28, и это ПРОВЕРЯЕМО ПОКАДРОВО:
#
#   кадр `light_albedo` — тот же ракурс при LightGamma = 0 ⇒ множитель = lite^0 = 1.0 ТОЖДЕСТВЕННО
#                          в каждом пикселе, то есть кадр = ЧИСТОЕ альбедо (эталон сравнения);
#   кадр `light_etalon` — эталонные Ambient = 0.6, LightColor = 0.8.
#   Поэлементное отношение `light_etalon / light_albedo` в силуэте автомата ОБЯЗАНО лежать
#   в [0.48, 1.28]; его гистограмма и есть доказательство, что яркость приведена к оригиналу.
#
# ★ Тон-кривая капчера выключена актором (`ToneCurveAmount = 0`), иначе отношение перестаёт быть
#   линейным и такая проверка невозможна.
#
# ★★★ ГРАБЛЯ: при headless-запуске движок не тикает во время скрипта ⇒ машина состояний на
#   `register_slate_post_tick_callback`, БЕЗ `-NullRHI`, перед экспортом обязательный
#   `capture_scene()`. Запускать через `-ExecCmds="py <файл>"` (при `-ExecutePythonScript`
#   редактор выходит раньше, чем колбэк успевает отработать).
import os

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/gacha_3d"
LEVEL = "/Temp/L_GachaLight"
MACHINES = 2
WARMUP_TICKS = 420
SETTLE_TICKS = 120
MAX_TICKS = 60000

# (имя кадра, Ambient, LightColor, LightGamma)
VARIANTS = [("light_albedo", 1.0, 1.0, 0.0), ("light_etalon", 0.6, 0.8, 2.2)]


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
    act.set_actor_label("GachaLight")
    act.build_machines(MACHINES)
    act.play_clip_all(1, True)

    st = {"tick": 0, "step": -1, "next_at": WARMUP_TICKS, "ok": 0, "handle": None}

    def apply(step):
        name, amb, lc, gam = VARIANTS[step]
        act.set_editor_property("ambient", amb)
        act.set_editor_property("light_color", unreal.LinearColor(lc, lc, lc, 0.0))
        act.set_editor_property("light_dir_ue", unreal.Vector(0.0, 0.0, -1.0))
        # ★ LightGamma = 0 обнуляет ВЕСЬ множитель (lite^0 = 1) — так получается точный кадр альбедо.
        act.set_editor_property("light_gamma", gam)
        act.build_machines(MACHINES)      # переливает параметры в MID слотов (ApplyLightParams)
        act.play_clip_all(1, True)
        unreal.log("[гача-свет] вариант %s: Ambient=%.2f LightColor=%.2f LightGamma=%.2f" % (name, amb, lc, gam))

    def grab(step):
        name = VARIANTS[step][0]
        for c in act.get_components_by_class(unreal.SceneCaptureComponent2D):
            c.capture_scene()
        # ★ RT ОДИН: альфа лежит в нём же (капчер-маска убран, см. gacha_alpha_composite.md).
        for tgt, suffix in ((act.get_render_target(), "color"),):
            fn = "%s_%s.png" % (name, suffix)
            dst = os.path.join(OUT_DIR, fn)
            if os.path.exists(dst):
                os.remove(dst)
            unreal.RenderingLibrary.export_render_target(world, tgt, OUT_DIR, fn)
            st["ok"] += 1 if os.path.exists(dst) else 0

    def finish():
        if st["handle"] is not None:
            unreal.unregister_slate_post_tick_callback(st["handle"])
            st["handle"] = None
        unreal.log("=== гача-свет: сохранено %d из %d кадров ===" % (st["ok"], 2 * len(VARIANTS)))
        unreal.SystemLibrary.quit_editor()

    def step_tick():
        st["tick"] += 1
        if st["tick"] > MAX_TICKS:
            unreal.log_error("[гача-свет] предел тиков")
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
        try:
            step_tick()
        except Exception:
            import traceback
            unreal.log_error("[гача-свет] ошибка:\n" + traceback.format_exc())
            finish()

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    unreal.log("=== гача-свет: цикл запущен, вариантов %d ===" % len(VARIANTS))


if __name__ == "__main__":
    try:
        main()
    except Exception:
        import traceback
        import unreal
        unreal.log_error("[гача-свет] СБОЙ:\n" + traceback.format_exc())
        unreal.SystemLibrary.quit_editor()
