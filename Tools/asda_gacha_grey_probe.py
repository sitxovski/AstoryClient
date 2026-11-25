# Copyright (c) 2026 @sitxovski (Discord)
# asda_gacha_grey_probe.py — НАЙТИ, ЧТО ИМЕННО СЕРОЕ в кадре автоматов гачи.
#
# Жалоба владельца: «серый дефолт-материал под лутбоксами и шаром — под автоматами лежит большая
# ровно-серая плита, и под выпавшим призовым шаром такой же серый квадрат».
#
# Метод (закон [[false-negatives-method]] — доказывать вторым способом): вместо догадок кадр
# РАЗБИРАЕТСЯ ПО СЛОТАМ. Для каждого слота материала снимается кадр, где ВСЕ ОСТАЛЬНЫЕ слоты
# заменены полностью прозрачным материалом (`M_AsdaGachaGlass` c `GlassOpacity = 0`), то есть
# видна ровно геометрия этого слота. Затем то же самое по фазам клипа, чтобы поймать момент с
# выпавшим шаром (клип 3 = выдача, конец; клип 4 = вскрытие).
#
# Эталон, что должно быть в кадре (`md_gachya_a_00.jbm`, 20 подсеток, 3 текстуры):
#   слот `md_Gachya_x_00`      — корпус `gacha`, `Handle`, `L_Gear`, `R_Gear`, `Stand`, 9 шаров Object*
#   слот `md_Gachya_x_00_blend`— купол `Bowl` (рендер-флаг mr+0x004 бит 0x1)
#   слот `ball_ani_0N`         — `ball_up`, `ball_up01`, `ball_under` (призовой шар)
#   слот `gachya_shadow_blend` — `G_shadow` (квад 270×210 ед. под автоматом) и `Ball_shadow`
#                                (квад 55×43 ед. под шаром) — ровно те два, про которые жалоба
#
# ★★★ ГРАБЛЯ: при `-ExecutePythonScript` движок не тикает, пока идёт скрипт ⇒ машина состояний
#   на `register_slate_post_tick_callback`, запуск БЕЗ `-NullRHI`, перед экспортом `capture_scene()`.
#
# Запуск: UnrealEditor-Cmd.exe <проект> -ExecutePythonScript=<этот файл> -unattended -stdout
import os

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/gacha_3d"
LEVEL = "/Temp/L_GachaGrey2"
MACHINES = 2
WARMUP_TICKS = 420
SETTLE_TICKS = 90
MAX_TICKS = 60000
TRANSPARENT_MASTER = "/Game/Materials/M_AsdaGachaGlass"   # Opacity = Tex.A * GlassOpacity ⇒ 0 = невидимо

# (имя кадра, слот клипа, время внутри клипа в секундах; -1 = конец клипа)
PHASES = [("idle", 1, 0.0), ("roll", 2, 1.0), ("disp_end", 3, -1.0), ("open", 4, 1.5)]


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
    act.set_actor_label("GachaGrey")
    act.build_machines(MACHINES)

    comps = list(act.get_components_by_class(unreal.SkeletalMeshComponent))
    for c in comps:
        try:
            c.set_update_animation_in_editor(True)
        except Exception:
            pass

    # исходные материалы слотов — чтобы возвращать после изоляции
    orig = [[c.get_material(i) for i in range(c.get_num_materials())] for c in comps]
    slot_names = []
    for i in range(comps[0].get_num_materials() if comps else 0):
        mi = orig[0][i]
        slot_names.append(mi.get_name() if mi else "ПУСТО")
    unreal.log("[гача-серое] слоты автомата 0: %s" % ", ".join("%d:%s" % (i, n) for i, n in enumerate(slot_names)))

    master = unreal.load_asset(TRANSPARENT_MASTER)
    # ★ MID создаётся ТОЛЬКО библиотекой: у `unreal.MaterialInstanceDynamic` метода `create` нет.
    invisible = unreal.MaterialLibrary.create_dynamic_material_instance(act, master) if master else None
    if invisible:
        invisible.set_scalar_parameter_value("GlassOpacity", 0.0)

    # Путь клипа = ровно тот, по которому его берёт AAsdaGachaSceneActor::ClipPath.
    MODELS = ["md_gachya_%s_00" % ch for ch in "abcde"]
    SKEL_ROOT = "/Game/AsdaAssets/Models/Skeletal/Gachya"

    def clip_asset(mi, slot):
        n = MODELS[min(mi, 4)]
        return unreal.load_asset("%s/%s/SkeletalMeshes/%sanim_%02d" % (SKEL_ROOT, n, n, slot))

    def set_clip(slot, t):
        """Поставить клип и ТОЧНОЕ время внутри него.

        ★ В редакторе анимация сама не тикает (грабля из gacha_3d_impl.md §7.5), поэтому поза
          выставляется ЯВНО: `set_position` + `refresh_bone_transforms`. Клип берём загрузкой
          ассета по пути (у `SkeletalMeshComponent` в Python нет `get_single_node_instance`).
        """
        act.play_clip_all(slot, False)
        for ci, c in enumerate(comps):
            try:
                seq = clip_asset(ci, slot)
                dur = unreal.AnimationLibrary.get_sequence_length(seq) if seq else 0.0
                pos = (dur - 0.001) if t < 0 else min(t, max(dur - 0.001, 0.0))
                c.set_position(pos, False)
                # обновление костей после ручной перемотки — метод есть не во всех сборках
                for m in ("refresh_bone_transforms", "tick_pose", "update_bounds"):
                    if hasattr(c, m):
                        getattr(c, m)()
                        break
                if ci == 0:
                    unreal.log("[гача-серое]   клип %d: длит=%.4f с, поза на %.4f с" % (slot, dur, pos))
            except Exception as e:
                unreal.log_warning("[гача-серое] поза не выставлена: %s" % e)

    def isolate(keep):
        """keep = индекс слота, который оставить видимым; None = вернуть всё."""
        for ci, c in enumerate(comps):
            for i in range(c.get_num_materials()):
                if keep is None or i == keep:
                    c.set_material(i, orig[ci][i])
                elif invisible:
                    c.set_material(i, invisible)

    jobs = []
    for pname, slot, t in PHASES:
        jobs.append(("grey_%s_all" % pname, slot, t, None))
    # изоляция слотов — на фазе с выпавшим шаром (там видны ОБА подозреваемых квада)
    for i in range(comps[0].get_num_materials() if comps else 0):
        jobs.append(("grey_slot%d_%s" % (i, slot_names[i]), 3, -1.0, i))

    st = {"tick": 0, "step": -1, "next_at": WARMUP_TICKS, "ok": 0, "handle": None}

    def apply(step):
        name, slot, t, keep = jobs[step]
        isolate(keep)
        set_clip(slot, t)
        unreal.log("[гача-серое] вариант %s (клип %d, слот-изоляция %s)" % (name, slot, keep))

    def grab(step):
        name = jobs[step][0]
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
        unreal.log("=== гача-серое: сохранено %d из %d кадров ===" % (st["ok"], 2 * len(jobs)))
        unreal.SystemLibrary.quit_editor()

    def step_tick():
        st["tick"] += 1
        if st["tick"] > MAX_TICKS:
            unreal.log_error("[гача-серое] предел тиков")
            finish()
            return
        if st["tick"] < st["next_at"]:
            return
        if st["step"] >= 0:
            grab(st["step"])
        st["step"] += 1
        if st["step"] >= len(jobs):
            finish()
            return
        apply(st["step"])
        st["next_at"] = st["tick"] + SETTLE_TICKS

    def on_tick(_dt):
        try:
            step_tick()
        except Exception:
            import traceback
            unreal.log_error("[гача-серое] ошибка:\n" + traceback.format_exc())
            finish()

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    unreal.log("=== гача-серое: цикл запущен, вариантов %d ===" % len(jobs))


if __name__ == "__main__":
    try:
        main()
    except Exception:
        import traceback
        import unreal
        unreal.log_error("[гача-серое] СБОЙ:\n" + traceback.format_exc())
        unreal.SystemLibrary.quit_editor()
