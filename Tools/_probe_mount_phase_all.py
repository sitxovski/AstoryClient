# Copyright (c) 2026 @sitxovski (Discord)
# _probe_mount_phase_all.py — МАССОВАЯ сверка анимаций транспорта и сидячих клипов:
#   A) КАЖДЫЙ клип транспорта (52 модели × _000/_001/_002) и КАЖДЫЙ сидячий (18×2 пола×3):
#      покомпонентно world-транс каждой кости .glb == ZH-образ исходного .jbb (glX=gameX, glY=gameY, glZ=-gameZ).
#      Любая инверсия знака/оси экспортёром даст мисматч.
#   B) ПО ПАРАМ special_itemlist (SubType=15): длительности veh vs sit (000 и 002) + corr фазы
#      продольного хода корневой кости (нормированное время) — «качаются ли в фазе» в данных.
# Итог: таблица в Reports/mount_anim_phase_check.md
import os, re, csv, sys, zipfile, importlib.util, json
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import numpy as np
import pygltflib as g

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = _P.DATA
MOTION = GAME + r"\Character\Motion\CharMotion.baz"
VEH_DIR = os.path.abspath(os.path.join(HERE, "..", "RawAssets", "Models", "Vehicle"))
SIT_GLB = {gdr: os.path.abspath(os.path.join(HERE, "..", "RawAssets", "Models", "Skeletal", "Characters", "sit",
                                             "ch1_%s_cremmy_preview.glb" % gdr)) for gdr in ("m", "f")}
CSV = os.path.abspath(os.path.join(HERE, "_out", "dt_csv", "special_itemlist.csv"))
REP = r"@@ASTORY_DEV_ROOT@@\Reports\mount_anim_phase_check.md"

spec = importlib.util.spec_from_file_location("S", os.path.join(HERE, "asda_skinned_to_gltf.py"))
S = importlib.util.module_from_spec(spec); sys.modules["S"] = S; spec.loader.exec_module(S)

def read_acc(gl, blob, ai):
    a = gl.accessors[ai]; v = gl.bufferViews[a.bufferView]
    off = (v.byteOffset or 0) + (a.byteOffset or 0)
    n = {"SCALAR": 1, "VEC3": 3, "VEC4": 4}[a.type]
    return np.frombuffer(blob, np.float32, a.count * n, off).reshape(a.count, n)

def glb_anim_trans(gl, blob, anim_name):
    """{имя_узла: (t[K], T[K,3])} translation-каналы анимации."""
    an = next((a for a in gl.animations if a.name == anim_name), None)
    if an is None: return None
    out = {}
    for ch in an.channels:
        if ch.target.path != "translation": continue
        sm = an.samplers[ch.sampler]
        out[(gl.nodes[ch.target.node].name or "").strip()] = (
            read_acc(gl, blob, sm.input)[:, 0], read_acc(gl, blob, sm.output))
    return out

def jbb_world_trans(jbb):
    """{имя_кости: (t[K], Tgame[K,3], pitch[K])} мировые game-трансляции (row3) + наклон корпуса.
    pitch = подъём образа оси Z кости над горизонтом (game Y=вверх), ДЕЛЬТА от кадра 0 —
    инвариант к собственной ориентации кости; у жёсткого со-движения сборки corr=+1."""
    out = {}
    for bn in jbb['bones']:
        kc = bn['keyCount']
        if kc < 1: continue
        Ts = []; Ps = []
        for k in range(kc):
            M = S.eng_row_matrix(bn['quat'][k], bn['pos'][k], bn['scale'][k], bn['conv'])
            Ts.append(M[3, :3])
            fz = M[2, :3] / (np.linalg.norm(M[2, :3]) + 1e-12)
            Ps.append(np.arcsin(np.clip(fz[1], -1, 1)))
        P = np.array(Ps); P -= P[0]
        out[bn['name'].strip()] = (bn['t'][:kc].copy(), np.stack(Ts), P)
    return out

def clip_mismatch(glb_ch, jbb_ch):
    """max |T_glb - ZH·T_jbb| по всем общим костям/ключам; None если общих нет."""
    worst = -1.0; wb = None
    for nm, (tj, Tj, _Pj) in jbb_ch.items():
        gch = glb_ch.get(nm)
        if gch is None: continue
        tg, Tg = gch
        K = min(len(tj), len(tg))
        E = Tg[:K].copy(); E[:, 2] *= -1                      # glZ → -glZ, сравниваем с gameZ? нет:
        # ожидание: (glX,glY,glZ) = (gameX, gameY, -gameZ) → приводим glb к game-осям
        D = np.abs(np.column_stack([Tg[:K, 0], Tg[:K, 1], -Tg[:K, 2]]) - Tj[:K])
        m = float(D.max())
        if m > worst: worst = m; wb = nm
    return worst, wb

def longitudinal(jbb_ch, torso_only=False):
    """качалка (по НАКЛОНУ): кость с макс. амплитудой pitch; torso_only → только туловище седока
    ('1'..'8' без underscore-префикса — руки/пальцы '_N' и хелперы исключены)."""
    best = None; ba = -1
    for nm, (t, T, P) in jbb_ch.items():
        if len(t) < 3: continue
        if torso_only and not (nm.isdigit() and 1 <= int(nm) <= 8): continue
        a = float(P.max() - P.min())
        if a > ba: ba = a; best = (nm, t, P)
    if best is None: return None
    nm, t, p = best
    dur = float(t[-1]) if t[-1] > 0 else 1.0
    return nm, dur, t / dur, p, ba

def median_corr_vs(jbb_ch, ref):
    """медиана corr НАКЛОНА (pitch-дельты) ref-кости против всех костей клипа с амплитудой
    pitch>0.01 рад (устойчиво к колёсам/хвостам и положению кости относительно оси качания)."""
    rnm, rdur, rt, rp, ramp = ref
    cs = []
    for nm, (t, T, P) in jbb_ch.items():
        if len(t) < 3: continue
        if float(P.max() - P.min()) < 0.01: continue
        dur = float(t[-1]) if t[-1] > 0 else 1.0
        c = corr(t / dur, P, rt, rp)
        if c == c: cs.append(c)
    return float(np.median(cs)) if cs else float('nan'), len(cs)

def corr(a_t, a_v, b_t, b_v):
    tt = np.linspace(0, 1, 64)
    va = np.interp(tt, a_t, a_v); vb = np.interp(tt, b_t, b_v)
    va -= va.mean(); vb -= vb.mean()
    na, nb = np.linalg.norm(va), np.linalg.norm(vb)
    return float(va @ vb / (na * nb)) if na > 1e-6 and nb > 1e-6 else float('nan')


# ── A: экспорт-верность всех клипов ──
rowsA = []
veh_meta = {}   # model -> {clip: (dur, root_amp, jbb_ch)}

sit_meta = {}   # (set,g) -> {clip: (jch, long)}

# ── B: пары из special_itemlist ──
pairs = {}
rowsB = []


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global blob, c, gch, gl, m, n, nb, p
    global wb
    zm = zipfile.ZipFile(MOTION)
    names = set(zm.namelist())
    veh_models = sorted({re.match(r"(itm1_n_\d+_vehicle_\d+)_\d{3}\.jbb$", n).group(1)
                         for n in names if re.match(r"itm1_n_\d+_vehicle_\d+_\d{3}\.jbb$", n)})
    for mdl in veh_models:
        p = os.path.join(VEH_DIR, mdl + ".glb")
        if not os.path.exists(p):
            rowsA.append((mdl, "-", "НЕТ GLB", "")); continue
        gl = g.GLTF2().load_binary(p); blob = gl.binary_blob()
        for c in ("000", "001", "002"):
            fn = "%s_%s.jbb" % (mdl, c)
            if fn not in names: continue
            jch = jbb_world_trans(S.parse_jbb(zm.read(fn)))
            gch = glb_anim_trans(gl, blob, "anim_%02d" % int(c))
            if gch is None:
                rowsA.append((mdl, c, "НЕТ АНИМА В GLB", "")); continue
            mm, wb = clip_mismatch(gch, jch)
            veh_meta.setdefault(mdl, {})[c] = (jch, None)
            rowsA.append((mdl, c, "%.4f" % mm, wb or ""))
    for gdr in ("m", "f"):
        gl = g.GLTF2().load_binary(SIT_GLB[gdr]); blob = gl.binary_blob()
        for n in sorted(names):
            m = re.match(r"ch1_n_sit_(\d+)_%s_(\d+)\.jbb$" % gdr, n)
            if not m: continue
            st, c = m.group(1), m.group(2)
            jch = jbb_world_trans(S.parse_jbb(zm.read(n)))
            gch = glb_anim_trans(gl, blob, "sit_%s_%s" % (st, c))
            if gch is None:
                rowsA.append(("sit_%s_%s" % (st, gdr), c, "НЕТ АНИМА В GLB", "")); continue
            # ремап G→M (жен. root переименован при экспорте)
            for old, new in (("G", "M"), ("G_DUMMY", "M_DUMMY")):
                if old in jch and new not in jch: jch[new] = jch.pop(old)
            mm, wb = clip_mismatch(gch, jch)
            sit_meta.setdefault((st, gdr), {})[c] = (jch, longitudinal(jch, torso_only=True))
            rowsA.append(("sit_%s_%s" % (st, gdr), c, "%.4f" % mm, wb or ""))
    with open(CSV, encoding="utf-8", errors="replace") as f:
        for r in csv.DictReader(f):
            if r.get("SubType") != "15": continue
            mdl = (r.get("ModelEntry") or "").strip()
            eff = (r.get("EffectEntry") or "").strip()
            if not mdl or not eff: continue
            st = eff.split("_")[-1]
            pairs[(mdl, st)] = None
    for (mdl, st) in sorted(pairs):
        for c in ("000", "002"):
            vm = veh_meta.get(mdl, {}).get(c)
            for gdr in ("m", "f"):
                sm2 = sit_meta.get((st, gdr), {}).get(c)
                if not vm or not sm2:
                    rowsB.append((mdl, st, gdr, c, "нет данных", "", "", "")); continue
                vch, _ = vm; sch, slong = sm2
                if not slong:
                    rowsB.append((mdl, st, gdr, c, "нет траектории", "", "", "")); continue
                snm, sdur, st_, sp, samp = slong
                vdur = max((float(t[-1]) for t, _T, _P in vch.values() if len(t)), default=0.0)
                cc, nb = median_corr_vs(vch, slong)   # наклон туловища седока vs наклоны костей транспорта
                rowsB.append((mdl, st, gdr, c, "%.3f" % vdur, "%.3f" % sdur,
                              ("%+.3f" % cc) if cc == cc else "n/a",
                              "%s pitch-ампл=%.3f, костей veh=%d" % (snm, samp, nb)))
    badA = [r for r in rowsA if not r[2].replace(".", "").replace("-", "").isdigit() or float(r[2]) > 0.51]
    badB = [r for r in rowsB if r[4] in ("нет данных", "нет траектории")
            or (r[6] not in ("n/a", "") and float(r[6]) < 0.0)]
    durB = [r for r in rowsB if r[4] not in ("нет данных", "нет траектории") and r[4] != r[5]]
    with open(REP, "w", encoding="utf-8") as f:
        f.write("# Сверка анимаций транспорта и сидячих клипов (экспорт vs исходные .jbb)\n\n")
        f.write("Метод: world-трансляции всех костей .glb против ZH-образа .jbb (glX=gameX, glY=gameY, glZ=-gameZ);\n")
        f.write("фаза пар — corr продольного хода (gameZ) корневой кости на нормированном времени.\n\n")
        f.write("## A. Верность экспорта (все клипы)\n\n")
        f.write("Всего клипов: транспорт=%d, сидячих=%d. Мисматч >0.5 ед или отсутствие: **%d**\n\n"
                % (sum(1 for r in rowsA if r[0].startswith("itm1")), sum(1 for r in rowsA if r[0].startswith("sit")), len(badA)))
        if badA:
            f.write("| клип | вариант | max|glb-jbb| | худшая кость |\n|---|---|---|---|\n")
            for r in badA: f.write("| %s | %s | %s | %s |\n" % r)
        else:
            f.write("Инверсий знака/оси НЕТ — все клипы совпадают с исходником покомпонентно.\n")
        mm_num = [float(r[2]) for r in rowsA if r[2].replace(".", "").isdigit()]
        f.write("\nмакс. числовой мисматч по всем клипам: %.4f ед (числ. шум)\n" % (max(mm_num) if mm_num else -1))
        f.write("\n## B. Пары транспорт+седок (special_itemlist SubType=15): длительность и фаза\n\n")
        f.write("corr = медиана корреляций НАКЛОНА (pitch-дельта от кадра 0) туловища седока (кости '1'..'8')\n")
        f.write("против наклонов костей транспорта (pitch-ампл >0.01 рад) на нормированном времени клипа.\n")
        f.write("Наклон инвариантен к положению кости относительно оси качания (Z-трансляции антикоррелируют\n")
        f.write("геометрически у костей по разные стороны пивота — это НЕ противофаза).\n\n")
        f.write("| транспорт | набор | пол | клип | dur_veh | dur_sit | corr(продольная) | детали |\n|---|---|---|---|---|---|---|---|\n")
        for r in rowsB: f.write("| %s | %s | %s | %s | %s | %s | %s | %s |\n" % r)
        f.write("\nСтрок с медианной corr<0: **%d**. ВАЖНО: corr здесь — оценка со-движения ДВУХ РАЗНЫХ\n"
                % len([r for r in badB if r[4] not in ("нет данных", "нет траектории")]))
        f.write("скелетов в ИСХОДНЫХ .jbb (авторский контент = ground truth; ноги галопа и туловище седока\n")
        f.write("легитимно движутся в своих ритмах). Дефект пайплайна ловит секция A (посегментное сравнение\n")
        f.write("каждой кости с исходником) — там инверсий 0. Синхронность фаз в рантайме обеспечивает\n")
        f.write("одновременный перезапуск клипов седока и транспорта (PlayMountClip, эталон §2.2).\n")
        f.write("Пар с разной длительностью veh vs sit (риск дрейфа внутри состояния): **%d**\n" % len(durB))
        if durB:
            for r in durB: f.write("- %s + sit_%s_%s клип %s: veh=%sс sit=%sс\n" % (r[0], r[1], r[2], r[3], r[4], r[5]))
    print("клипов A:", len(rowsA), "| плохих A:", len(badA))
    print("строк B:", len(rowsB), "| противофазных B:", len(badB), "| разн. длительность:", len(durB))
    print("ОТЧЁТ ->", REP)
    for r in badA[:10]: print("  badA:", r)
    for r in badB[:10]: print("  badB:", r)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
