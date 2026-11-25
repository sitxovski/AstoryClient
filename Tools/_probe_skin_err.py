# Copyright (c) 2026 @sitxovski (Discord)
# _probe_skin_err.py — МЕТРИКА КАЧЕСТВА СКИННИНГА по ГОТОВОМУ .glb.
#   max |вершина_порта(t) − вершина_игры(t)| в модельных единицах, по кадрам клипа.
#
# Эталон (игра, шейдер g_pSkinShaderA @0x15C39B0, row-vector):
#     v_game(t) = w0·([POSITION0|1]·W_row[j0](t)) + w1·([POSITION1|1]·W_row[j1](t))
#   W_row = eng_row_matrix(quat,pos,scale,conv) кадра t клипа .jbb (абсолютные кейфреймы).
# Порт (то, что реально лежит в .glb и попадёт в UE, column-vector, glTF-пространство z-зеркало):
#     v_port(t) = Σ_i w_i · (JointTRS_i(t) · IBM_i) · [P_gltf|1],   затем z → −z (обратно в game-space).
#
# Кадры берутся по ИНДЕКСУ (как движок: kk = min(k, keyCount−1)) — поэтому LINEAR-интерполяция glTF
# на ключе даёт РОВНО ключевое значение, и сравнение честное (без разночтений интерполяции).
#
# Запуск: python _probe_skin_err.py <Vehicle|ClanFlag|Pet|путь_к_папке_glb> [маска_имени]
import sys, os, re, zipfile, importlib.util, json
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = _P.DATA
MODEL_ARC = GAME + r"\Character\Model\CharModel.bnz"
MOTION_ARC = GAME + r"\Character\Motion\CharMotion.baz"


def load_mod(name, fn):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, fn))
    m = importlib.util.module_from_spec(spec); sys.modules[name] = m; spec.loader.exec_module(m); return m


import pygltflib as g

CT = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
NC = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def acc_data(gl, blob, i):
    a = gl.accessors[i]; bv = gl.bufferViews[a.bufferView]
    n = NC[a.type]; dt = CT[a.componentType]
    off = (bv.byteOffset or 0) + (a.byteOffset or 0)
    arr = np.frombuffer(blob, dt, a.count * n, off)
    return arr.reshape(a.count, n) if n > 1 else arr


def node_matrix(t, q, s):
    """glTF-узел T·R·S (column-vector)."""
    x, y, z, w = q
    R = np.array([[1-2*(y*y+z*z), 2*(x*y-w*z), 2*(x*z+w*y)],
                  [2*(x*y+w*z), 1-2*(x*x+z*z), 2*(y*z-w*x)],
                  [2*(x*z-w*y), 2*(y*z+w*x), 1-2*(x*x+y*y)]], float)
    M = np.eye(4); M[:3, :3] = R * np.asarray(s, float)[None, :]; M[:3, 3] = t
    return M


def port_joint_frames(gl, blob, anim_name):
    """[joint_idx][frame] → 4×4 матрица узла-кости в glTF (плоский скелет: world = local)."""
    skin = gl.skins[0]; joints = list(skin.joints)
    jpos = {nd: i for i, nd in enumerate(joints)}
    trs = [{} for _ in joints]                       # path → массив ключей
    anim = None
    for a in gl.animations:
        if a.name == anim_name: anim = a; break
    if anim is None and gl.animations: anim = gl.animations[0]
    if anim is not None:
        for ch in anim.channels:
            jn = jpos.get(ch.target.node)
            if jn is None: continue
            smp = anim.samplers[ch.sampler]
            trs[jn][ch.target.path] = acc_data(gl, blob, smp.output)
    # rest-поза узла — на случай костей без треков
    rest = []
    for nd in joints:
        n = gl.nodes[nd]
        rest.append((np.array(n.translation or [0, 0, 0], float),
                     np.array(n.rotation or [0, 0, 0, 1], float),
                     np.array(n.scale or [1, 1, 1], float)))
    nf = 1
    for d in trs:
        for v in d.values(): nf = max(nf, len(v))
    out = []
    for j in range(len(joints)):
        d = trs[j]; kc = max([len(v) for v in d.values()] or [0])
        frames = []
        for k in range(nf):
            if kc:
                kk = min(k, kc - 1)
                t = d["translation"][kk] if "translation" in d else rest[j][0]
                q = d["rotation"][kk] if "rotation" in d else rest[j][1]
                s = d["scale"][kk] if "scale" in d else rest[j][2]
            else:
                t, q, s = rest[j]
            frames.append(node_matrix(t, q, s))
        out.append(frames)
    return out, [gl.nodes[nd].name for nd in joints], nf


def measure(glb_path, jbm, jbb, anim_name):
    gl = g.GLTF2().load(glb_path); blob = gl.binary_blob()
    skin = gl.skins[0]
    IBMs = acc_data(gl, blob, skin.inverseBindMatrices).reshape(-1, 4, 4)
    IBM = [m.reshape(4, 4).T for m in IBMs]           # glTF column-major → numpy column-vector
    JM, jnames, nf = port_joint_frames(gl, blob, anim_name)
    # карта имени кости → индекс в клипе игры
    nrm = lambda s: re.sub(r"\s+", " ", (s or "")).strip().lower()
    gmap = {}
    for i, b in enumerate(jbb["bones"]): gmap.setdefault(nrm(b["name"]), i)
    j2g = [gmap.get(nrm(n)) for n in jnames]
    # мировые матрицы игры по кадрам
    bones = jbb["bones"]
    frames = list(range(0, nf, max(1, nf // 12)))[:12] or [0]
    Wg = {}                                           # (gbone, frame) → 4×4 row
    prims = gl.meshes[0].primitives
    if len(prims) != len(jbm["meshes"]):
        return None, "примитивов %d ≠ мешей %d" % (len(prims), len(jbm["meshes"]))
    worst = 0.0; worstinfo = ""
    for pi, pr in enumerate(prims):
        me = jbm["meshes"][pi]
        used = np.unique(me["idx"])
        P = acc_data(gl, blob, pr.attributes.POSITION).astype(float)
        J = acc_data(gl, blob, pr.attributes.JOINTS_0).astype(int)
        Wt = acc_data(gl, blob, pr.attributes.WEIGHTS_0).astype(float)
        if len(P) != len(used):
            return None, "верш. в примитиве %d ≠ %d" % (len(P), len(used))
        P0 = me["P0"][used]; P1 = me["P1"][used]; w0 = me["w0"][used].astype(float)
        g0 = me["j0"][used]; g1 = me["j1"][used]
        Ph = np.hstack([P, np.ones((len(P), 1))])
        G0 = np.hstack([P0, np.ones((len(P0), 1))])
        G1 = np.hstack([P1, np.ones((len(P1), 1))])
        for k in frames:
            # порт
            vp = np.zeros((len(P), 3))
            for slot in range(4):
                w = Wt[:, slot]
                if not np.any(w > 0): continue
                for jj in np.unique(J[:, slot][w > 0]):
                    sel = (J[:, slot] == jj) & (w > 0)
                    M = JM[jj][min(k, len(JM[jj]) - 1)] @ IBM[jj]
                    vp[sel] += w[sel, None] * (Ph[sel] @ M.T)[:, :3]
            vp[:, 2] *= -1                              # glTF → game space
            # игра
            vg = np.zeros((len(P), 3))
            for (garr, Gh, ww) in ((g0, G0, w0), (g1, G1, 1.0 - w0)):
                for jj in np.unique(garr):
                    jj = int(jj)
                    key = (jj, k)
                    if key not in Wg:
                        bn = bones[jj] if jj < len(bones) else None
                        if bn is None or not bn["keyCount"]:
                            Wg[key] = np.eye(4)
                        else:
                            kk = min(k, bn["keyCount"] - 1)
                            Wg[key] = S.eng_row_matrix(bn["quat"][kk], bn["pos"][kk], bn["scale"][kk], bn["conv"])
                    sel = (garr == jj)
                    vg[sel] += ww[sel, None] * (Gh[sel] @ Wg[key])[:, :3]
            d = np.linalg.norm(vp - vg, axis=1).max()
            if d > worst: worst = float(d); worstinfo = "меш %s кадр %d" % (me.get("node", pi), k)
    return worst, worstinfo


GROUPS = {"vehicle": ("Vehicle", re.compile(r"^itm1_n_\d+_vehicle_\d+$", re.I), "_%d"),
          "clanflag": ("ClanFlag", re.compile(r"^small_flag$", re.I), "%03d"),
          "pet": ("Pet", re.compile(r"pet\d+", re.I), "_%d"),
          # ★ ХРАНИТЕЛЬ (Soul Guardian): 9 моделей sb_md_{oh,cb,am}_0{1,2,3}. Имена клипов НЕ выводятся
          #   из имени модели (`sb_md_oh_01` → `sb_md_ani_oh_idle_01_00`), поэтому референс-клип и его
          #   имя в .glb берутся из SoulGuarderTable.BIN — тем же правилом, что и при сборке
          #   (asda_build_soulguard_models.py: референс = поле 3 = мотион 0 «покой A»).
          "soulguard": ("SoulGuard", re.compile(r"^sb_md_", re.I), None)}

SG_TABLE = os.path.join(HERE, "..", "RawAssets", "Data", "Character", "SoulGuarderTable.BIN")


def soulguard_refclips():
    """модель(lower) → (база_клипа_мотиона_0, имя_анимации_в_glb). Раскладка — AsdaSoulGuardRows.h."""
    import struct as _st
    b = open(SG_TABLE, "rb").read()
    cnt = _st.unpack_from("<i", b, 4)[0]
    assert b[12:15] == b"SGT", "не SoulGuarderTable.BIN"
    out = {}
    for i in range(cnt):
        r = 20 + 1956 * i
        def s(k):
            raw = b[r + 228 + 64 * k:r + 228 + 64 * k + 64]
            z = raw.find(b"\0")
            return (raw[:z] if z >= 0 else raw).decode("latin1")
        model, clip = s(0).lower(), s(3).lower()      # поле 0 = меш, поле 3 = клип мотиона 0
        if model and clip:
            out.setdefault(model, (clip, clip[12:]))  # 12 = длина «sb_md_ani_<fam>»
    return out


def main():
    grp = (sys.argv[1] if len(sys.argv) > 1 else "vehicle").lower()
    sub, _flt, _ = GROUPS.get(grp, (sys.argv[1], None, "_%d"))
    raw = os.path.abspath(os.path.join(HERE, "..", "RawAssets", "Models", sub))
    mask = sys.argv[2] if len(sys.argv) > 2 else None
    zm = zipfile.ZipFile(MODEL_ARC); za = zipfile.ZipFile(MOTION_ARC)
    jbm_by = {n[:-4]: n for n in zm.namelist() if n.lower().endswith(".jbm")}
    # Клипы хранителя частично лежат в CharModel.bnz (8 файлов семейства oh уровня 01) — ищем в обоих.
    jbb_by = {n[:-4]: (za, n) for n in za.namelist() if n.lower().endswith(".jbb")}
    for n in zm.namelist():
        if n.lower().endswith(".jbb"): jbb_by.setdefault(n[:-4], (zm, n))
    jbb_lower = {k.lower(): v for k, v in jbb_by.items()}
    sg_ref = soulguard_refclips() if grp == "soulguard" else {}
    jbm_lower = {k.lower(): v for k, v in jbm_by.items()}
    glbs = sorted(f for f in os.listdir(raw) if f.lower().endswith(".glb"))
    if mask: glbs = [f for f in glbs if mask in f]
    res = []
    for fn in glbs:
        model = os.path.splitext(fn)[0]
        if model.lower() not in jbm_lower:
            print("  ! нет .jbm для", model); continue
        jbm = T.parse_jbm_ext(zm.read(jbm_lower[model.lower()]))
        if grp == "soulguard":
            ref = sg_ref.get(model.lower())
            if not ref or ref[0] not in jbb_lower:
                print("  ! нет референс-клипа для", model); continue
            base, anim = ref[0], ref[1]
            zsrc, jname = jbb_lower[base]
            jbb = S.parse_jbb(zsrc.read(jname))
            T.resolve_names(jbm, jbb)
            size = max(np.linalg.norm(me["pos0"], axis=1).max() for me in jbm["meshes"])
            try:
                err, info = measure(os.path.join(raw, fn), jbm, jbb, anim)
            except Exception as e:
                print("  FAIL %s: %s" % (model, e)); continue
            if err is None:
                print("  SKIP %s: %s" % (model, info)); continue
            res.append((model, size, err, info))
            print("  %-28s размер=%6.1f  ОТКЛОНЕНИЕ=%8.3f   %s" % (model, size, err, info))
            continue
        # референс-клип = тот же, что использовался при сборке (первый по индексу)
        cands = []
        for base in jbb_by:
            m = re.match(re.escape(model) + r"_?(\d+)$", base)
            if m: cands.append((int(m.group(1)), base))
        if not cands: print("  ! нет клипов для", model); continue
        cands.sort(); N, base = cands[0]
        jbb = S.parse_jbb(jbb_by[base][0].read(jbb_by[base][1]))
        T.resolve_names(jbm, jbb)
        size = max(np.linalg.norm(me["pos0"], axis=1).max() for me in jbm["meshes"])
        try:
            err, info = measure(os.path.join(raw, fn), jbm, jbb, "anim_%02d" % N)
        except Exception as e:
            print("  FAIL %s: %s" % (model, e)); continue
        if err is None:
            print("  SKIP %s: %s" % (model, info)); continue
        res.append((model, size, err, info))
        print("  %-28s размер=%6.1f  ОТКЛОНЕНИЕ=%8.3f   %s" % (model, size, err, info))
    if res:
        bad = [r for r in res if r[2] > 5.0]
        print("=" * 80)
        print("МОДЕЛЕЙ: %d | >5 ед: %d | МАКСИМУМ: %.3f (%s)"
              % (len(res), len(bad), max(r[2] for r in res), max(res, key=lambda r: r[2])[0]))
        json.dump([dict(model=r[0], size=float(r[1]), err=float(r[2])) for r in res],
                  open(os.path.join(HERE, "_probe_skin_err_%s.json" % grp), "w"), indent=1)


def _script_main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global S, T
    S = load_mod("conv", "asda_skinned_to_gltf.py")
    T = load_mod("T", "asda_test_export_one.py")
    main()


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    _script_main()
