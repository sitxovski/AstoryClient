# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_glb_normals.py — ОФЛАЙН-перегон NORMAL во ВСЕХ готовых .glb RawAssets/Models (2026-09-05).
#
# Зачем: `asda_jbm.parse_geometry` до 2026-09-05 отдавал как `nrm` бон-локальный pNormalArray, и все
#   экспортёры (asda_jbm_to_gltf.to_glb, asda_test_export_one.build_fixed, asda_skinned_to_gltf.build)
#   клали его в glb рядом с МОДЕЛЬНОЙ позицией → в UE нормаль повёрнута bind-ротацией кости
#   (review_setD.md §2, памятка char-vertex-normals-inverted). Исправлен parse_geometry (nrm = модельная
#   float 3..5 вершины); здесь — перенос исправления на 3643 уже собранных .glb.
#
# Метод (вместо повторного прогона 9 разных пайплайнов сборки, которые с момента сборки могли
#   разъехаться по именам материалов/клипам): для каждого примитива glb
#   1) POSITION-байты хэшируются и ищутся среди ВСЕХ мешей всех архивов (CharModel/NpcModel/MobModel ×
#      ORIG/MINE + Item/Model) — тождество экспортной трансформации `pos[unique(idx)]; z*=-1; nan→0`
#      доказывает соответствие «примитив ↔ меш .jbm» побайтно, без опоры на имена;
#   2) СТАРЫЙ NORMAL примитива сверяется с тремя вариантами экспортёров от pNormalArray
#      (raw — старый asda_skinned_to_gltf.build; sanz — to_glb: nan→0 до зеркала z; san/sanfix — build_fixed: nan→0 после, |n|<1e-6→(0,0,1)):
#      совпадение выбирает пайплайн и подтверждает, что старая нормаль была именно pNormalArray;
#   3) НОВЫЙ NORMAL = тот же вариант от `nrm_model` (float 3..5, невалидные → геометрическая);
#   4) в файл записываются ТОЛЬКО байты аксессора NORMAL (GLB разбирается вручную: JSON-чанк и все
#      остальные байты BIN не трогаются) → POSITION/JOINTS/WEIGHTS/IBM/индексы/анимации побайтно прежние.
#   Файл без совпадений или с неоднозначностью НЕ трогается и попадает в отчёт.
#
# Бэкап: старый .glb копируется зеркалом путей в RawAssets/Models/_prev_normals_2026-09-05/<кат>/… (размер
#   проверяется > 0). Папка исключена из обхода asda_import_models.py (SKIP_DIRS).
#
# Запуск (вне UE):
#   python asda_fix_glb_normals.py --dry [--limit N] [--only Skeletal/NPC]   # только проверка/статистика
#   python asda_fix_glb_normals.py --apply [--jobs 8]                        # перезапись NORMAL + бэкап
#   python asda_fix_glb_normals.py --verify 50                               # сверка бэкап↔новый: разница только в NORMAL
#   python asda_fix_glb_normals.py --stats                                   # per-file статистика в JSON
# Отчёт JSON: Reports/render_sets_2026-09-05/normals_export_stats.json
#
# ★ Модуль БЕЗ побочных эффектов при импорте (tools-no-import-side-effects).
import os, sys, json, struct, zipfile, hashlib, argparse, pickle, random, collections, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import asda_jbm as JBM
import asda_paths as _P

HERE = os.path.dirname(os.path.abspath(__file__))
RAW_MODELS = os.path.abspath(os.path.join(HERE, "..", "RawAssets", "Models"))
PREV_DIR = os.path.join(RAW_MODELS, "_prev_normals_2026-09-05")
REPORT_DIR = os.path.abspath(os.path.join(HERE, "..", "..", "Reports", "render_sets_2026-09-05"))
STATS_JSON = os.path.join(REPORT_DIR, "normals_export_stats.json")
import tempfile
INDEX_PKL = os.path.join(tempfile.gettempdir(), "asda_normals_mesh_index.pkl")   # кэш индекса мешей (регенерируется)


# Источники .jbm: те же, что у сборщиков (asda_build_models.py = ORIG; скелеталки = MINE)
def sources():
    orig, mine = _P.DATA_ORIG, _P.DATA
    return [("ORIG/CharModel", "zip", os.path.join(orig, "Character", "Model", "CharModel.bnz")),
            ("ORIG/MobModel",  "zip", os.path.join(orig, "Mob", "Model", "MobModel.bnz")),
            ("ORIG/NpcModel",  "zip", os.path.join(orig, "NPC", "Model", "NpcModel.bnz")),
            ("ORIG/Item",      "dir", os.path.join(orig, "Item", "Model")),
            ("MINE/CharModel", "zip", os.path.join(mine, "Character", "Model", "CharModel.bnz")),
            ("MINE/MobModel",  "zip", os.path.join(mine, "Mob", "Model", "MobModel.bnz")),
            ("MINE/NpcModel",  "zip", os.path.join(mine, "NPC", "Model", "NpcModel.bnz")),
            ("MINE/Item",      "dir", os.path.join(mine, "Item", "Model"))]


def iter_jbm(kind, path):
    if kind == "zip":
        if not os.path.isfile(path):
            return
        z = zipfile.ZipFile(path)
        for n in z.namelist():
            if n.lower().endswith(".jbm"):
                yield n, z.read(n)
    else:
        if not os.path.isdir(path):
            return
        for f in sorted(os.listdir(path)):
            if f.lower().endswith(".jbm"):
                yield f, open(os.path.join(path, f), "rb").read()


def exp_pos_bytes(pos, used):
    """POSITION ровно как у экспортёров: pos[unique(idx)], z*=-1 (LH→RH), nan→0, <f4."""
    P = pos[used].copy(); P[:, 2] *= -1
    P = np.nan_to_num(P, nan=0.0, posinf=0.0, neginf=0.0)
    return P.astype("<f4").tobytes()


def nrm_variants(nrm, used):
    """Три варианта экспортной нормали (см. шапку): raw / san / sanfix. dict имя→bytes."""
    N = nrm[used].copy(); N[:, 2] *= -1
    raw = N.astype("<f4").tobytes()
    S = np.nan_to_num(N, nan=0.0, posinf=0.0, neginf=0.0).astype(np.float32)
    san = S.astype("<f4").tobytes()
    F = S.copy(); ln = np.linalg.norm(F, axis=1); F[ln < 1e-6] = np.array([0.0, 0.0, 1.0], np.float32)
    # sanz — to_glb (asda_jbm_to_gltf): san ДО зеркала z → у NaN-вершин z = −0.0 (байтово ≠ +0.0)
    Z = np.nan_to_num(nrm[used].copy(), nan=0.0, posinf=0.0, neginf=0.0).astype(np.float32); Z[:, 2] *= -1
    return {"raw": raw, "san": san, "sanfix": F.astype("<f4").tobytes(), "sanz": Z.astype("<f4").tobytes()}


def build_index(verbose=True):
    """POSITION-хэш → кандидаты (src, jbm, mesh, node, mtype, nrm_bone[used], nrm_model[used], nfb)."""
    idx = collections.defaultdict(list); idx2 = collections.defaultdict(list); nfiles = nmesh = 0
    for src, kind, path in sources():
        for name, b in iter_jbm(kind, path):
            try:
                m = JBM.parse_geometry(b, guard=False)
            except Exception:
                try:
                    m = JBM.parse_geometry(b, guard=True)
                except Exception:
                    m = None
            if not m:
                continue
            nfiles += 1
            for i, me in enumerate(m["meshes"]):
                if me["idx"].size == 0 or me["idx"].max() >= me["V"]:
                    continue
                used, inv = np.unique(me["idx"], return_inverse=True)
                I = inv.astype(np.uint32).reshape(-1, 3)[:, ::-1].reshape(-1).astype("<u4").tobytes()
                UV = np.nan_to_num(me["uv"][used], nan=0.0, posinf=0.0, neginf=0.0).astype("<f4").tobytes()
                UVraw = me["uv"][used].astype("<f4").tobytes()
                c = dict(src=src, jbm=name, mesh=i, node=me["node"], mtype=me["mtype"],
                         bone=me["nrm_bone"][used].copy(), model=me["nrm_model"][used].copy(),
                         nfb=int(me["nrm_fallback"][used].sum()),
                         uvh=(hashlib.sha1(UV).digest(), hashlib.sha1(UVraw).digest()))
                idx[hashlib.sha1(exp_pos_bytes(me["pos"], used)).digest()].append(c)
                # ★ вторичный ключ — индексы: у мешей, где fix_degenerate_binds («мосты») переписал
                #   pos0 части вершин, POSITION в glb ≠ pos файла; индексы/UV/нормали не трогаются.
                idx2[hashlib.sha1(I).digest()].append(c)
                nmesh += 1
    if verbose:
        print("индекс: %d .jbm, %d мешей, %d уникальных POSITION" % (nfiles, nmesh, len(idx)))
    return dict(pos=dict(idx), idx=dict(idx2))


# ---------------- GLB ----------------
def read_glb(path):
    b = open(path, "rb").read()
    magic, ver, length = struct.unpack_from("<III", b, 0)
    assert magic == 0x46546C67 and ver == 2, path
    off = 12; chunks = []
    while off < length:
        clen, ctype = struct.unpack_from("<II", b, off); off += 8
        chunks.append((ctype, off, clen)); off += clen
    assert chunks[0][0] == 0x4E4F534A and chunks[1][0] == 0x004E4942, path   # JSON, BIN
    js = json.loads(b[chunks[0][1]:chunks[0][1] + chunks[0][2]].decode("utf-8"))
    return b, js, chunks[1][1], chunks[1][2]


def acc_range(js, ai, bin_off):
    """(абс. смещение в файле, длина) плотного аксессора ai."""
    a = js["accessors"][ai]; bv = js["bufferViews"][a["bufferView"]]
    assert bv.get("buffer", 0) == 0 and "byteStride" not in bv
    comp = {5120: 1, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4}[a["componentType"]]
    n = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[a["type"]]
    return bin_off + bv.get("byteOffset", 0) + a.get("byteOffset", 0), a["count"] * comp * n


def geo_cos(P, N, I):
    """(Σcos, число вершин cos<0, число учтённых) для cos(NORMAL, геометрическая по винду glb)."""
    tri = I.reshape(-1, 3)
    fn = np.cross(P[tri[:, 1]] - P[tri[:, 0]], P[tri[:, 2]] - P[tri[:, 0]])
    vn = np.zeros_like(P)
    for k in range(3):
        np.add.at(vn, tri[:, k], fn)
    Nn = np.nan_to_num(N)
    la, lb = np.linalg.norm(vn, axis=1), np.linalg.norm(Nn, axis=1)
    ok = (la > 0) & (lb > 1e-6) & np.isfinite(N).all(1)
    if not ok.any():
        return None
    c = (vn[ok] / la[ok][:, None] * Nn[ok] / lb[ok][:, None]).sum(1)
    return float(c.sum()), int((c < 0).sum()), int(ok.sum())


_INDEX = None


def _init_worker(pkl):
    global _INDEX
    _INDEX = pickle.load(open(pkl, "rb"))


def process_glb(args):
    """dict-результат по файлу. apply=True → бэкап + запись NORMAL."""
    path, apply = args
    rel = os.path.relpath(path, RAW_MODELS).replace("\\", "/")
    res = dict(file=rel, prims=0, matched=0, status="ok", paths=collections.Counter(), notes=[],
               before=[0.0, 0, 0], after=[0.0, 0, 0], nfb=0, srcs=collections.Counter())
    try:
        b, js, bin_off, bin_len = read_glb(path)
    except Exception as e:
        res["status"] = "glb-parse-fail"; res["notes"].append(str(e)[:80]); res["paths"] = {}; res["srcs"] = {}
        return res
    new = bytearray(b); writes = []
    for mesh in js.get("meshes", []):
        for prim in mesh.get("primitives", []):
            pi = res["prims"]; res["prims"] += 1
            at = prim["attributes"]
            if "NORMAL" not in at or "POSITION" not in at or "indices" not in prim:
                res["notes"].append("prim %d без NORMAL/indices" % pi); res["status"] = "no-normal"; continue
            po, pl = acc_range(js, at["POSITION"], bin_off); no, nl = acc_range(js, at["NORMAL"], bin_off)
            io_, il = acc_range(js, prim["indices"], bin_off)
            pos_b = b[po:po + pl]; old_n = b[no:no + nl]
            P = np.frombuffer(pos_b, "<f4").reshape(-1, 3)
            ia = js["accessors"][prim["indices"]]
            I = np.frombuffer(b[io_:io_ + il], {5123: "<u2", 5125: "<u4", 5121: "u1"}[ia["componentType"]]).astype(np.int64)
            if I.size == 0 or I.max() >= len(P):
                res["notes"].append("prim %d: индекс вне POSITION" % pi); res["status"] = "bad-index"; continue
            cands = _INDEX["pos"].get(hashlib.sha1(pos_b).digest(), []); via = ""
            if not cands:
                # вторичный путь (см. build_index): индексы + UV совпадают, POSITION частично переписан «мостами»
                uo, ul = acc_range(js, at["TEXCOORD_0"], bin_off) if "TEXCOORD_0" in at else (0, 0)
                uvh = hashlib.sha1(b[uo:uo + ul]).digest() if ul else None
                Ib = I.astype("<u4").tobytes()
                cands = [c for c in _INDEX["idx"].get(hashlib.sha1(Ib).digest(), []) if uvh in c["uvh"]]
                via = "~bridge"
            hit = None; newb = None; variants_new = set()
            for c in cands:
                used = np.arange(len(c["bone"]))
                v_old = nrm_variants(c["bone"], used)
                for k, vb in v_old.items():
                    if vb == old_n:
                        nb = nrm_variants(c["model"], used)[k]
                        variants_new.add(nb)
                        if hit is None:
                            hit = (c, k); newb = nb; res["nfb"] += c["nfb"]; res["srcs"][c["src"]] += 1
            if hit is None:
                # уже перегнан? (старый NORMAL == модельный вариант) → идемпотентный повтор
                for c in cands:
                    vn = nrm_variants(c["model"], np.arange(len(c["model"])))
                    if old_n in vn.values():
                        hit = (c, "already"); break
                if hit is not None:
                    res["matched"] += 1; res["paths"]["already"] += 1
                    ga = geo_cos(P, np.frombuffer(old_n, "<f4").reshape(-1, 3), I)
                    if ga:
                        res["after"][0] += ga[0]; res["after"][1] += ga[1]; res["after"][2] += ga[2]
                    continue
                res["status"] = "no-match"
                res["notes"].append("prim %d: кандидатов %d, старый NORMAL не совпал ни с одним вариантом" % (pi, len(cands)))
                continue
            if len(variants_new) > 1:
                res["status"] = "ambiguous"; res["notes"].append("prim %d: разные nrm_model у кандидатов" % pi); continue
            res["matched"] += 1; res["paths"][hit[1] + via] += 1
            gb = geo_cos(P, np.frombuffer(old_n, "<f4").reshape(-1, 3), I)
            ga = geo_cos(P, np.frombuffer(newb, "<f4").reshape(-1, 3), I)
            for acc, g in ((res["before"], gb), (res["after"], ga)):
                if g:
                    acc[0] += g[0]; acc[1] += g[1]; acc[2] += g[2]
            writes.append((no, newb))
    res["paths"] = dict(res["paths"]); res["srcs"] = dict(res["srcs"])
    if res["status"] == "ok" and res["matched"] != res["prims"]:
        res["status"] = "partial"
    if apply and res["status"] == "ok" and writes:
        prev = os.path.join(PREV_DIR, rel)
        os.makedirs(os.path.dirname(prev), exist_ok=True)
        if not os.path.isfile(prev):
            open(prev, "wb").write(b)
        if os.path.getsize(prev) != len(b) or os.path.getsize(prev) == 0:
            res["status"] = "backup-fail"; return res
        for off, nb in writes:
            new[off:off + len(nb)] = nb
        tmp = path + ".tmp"; open(tmp, "wb").write(bytes(new))
        try:
            os.replace(tmp, path)
        except PermissionError:                       # ★ glb под Perforce лежат read-only — снять атрибут
            import stat
            os.chmod(path, stat.S_IREAD | stat.S_IWRITE); os.replace(tmp, path)
        res["written"] = True
    return res


def list_glb(only=None):
    out = []
    for root, dirs, files in os.walk(RAW_MODELS):
        dirs[:] = [d for d in dirs if not d.startswith("_")]      # бэкапы/_AnimTest не трогаем
        for f in files:
            if f.lower().endswith(".glb"):
                p = os.path.join(root, f)
                if only and not os.path.relpath(p, RAW_MODELS).replace("\\", "/").startswith(only):
                    continue
                out.append(p)
    return sorted(out)


def verify(nsamples):
    """Сверка бэкап↔текущий: байты равны всюду, кроме аксессоров NORMAL; POSITION/JOINTS/WEIGHTS/UV/индексы/IBM/анимации равны."""
    files = [p for p in list_glb() if os.path.isfile(os.path.join(PREV_DIR, os.path.relpath(p, RAW_MODELS)))]
    random.seed(20260905); bad = 0; checked = 0
    if nsamples > 0:
        files = random.sample(files, min(nsamples, len(files)))
    for p in files:
        rel = os.path.relpath(p, RAW_MODELS).replace("\\", "/"); prev = os.path.join(PREV_DIR, rel)
        a, ja, aoff, alen = read_glb(prev); b, jb, boff, blen = read_glb(p)
        checked += 1
        if len(a) != len(b) or ja != jb:
            print("  ✗ %s: JSON/длина отличаются" % rel); bad += 1; continue
        allowed = np.zeros(len(a), bool)
        for mesh in ja["meshes"]:
            for prim in mesh["primitives"]:
                o, l = acc_range(ja, prim["attributes"]["NORMAL"], aoff); allowed[o:o + l] = True
        diff = np.frombuffer(a, np.uint8) != np.frombuffer(b, np.uint8)
        outside = int((diff & ~allowed).sum())
        same_keys = True
        for mesh in ja["meshes"]:
            for prim in mesh["primitives"]:
                for key in ("POSITION", "JOINTS_0", "WEIGHTS_0", "TEXCOORD_0"):
                    if key in prim["attributes"]:
                        o, l = acc_range(ja, prim["attributes"][key], aoff)
                        same_keys &= a[o:o + l] == b[o:o + l]
                o, l = acc_range(ja, prim["indices"], aoff); same_keys &= a[o:o + l] == b[o:o + l]
        for sk in ja.get("skins", []):
            o, l = acc_range(ja, sk["inverseBindMatrices"], aoff); same_keys &= a[o:o + l] == b[o:o + l]
        for an in ja.get("animations", []):
            for s in an["samplers"]:
                for k in ("input", "output"):
                    o, l = acc_range(ja, s[k], aoff); same_keys &= a[o:o + l] == b[o:o + l]
        ok = outside == 0 and same_keys
        if not ok:
            bad += 1
        print("  %s %s: байт вне NORMAL отличается=%d, ключевые аксессоры равны=%s, NORMAL изменён байт=%d"
              % ("OK" if ok else "FAIL", rel, outside, same_keys, int((diff & allowed).sum())))
    print("проверено %d, дефектов %d" % (checked, bad))
    return bad == 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry", action="store_true"); ap.add_argument("--apply", action="store_true")
    ap.add_argument("--verify", type=int, default=None); ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--only", default=None)
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    ap.add_argument("--stats", action="store_true"); ap.add_argument("--rebuild-index", action="store_true")
    a = ap.parse_args()
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    if a.verify is not None:
        sys.exit(0 if verify(a.verify) else 1)
    os.makedirs(REPORT_DIR, exist_ok=True)
    t0 = time.time()
    if a.rebuild_index or not os.path.isfile(INDEX_PKL):
        pickle.dump(build_index(), open(INDEX_PKL, "wb"), protocol=pickle.HIGHEST_PROTOCOL)
    files = list_glb(a.only)
    if a.limit:
        files = files[:a.limit]
    apply = bool(a.apply) and not a.dry
    print("файлов .glb: %d, режим: %s, jobs=%d" % (len(files), "ЗАПИСЬ" if apply else "проверка", a.jobs))
    tasks = [(p, apply) for p in files]
    if a.jobs > 1 and len(files) > 50:
        import multiprocessing as mp
        with mp.Pool(a.jobs, initializer=_init_worker, initargs=(INDEX_PKL,)) as pool:
            results = pool.map(process_glb, tasks, chunksize=16)
    else:
        _init_worker(INDEX_PKL); results = [process_glb(t) for t in tasks]
    cats = collections.defaultdict(lambda: dict(files=0, ok=0, prims=0, matched=0, paths=collections.Counter(),
                                                 srcs=collections.Counter(), b=[0.0, 0, 0], a=[0.0, 0, 0], nfb=0, bad=[]))
    for r in results:
        cat = "/".join(r["file"].split("/")[:-1]); c = cats[cat]
        c["files"] += 1; c["prims"] += r["prims"]; c["matched"] += r["matched"]; c["nfb"] += r["nfb"]
        for k, v in r["paths"].items():
            c["paths"][k] += v
        for k, v in r["srcs"].items():
            c["srcs"][k] += v
        if r["status"] == "ok":
            c["ok"] += 1
        else:
            c["bad"].append((r["file"], r["status"], r["notes"][:2]))
        for acc, src in ((c["b"], r["before"]), (c["a"], r["after"])):
            acc[0] += src[0]; acc[1] += src[1]; acc[2] += src[2]
    print("\n%-22s %5s %5s %6s | %-24s | %-14s | %-14s | %s" % ("категория", "файлы", "ок", "прим.", "пайплайн", "cos до / <0 %", "cos после / <0 %", "геом.замен"))
    tot_b = [0.0, 0, 0]; tot_a = [0.0, 0, 0]; nbad = 0
    for cat in sorted(cats):
        c = cats[cat]; b_, a_ = c["b"], c["a"]
        fb = ("%.3f / %.1f" % (b_[0] / b_[2], 100.0 * b_[1] / b_[2])) if b_[2] else "-"
        fa = ("%.3f / %.1f" % (a_[0] / a_[2], 100.0 * a_[1] / a_[2])) if a_[2] else "-"
        print("%-22s %5d %5d %6d | %-24s | %-14s | %-14s | %d" % (cat, c["files"], c["ok"], c["prims"],
              ",".join("%s:%d" % kv for kv in sorted(c["paths"].items())), fb, fa, c["nfb"]))
        for i in range(3):
            tot_b[i] += b_[i]; tot_a[i] += a_[i]
        nbad += len(c["bad"])
    if tot_b[2]:
        print("%-22s %5d %5d %6s | %-24s | %-14s | %-14s |" % ("ИТОГО", sum(c["files"] for c in cats.values()),
              sum(c["ok"] for c in cats.values()), "", "",
              "%.3f / %.1f" % (tot_b[0] / tot_b[2], 100.0 * tot_b[1] / tot_b[2]),
              "%.3f / %.1f" % (tot_a[0] / tot_a[2], 100.0 * tot_a[1] / tot_a[2])))
    print("источники по категориям:", {k: dict(v["srcs"]) for k, v in cats.items()})
    if nbad:
        print("\nНЕ обработаны (%d):" % nbad)
        for cat in sorted(cats):
            for f, st, notes in cats[cat]["bad"][:40]:
                print("  %-60s %-12s %s" % (f, st, "; ".join(notes)))
    out = dict(mode="apply" if apply else "dry", when=time.strftime("%Y-%m-%d %H:%M:%S"), seconds=round(time.time() - t0, 1),
               categories={k: dict(files=v["files"], ok=v["ok"], prims=v["prims"], matched=v["matched"], paths=dict(v["paths"]),
                                   srcs=dict(v["srcs"]), before=v["b"], after=v["a"], geo_fallback_verts=v["nfb"], bad=v["bad"])
                           for k, v in cats.items()},
               per_file=results if a.stats else None)
    dst = STATS_JSON if apply else STATS_JSON.replace(".json", "_dry.json")
    json.dump(out, open(dst, "w", encoding="utf-8"), ensure_ascii=False, indent=1, default=str)
    print("\n%.1f с; JSON → %s" % (time.time() - t0, dst))


if __name__ == "__main__":
    main()
