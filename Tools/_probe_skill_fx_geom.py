# Copyright (c) 2026 @sitxovski (Discord)
# _probe_skill_fx_geom.py - ОФФЛАЙН-СВЕРКА ГЕОМЕТРИИ ЭФФЕКТОВ НАВЫКОВ С ЭТАЛОНОМ.
# Считает по .jeb, БЕЗ редактора: состав эмиттеров, размеры (полу-экстенты), pos/basePos-каналы,
# и РАСХОЖДЕНИЕ между эталонным центром квада (M._41-43 = basePos + pos*RotX*RotY*RotZ*N48)
# и историческим ("pos в углах, транспонированный знак") - то, что чинит bStrictQuadPivot.
# Запуск: python Tools/_probe_skill_fx_geom.py <jeb-без-расширения> ...
import os, sys, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_paths as P
import asda_jeb_parse as J

SRC = P.EFFECT_MODEL_ORIG_DIR
QUAD_KF = (1, 2, 0xB, 0xC, 0x13, 0x14, 0x18, 0x19)      # ветка growQuad
QUAD_LEGACY = (1, 0xB, 0xC, 0x18)
RING = (0xE, 4, 0x15, 0x1A)
PART = (0xD, 3, 0x1B, 0x17, 0x20)
MESH = (0x11, 0x12, 0x1F)

D = math.pi / 180.0


# исторические лямбды порта (AsdaEffectActor.cpp:1829-1831) - ТРАНСПОНИРОВАННЫЕ
def hRX(p, a):
    c, s = math.cos(a), math.sin(a)
    return (p[0], p[1] * c + p[2] * s, -p[1] * s + p[2] * c)


def hRY(p, a):
    c, s = math.cos(a), math.sin(a)
    return (p[0] * c - p[2] * s, p[1], p[0] * s + p[2] * c)


def hRZ(p, a):
    c, s = math.cos(a), math.sin(a)
    return (p[0] * c + p[1] * s, -p[0] * s + p[1] * c, p[2])


# эталонные D3DX row-vector (0xFF4ADD/0xFF4BDA/0xFF4CED)
def dRX(p, a):
    c, s = math.cos(a), math.sin(a)
    return (p[0], p[1] * c - p[2] * s, p[1] * s + p[2] * c)


def dRY(p, a):
    c, s = math.cos(a), math.sin(a)
    return (p[0] * c + p[2] * s, p[1], -p[0] * s + p[2] * c)


def dRZ(p, a):
    c, s = math.cos(a), math.sin(a)
    return (p[0] * c - p[1] * s, p[0] * s + p[1] * c, p[2])


def L(v):
    return math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def fmtv(v):
    if v is None:
        return "-"
    return "(%.1f,%.1f,%.1f)" % (v[0], v[1], v[2])


def quad_nodes(p):
    """Узлы (t, sx, sy, roll, (rx,ry,rz), basePos, pos) - как их кладёт печка."""
    life = min(max((p.get("life") or [1.0])[0], 0.1), 10.0)
    tn = p.get("trackNodes") or []
    sX = p.get("scaleX", [200, 200, 0])
    sY = p.get("scaleY", sX)
    r1 = p.get("rotY1", [0, 0, 0])
    rX = p.get("rotX", [0, 0, 0])
    rY = p.get("rotY", [0, 0, 0])
    rZ = p.get("rotZ", [0, 0, 0])
    out = [(0.0, sX[0], sY[0], r1[0], (rX[0], rY[0], rZ[0]),
            tuple(p.get("basePosStart", [0, 0, 0])), tuple(p.get("posStart", [0, 0, 0])))]
    if tn:
        for n in tn:
            out.append((min(n[0], life), n[15], n[17], n[28], (n[30], n[32], n[34]),
                        (n[1], n[3], n[5]), (n[7], n[9], n[11])))
    out.append((life, sX[1], sY[1], r1[1], (rX[1], rY[1], rZ[1]),
                tuple(p.get("basePosEnd", [0, 0, 0])), tuple(p.get("posEnd", [0, 0, 0]))))
    return life, out


def centers(nd):
    """(эталонный центр, исторический центр) в game-осях, holder=0."""
    _t, _sx, _sy, roll, rot, _bp, pos = nd
    ax, ay, az = rot[0] * D, rot[1] * D, rot[2] * D
    et = dRZ(dRY(dRX(pos, ax), ay), az)                     # эталон: без roll, D3DX-знак
    hs = hRZ(hRY(hRX(hRY(pos, roll * D), ax), ay), az)      # историч.: pos в углах -> roll + транспон.
    return et, hs


def emit_report(name, verbose=True, out=None):
    fp = os.path.join(SRC, name + ".jeb")
    if not os.path.isfile(fp):
        print("НЕТ ФАЙЛА: %s" % fp)
        return []
    jb = J.parse_jeb(open(fp, "rb").read())
    rows = []
    print("\n===== %s  (эмиттеров %d, blendDst=%s add=%s) =====" %
          (name, len(jb["emitters"]), jb.get("blendDst"), jb.get("blendAdditive")))
    for e in jb["emitters"]:
        t = e["type"]
        p = e["params"]
        nm = e["name"]
        if e.get("mesh") or t in MESH:
            print("  [меш 0x%02X] %-18s (jem)" % (t, nm))
            continue
        if t in PART:
            sx = p["sizeX"]
            sy = p["sizeY"]
            sp = p["speed"]
            print("  [част 0x%02X] %-18s размер X %.1f>%.1f (+-%.1f) Y %.1f>%.1f | скор %.0f>%.0f | "
                  "жизнь %.2f | rate %.1f>%.1f | период %.2f | posBase %s"
                  % (t, nm, sx[0], sx[1], sx[2], sy[0], sy[1], sp[0], sp[1],
                     p["partLife"][0], p["spawnRate"][0], p["spawnRate"][1], p.get("period", 0),
                     fmtv(p.get("posBase"))))
            continue
        if t in RING and p.get("ring"):
            r = p
            print("  [кольц 0x%02X] %-18s R низ %.1f>%.1f верх %.1f>%.1f h %.1f>%.1f scale %.2f>%.2f | "
                  "pos %s>%s | basePos %s>%s"
                  % (t, nm, r["botR"][0], r["botR"][1], r["topR"][0], r["topR"][1],
                     r["height"][0], r["height"][1], r["scale"][0], r["scale"][1],
                     fmtv(r.get("posStart")), fmtv(r.get("posEnd")),
                     fmtv(r.get("basePosStart")), fmtv(r.get("basePosEnd"))))
            continue
        if t in QUAD_KF and p.get("growQuad"):
            life, nds = quad_nodes(p)
            mx = 0.0
            worst = None
            maxhalf = 0.0
            for nd in nds:
                et, hs = centers(nd)
                d = L(sub(et, hs))
                maxhalf = max(maxhalf, abs(nd[1]), abs(nd[2]))
                if d > mx:
                    mx = d
                    worst = (nd, et, hs)
            pmax = max(L(nd[6]) for nd in nds)
            bmax = max(L(nd[5]) for nd in nds)
            print("  [квад 0x%02X] %-18s узлов %d life %.2f bb=%d | полуэкстент max %.1f (габарит %.0f) | "
                  "|pos| max %.1f |basePos| max %.1f | dЦЕНТРА эталон/историч %.1f"
                  % (t, nm, len(nds), life, int(p.get("billboardMode", 0) or 0),
                     maxhalf, maxhalf * 2, pmax, bmax, mx))
            if verbose and worst and mx > 1.0:
                nd, et, hs = worst
                print("        худший узел t=%.2f roll=%.0f rot=(%.0f,%.0f,%.0f) pos=%s -> эталон %s / историч %s"
                      % (nd[0], nd[3], nd[4][0], nd[4][1], nd[4][2], fmtv(nd[6]), fmtv(et), fmtv(hs)))
            rows.append((name, nm, t, maxhalf, pmax, mx))
            continue
        if t in QUAD_LEGACY:
            print("  [квадL 0x%02X] %-18s LEGACY-ветка (без pos/scale) sizeXcur=%.1f sizeBase=%.1f"
                  % (t, nm, p.get("sizeXcur", 0.0) or 0.0, p.get("sizeBase", 0.0) or 0.0))
            continue
        print("  [ПРОЧ 0x%02X] %-18s" % (t, nm))
    return rows


def scan(names, tag):
    """Охват: сколько квадов/колец с ненулевым pos-каналом и величина расхождения."""
    tot_f = 0
    tot_q = 0
    q_pos = 0
    tot_r = 0
    r_pos = 0
    buckets = {}
    worst = []
    for n in names:
        fp = os.path.join(SRC, n + ".jeb")
        if not os.path.isfile(fp):
            continue
        try:
            jb = J.parse_jeb(open(fp, "rb").read())
        except Exception:
            continue
        tot_f += 1
        fmax = 0.0
        for e in jb["emitters"]:
            t = e["type"]
            p = e["params"]
            if e.get("mesh"):
                continue
            if t in QUAD_KF and p.get("growQuad"):
                tot_q += 1
                _life, nds = quad_nodes(p)
                pm = max(L(nd[6]) for nd in nds)
                dm = max(L(sub(*centers(nd))) for nd in nds)
                if pm > 1e-3:
                    q_pos += 1
                fmax = max(fmax, dm)
            elif t in RING and p.get("ring"):
                tot_r += 1
                ps = p.get("posStart") or [0, 0, 0]
                pe = p.get("posEnd") or [0, 0, 0]
                if max(L(ps), L(pe)) > 1e-3:
                    r_pos += 1
        b = ("0" if fmax < 1 else "1-10" if fmax < 10 else "10-50" if fmax < 50 else
             "50-200" if fmax < 200 else "200+")
        buckets[b] = buckets.get(b, 0) + 1
        worst.append((fmax, n))
    worst.sort(reverse=True)
    print("\n### %s: файлов %d | квадов %d (с pos!=0: %d) | колец %d (pos!=0: %d)"
          % (tag, tot_f, tot_q, q_pos, tot_r, r_pos))
    print("    расхождение центра квада (макс по файлу): " +
          ", ".join("%s: %d" % (k, buckets[k])
                    for k in ["0", "1-10", "10-50", "50-200", "200+"] if k in buckets))
    print("    худшие: " + ", ".join("%s %.0f" % (n, d) for d, n in worst[:10]))
    return dict(files=tot_f, quads=tot_q, quads_pos=q_pos, rings=tot_r, rings_pos=r_pos,
                buckets=buckets, worst=worst[:20])


if __name__ == "__main__":
    args = list(sys.argv[1:])
    if not args:
        args = ["eff_1_char_06_fballg01-a_00"]
    for a in args:
        emit_report(a)
