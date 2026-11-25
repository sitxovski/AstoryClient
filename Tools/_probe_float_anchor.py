# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_float_anchor.py — ЗАМЕР: куда попадал ПРЕЖНИЙ якорь всплывающей надписи над своим
# персонажем (`GetActorBounds(bOnlyColliding=false)`) и куда попадает НОВЫЙ (центр видимой
# геометрии). Игрового кода не трогает — считает ровно те же две формулы средствами Python и
# проецирует обе точки в экран. Нужен, чтобы утверждение «надпись уезжала» стояло на числах,
# а не на рассуждении (закон `false-negatives-method`).
#
# ЗАПУСК: Tools\run_headless_probe_float_anchor.bat
import unreal

LEVEL = "/Game/Map/alpen/alpen"
PAWN_LABEL = "AnchorProbePawn"
PAWN_LOC = (0.0, 0.0, 30000.0)
WARMUP = 300
TRAVEL_SETTLE = 400
SETTLE = 40
MAX_TICKS = 20000


def log(m):
    unreal.log("[якорь] " + str(m))


def main():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
    probe = eas.spawn_actor_from_class(unreal.AsdaPlayerCharacter, unreal.Vector(*PAWN_LOC), unreal.Rotator(0, 0, 0))
    probe.set_actor_label(PAWN_LABEL)
    les.editor_request_begin_play()

    st = {"tick": 0, "step": -1, "next_at": WARMUP, "handle": None, "busy": False}

    def world():
        try:
            return ues.get_game_world()
        except Exception:
            return None

    def cmd(c):
        unreal.SystemLibrary.execute_console_command(world(), c)
        log("→ %s" % c)

    def possess():
        w = world()
        pc = unreal.GameplayStatics.get_player_controller(w, 0)
        pawn = None
        for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.AsdaPlayerCharacter):
            pawn = a
            break
        pawn.set_actor_location(unreal.Vector(*PAWN_LOC), False, False)
        pawn.init_appearance(1, unreal.AsdaWornParts())
        pc.possess(pawn)
        st["pawn"] = pawn
        st["pc"] = pc
        log("павн %s вселён" % pawn.get_name())

    def measure():
        w = world()
        pawn = st["pawn"]
        pc = st["pc"]
        loc = pawn.get_actor_location()
        # ── ПРЕЖНИЙ якорь: AActor::GetActorBounds(bOnlyCollidingComponents = false) ──
        old_o, old_e = pawn.get_actor_bounds(False)
        # ── НОВЫЙ якорь: объединение габаритов ВИДИМЫХ UMeshComponent ──
        mn = [1e30, 1e30, 1e30]
        mx = [-1e30, -1e30, -1e30]
        names = []
        for c in pawn.get_components_by_class(unreal.MeshComponent):
            if not c.is_visible():
                continue
            o = c.get_world_location()   # для отчёта; габариты берём ниже
            b_o, b_e = c.k2_get_component_bounds() if hasattr(c, "k2_get_component_bounds") else (o, unreal.Vector(0, 0, 0))
            names.append(c.get_name())
            for i, (oo, ee) in enumerate(zip([b_o.x, b_o.y, b_o.z], [b_e.x, b_e.y, b_e.z])):
                mn[i] = min(mn[i], oo - ee)
                mx[i] = max(mx[i], oo + ee)
        new_o = unreal.Vector((mn[0] + mx[0]) / 2, (mn[1] + mx[1]) / 2, (mn[2] + mx[2]) / 2)
        log("позиция павна           : (%.0f, %.0f, %.0f)" % (loc.x, loc.y, loc.z))
        log("ПРЕЖНИЙ якорь (все комп): (%.0f, %.0f, %.0f), полугабарит (%.0f, %.0f, %.0f)"
            % (old_o.x, old_o.y, old_o.z, old_e.x, old_e.y, old_e.z))
        log("НОВЫЙ якорь (видимые меши %s): (%.0f, %.0f, %.0f)" % (",".join(names), new_o.x, new_o.y, new_o.z))
        # ★ ГЛАВНОЕ ЧИСЛО ЗАМЕРА: проецируется ли точка вообще. `project_world_to_screen`
        #   возвращает None, если точка за ближней плоскостью — ровно тот случай, когда
        #   `ProjectWorldLocationToScreen` в C++ отдаёт false и спавн надписи выходит молча.
        for tag, p in (("ПРЕЖНИЙ", old_o), ("НОВЫЙ", new_o)):
            sp = unreal.GameplayStatics.project_world_to_screen(pc, p, False)
            if sp is None:
                log("%s якорь → проекция НЕ УДАЛАСЬ (точка за камерой) ⇒ надписи не будет" % tag)
            else:
                log("%s якорь → экран (%.0f, %.0f)" % (tag, sp.x, sp.y))
        cmd("asda.Hud.TestExpGain 1395 1")

    PLAN = [
        (lambda: cmd("open /Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode"), TRAVEL_SETTLE),
        (possess, SETTLE),
        (measure, 10),
        (lambda: cmd("asda.Fx.FloatDump"), SETTLE),
    ]

    def finish(ok=False):
        """Снять тик, остановить PIE, убрать пробу и выйти. ★ С5 (11.10.2026): маркер успеха ANCHOR_PROBE_OK — ТОЛЬКО при
        ok=True (весь план выполнен); исключение и потолок тиков — без маркера (run_headless_safe даст провал)."""
        if ok:
            log("ЗАВЕРШЕНО ANCHOR_PROBE_OK")
        if st["handle"]:
            unreal.unregister_slate_post_tick_callback(st["handle"])
        try:
            les.editor_request_end_play()
        except Exception:
            pass
        try:
            eas.destroy_actor(probe)
        except Exception:
            pass
        unreal.SystemLibrary.quit_editor()

    def on_tick(_dt):
        if st["busy"]:
            return
        st["busy"] = True
        try:
            st["tick"] += 1
            if st["tick"] > MAX_TICKS:
                unreal.log_error("[якорь] потолок тиков %d — план не выполнен" % MAX_TICKS)
                finish()
                return
            if st["tick"] < st["next_at"]:
                return
            st["step"] += 1
            if st["step"] >= len(PLAN):
                finish(ok=True)
                return
            fn, wait = PLAN[st["step"]]
            fn()
            st["next_at"] = st["tick"] + wait
        except Exception:
            import traceback
            unreal.log_error("[якорь] ошибка:\n" + traceback.format_exc())
            finish()
        finally:
            st["busy"] = False

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
