# Copyright (c) 2026 @sitxovski (Discord)
# _wing_particle_probe.py — РАНТАЙМ-ЗАМЕР блёсток крыльев (круг 2): PIE headless, реальный Tick.
# Спавнит BP_Fx_eff_wing_m_ss_00 и BP_Fx_eff_wing_m_big_00, раз в ~0.5с снимает PMC-секции
# частиц (0x17): число живых квадов, длины сторон (=2·SX), центроид относительно актора.
# Доказывает: (а) исполняется ли SpawnParticle (лог [FxSpawn]); (б) размер спавна во времени
# (фикс: постоянный 5/10; баг: деградация к 0 к концу 5с-периода); (в) где частицы относительно актора.
import unreal

def log(m): unreal.log_warning("[wingprobe] %s" % m)

if __name__ in ("__main__", "builtins"):   # main-guard: закон tools-no-import-side-effects
    unreal.EditorLoadingAndSavingUtils.load_map("/Game/Map/Login")
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    log("editor-мир (AsdaEffectActor тикает и в редакторе: ShouldTickIfViewportsOnly)")

    state = {"n": 0, "spawned": False, "actors": [], "t": 0.0, "next": 0.5, "done": False}


    def probe(a, label):
        pmc = a.get_component_by_class(unreal.ProceduralMeshComponent)
        if not pmc:
            log("%s: НЕТ PMC" % label); return
        al = a.get_actor_location()
        ems = a.get_editor_property("emitters")
        for i in range(len(ems)):
            em = ems[i]
            if em.get_editor_property("jeb_type") != 0x17: continue
            try:
                r = unreal.ProceduralMeshLibrary.get_section_from_procedural_mesh(pmc, i)
                V = r[0]
            except Exception as ex:
                log("%s em%d: секции нет (%s)" % (label, i, ex)); continue
            if not V:
                log("%s em%d: 0 вершин" % (label, i)); continue
            # квады по 4 вершины; живые = невырожденные (сторона > 0.01)
            alive = []
            for q in range(len(V) // 4):
                v0, v1, v3 = V[4*q], V[4*q+1], V[4*q+3]
                w = (v1 - v0).length(); h = (v3 - v0).length()
                if w > 0.01:
                    c = (v0 + v1 + V[4*q+2] + v3) * 0.25
                    alive.append((w, h, c))
            if not alive:
                log("%s em%d(%s): verts=%d, живых квадов 0" % (label, i, em.get_editor_property("name"), len(V)))
                continue
            ws = sorted(x[0] for x in alive)
            cx = sum(x[2].x for x in alive)/len(alive) - al.x
            cy = sum(x[2].y for x in alive)/len(alive) - al.y
            cz = sum(x[2].z for x in alive)/len(alive) - al.z
            zmin = min(x[2].z for x in alive) - al.z; zmax = max(x[2].z for x in alive) - al.z
            log("%s em%d(%s): живых=%d размер(min/med/max)=%.2f/%.2f/%.2f центр-офсет=(%.1f,%.1f,%.1f) z=[%.1f..%.1f]" % (
                label, i, em.get_editor_property("name"), len(alive), ws[0], ws[len(ws)//2], ws[-1], cx, cy, cz, zmin, zmax))

    def on_tick(dt):
        state["n"] += 1
        if not state["spawned"]:
            state["spawned"] = True
            for bp in ("BP_Fx_eff_wing_m_ss_00", "BP_Fx_eff_wing_m_big_00"):
                path = "/Game/AsdaAssets/Effects/%s.%s_C" % (bp, bp)
                cls = unreal.load_class(None, path)
                if not cls:
                    log("НЕТ класса %s" % path); continue
                a = eas.spawn_actor_from_class(cls, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
                state["actors"].append((a, bp))
                log("заспавнен %s @ (0,0,0)" % bp)
            return
        state["t"] += dt
        if state["t"] >= state["next"]:
            state["next"] += 0.5
            log("--- t=%.2fс ---" % state["t"])
            for a, bp in state["actors"]:
                probe(a, bp)
        if state["t"] >= 6.0 and not state["done"]:
            state["done"] = True
            unreal.unregister_slate_post_tick_callback(state["handle"])
            log("ГОТОВО")
            unreal.SystemLibrary.quit_editor()

    state["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    log("tick-callback зарегистрирован")
