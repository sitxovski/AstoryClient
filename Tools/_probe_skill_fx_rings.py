# Copyright (c) 2026 @sitxovski (Discord)
# _probe_skill_fx_rings.py - СВЕРКА ЦЕНТРА КОЛЬЦА 0xE У ЭФФЕКТОВ НАВЫКОВ С ЭТАЛОНОМ (оффлайн).
#
# ЭТАЛОН (ringDraw@0x1215C10, Docs/Port/systems/EFFECTS_CONVERSION_MODEL.md):
#   T = basePos(0x3FC, СЫРОЙ МИР) + (posChan(0x3E4)*R)*(R*N48*N520)
#   т.е. центр = basePos + R^2(posChan), R = RotX*RotY*RotZ (D3DX row-vector).
#
# ПОРТ, канал НАВЫКОВ (asda_build_char_fx.build_one -> build_emitter БЕЗ ring_track_pos,
#   БЕЗ pure_ring_attached), а рантайм при этом идёт ЭТАЛОННОЙ веткой, т.к.
#   AsdaSkillFxSubsystem ставит актору bStrictQuadAttach = true (asda.Skill.StrictFx, дефолт 1):
#     * кольцо БЕЗ трек-узлов: ring_base_pos = basePos + pos (БЕЗ негации z), ring_pos_ofs = 0;
#       рантайм делает PC = (x, y, -z) -> z ПЕРЕВЁРНУТ, basePos прогоняется через R^2;
#     * кольцо С трек-узлами: офсет узла зашит константой [0,0,0] -> центр = НАЧАЛО эффекта.
import os, sys, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_paths as P
import asda_jeb_parse as J

SRC = P.EFFECT_MODEL_ORIG_DIR
RING = (0xE, 4, 0x15, 0x1A)
D = math.pi / 180.0


def dRX(p, a):
    c, s = math.cos(a), math.sin(a)
    return (p[0], p[1] * c - p[2] * s, p[1] * s + p[2] * c)


def dRY(p, a):
    c, s = math.cos(a), math.sin(a)
    return (p[0] * c + p[2] * s, p[1], -p[0] * s + p[2] * c)


def dRZ(p, a):
    c, s = math.cos(a), math.sin(a)
    return (p[0] * c - p[1] * s, p[0] * s + p[1] * c, p[2])


def R2(v, rx, ry, rz):
    v = dRZ(dRY(dRX(v, rx * D), ry * D), rz * D)
    return dRZ(dRY(dRX(v, rx * D), ry * D), rz * D)


def L(v):
    return math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])


def fmtv(v):
    return "(%.0f,%.0f,%.0f)" % (v[0], v[1], v[2])


def report(name):
    fp = os.path.join(SRC, name + ".jeb")
    if not os.path.isfile(fp):
        print("НЕТ ФАЙЛА %s" % fp)
        return
    jb = J.parse_jeb(open(fp, "rb").read())
    print("\n===== %s =====" % name)
    for e in jb["emitters"]:
        t = e["type"]
        p = e["params"]
        if e.get("mesh") or t not in RING or not p.get("ring"):
            continue
        tn = [x for x in (p.get("trackNodes") or []) if len(x) >= 45]
        for k, tag in ((0, "старт"), (1, "конец")):
            pos = p["posStart"] if k == 0 else p["posEnd"]
            base = p["basePosStart"] if k == 0 else p["basePosEnd"]
            rx, ry, rz = p["rotX"][k], p["rotY"][k], p["rotZ"][k]
            et = tuple(base[i] + R2(pos, rx, ry, rz)[i] for i in range(3))
            if tn:
                po = (0.0, 0.0, 0.0)
                why = "трек: офсет обнулён"
            else:
                po = R2((base[0] + pos[0], base[1] + pos[1], -(base[2] + pos[2])), rx, ry, rz)
                why = "без трека: z перевёрнут"
            d = L((et[0] - po[0], et[1] - po[1], et[2] - po[2]))
            if d < 0.5 and k == 1:
                continue
            print("  %-14s 0x%02X %-6s R %.0f/%.0f h %.0f sc %.1f rot(%.0f,%.0f,%.0f) "
                  "pos %s base %s | ЭТАЛОН %s ПОРТ %s | d=%.0f  %s"
                  % (e["name"], t, tag, p["botR"][k], p["topR"][k], p["height"][k], p["scale"][k],
                     rx, ry, rz, fmtv(pos), fmtv(base), fmtv(et), fmtv(po), d, why if d > 0.5 else "ок"))


if __name__ == "__main__":
    for a in (sys.argv[1:] or ["eff_1_char_06_bigmeob03-a_00"]):
        report(a)
