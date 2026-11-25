# Copyright (c) 2026 @sitxovski (Discord)
# _verify_ring_etalon.py — ПОВЕРШИННАЯ сверка колец 0xE карт с ЭТАЛОННОЙ формулой (дизасм 0x1215C10, 2026-07-31).
#
# Эталон (row-vector D3DX, game-оси):
#   v = (−sinθ·R0, {0|H}, cosθ·R0) · RotY(spin) · S(rScale) · Rx·Ry·Rz · RotY(holder) · [chiral] → swap → Xf
#   центр += posChan(диск rec+0x144; в данных порта = ring_base_pos c НЕГИРОВАННЫМ z)·R·R·RotY(holder)·[chiral] через Xf
#   + swap(ring_pos_ofs)(=эталонный basePos 0x3FC, сырой мир, без scale) + HolderOfs.
# МЕТОД: у каждого репера спавнится двойник с rRoll=0 во всех нодах (roll неизвестен в рантайме),
# все 60 вершин секции сравниваются с python-репликой формулы. Допуск 1 юн.
import sys, math
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

REPERS = ("eff_rainbow_a_00_01", "eff_rainbow_a_00_02", "eff_rainbow_a_00_03",
          "eff_rainbow2_a_00_01", "eff_alpenredlight_a_00_01", "eff_alpenbluelight_a_00_01",
          "eff_alpengreenlight_a_00_01", "eff_alpenyellowlight_a_00_01",
          "eff_noeaplhentower00_a_00_01")
D = math.pi / 180.0


def log(m): unreal.log_warning("[ringEt] %s" % m)


def rotXd(p, a):
    c, s = math.cos(a), math.sin(a)
    return (p[0], p[1]*c - p[2]*s, p[1]*s + p[2]*c)


def rotYd(p, a):
    c, s = math.cos(a), math.sin(a)
    return (p[0]*c + p[2]*s, p[1], -p[0]*s + p[2]*c)


def rotZd(p, a):
    c, s = math.cos(a), math.sin(a)
    return (p[0]*c - p[1]*s, p[0]*s + p[1]*c, p[2])


def chiral(p, n):
    if abs(n[0]) + abs(n[1]) + abs(n[2]) < 1e-6: return p
    d = 2.0 * (p[0]*n[0] + p[1]*n[1] + p[2]*n[2])
    return (p[0] - d*n[0], p[1] - d*n[1], p[2] - d*n[2])


def predict(xf, nd, cn, pofs_holder):
    rx, ry, rz = nd["rot"][0]*D, nd["rot"][1]*D, nd["rot"][2]*D
    # pos-канал: компенсация z-негации парсера (диск +z)
    pc = (nd["bp"][0], nd["bp"][1], -nd["bp"][2])
    p1 = rotZd(rotYd(rotXd(pc, rx), ry), rz)
    p1 = rotZd(rotYd(rotXd(p1, rx), ry), rz)
    p1 = chiral(p1, cn)   # holder=0 у карт
    out = []
    for seg in range(30):
        th = 2.0*math.pi*seg/29.0
        for tb in range(2):
            r0 = nd["top"] if tb else nd["bot"]
            g = (-math.sin(th)*r0, nd["h"] if tb else 0.0, math.cos(th)*r0)
            g = tuple(v*nd["scale"] for v in g)          # roll=0 (обнулён в двойнике)
            g = rotZd(rotYd(rotXd(g, rx), ry), rz)
            g = chiral(g, cn)
            gp = (g[0]+p1[0], g[1]+p1[1], g[2]+p1[2])
            ue = unreal.Vector(-gp[2], gp[0], gp[1])
            w = xf.transform_location(ue)
            po = nd["pofs"]
            out.append((w.x + (-po[2]) + pofs_holder[0], w.y + po[0] + pofs_holder[1], w.z + po[1] + pofs_holder[2]))
    return out


def main():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level("/Game/Map/alpen/alpen")
    total = 0; bad = 0; worst = 0.0
    for a in list(eas.get_all_level_actors()):
        if not isinstance(a, unreal.AsdaEffectActor): continue
        lbl = a.get_actor_label()
        if lbl[len("MapEffect_"):] not in REPERS: continue
        if a.get_editor_property("base_already_ue"): continue
        xf = a.get_actor_transform()
        cnv = a.get_editor_property("chiral_normal"); cn = (cnv.x, cnv.y, cnv.z)
        ems = a.get_editor_property("emitters")
        ring_idx = [i for i in range(len(ems)) if ems[i].get_editor_property("ring")
                    and not ems[i].get_editor_property("strict_ring_base_pos")]
        if not ring_idx: continue
        # двойник с rRoll=0
        for i in ring_idx:
            e = ems[i]; nds = e.get_editor_property("nodes")
            for k in range(len(nds)):
                n = nds[k]; n.set_editor_property("ring_roll", 0.0); nds[k] = n
            e.set_editor_property("nodes", nds); ems[i] = e
        b = eas.spawn_actor_from_class(unreal.AsdaEffectActor, xf.translation)
        b.set_actor_transform(xf, False, False)
        for pn in ("chiral_normal", "holder_yaw_start", "holder_yaw_speed", "holder_pos_vel",
                   "base_already_ue", "effect_emitter_scale"):
            try: b.set_editor_property(pn, a.get_editor_property(pn))
            except Exception: pass
        b.set_editor_property("emitters", ems)
        pmc = b.get_component_by_class(unreal.ProceduralMeshComponent)
        for i in ring_idx:
            r = unreal.ProceduralMeshLibrary.get_section_from_procedural_mesh(pmc, i)
            V = r[0]
            if not V or len(V) != 60:
                log("%s em%d: вершин %s (жду 60) — SKIP/FAIL" % (lbl, i, len(V) if V else 0)); bad += 1; continue
            nd0 = ems[i].get_editor_property("nodes")[0]
            ndd = {"rot": (nd0.get_editor_property("ring_rot").x, nd0.get_editor_property("ring_rot").y, nd0.get_editor_property("ring_rot").z),
                   "bp": (nd0.get_editor_property("ring_base_pos").x, nd0.get_editor_property("ring_base_pos").y, nd0.get_editor_property("ring_base_pos").z),
                   "pofs": (nd0.get_editor_property("ring_pos_ofs").x, nd0.get_editor_property("ring_pos_ofs").y, nd0.get_editor_property("ring_pos_ofs").z),
                   "scale": nd0.get_editor_property("ring_scale"), "h": nd0.get_editor_property("ring_h"),
                   "top": nd0.get_editor_property("ring_top_r"), "bot": nd0.get_editor_property("ring_bot_r")}
            pred = predict(xf, ndd, cn, (0.0, 0.0, 0.0))
            err = max(math.dist((V[k].x, V[k].y, V[k].z), pred[k]) for k in range(60))
            total += 1; worst = max(worst, err)
            ok = err <= 1.0
            if not ok: bad += 1
            c = [sum(V[k].x for k in range(60))/60.0, sum(V[k].y for k in range(60))/60.0, sum(V[k].z for k in range(60))/60.0]
            log("%-42s em%d maxΔверш=%.3f центр=(%.1f,%.1f,%.1f) %s" % (lbl, i, err, c[0], c[1], c[2], "OK" if ok else "FAIL"))
        eas.destroy_actor(b)
    log("=== ИТОГ: колец %d, FAIL %d, макс.повершинная ошибка %.3f юн" % (total, bad, worst))
    log("ВЕРДИКТ: %s" % ("FAIL" if bad or not total else "ОК — карт-путь 0xE = эталон 0x1215C10"))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
