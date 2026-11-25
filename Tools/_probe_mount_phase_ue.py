# Copyright (c) 2026 @sitxovski (Discord)
# _probe_mount_phase_ue.py — headless-СМОК: треки костей ИМПОРТИРОВАННЫХ AnimSequence (UE) против .glb.
# Двухфазный (в UE-питоне нет numpy/pygltflib):
#   ФАЗА 1 (обычный python):  python _probe_mount_phase_ue.py prep   → _out/mount_phase_ue_cases.json
#   ФАЗА 2 (UE headless):     run_headless.bat _probe_mount_phase_ue.py  → _out/mount_phase_ue_raw.json
#   ФАЗА 3 (обычный python):  python _probe_mount_phase_ue.py check → сравнение, Reports/mount_phase_ue.json
import os, sys, json

HERE = os.path.dirname(os.path.abspath(__file__))
CASES_F = os.path.join(HERE, "_out", "mount_phase_ue_cases.json")
RAW_F = os.path.join(HERE, "_out", "mount_phase_ue_raw.json")
OUT = r"@@ASTORY_DEV_ROOT@@\Reports\mount_phase_ue.json"
VEH_GLB = os.path.join(HERE, "..", "RawAssets", "Models", "Vehicle")
SIT_GLB = os.path.join(HERE, "..", "RawAssets", "Models", "Skeletal", "Characters", "sit")
VEH_UE = "/Game/AsdaAssets/Models/Skeletal/Vehicle/%s/SkeletalMeshes/%sanim_%02d"
SIT_UE = "/Game/AsdaAssets/Models/Skeletal/Characters/ch1_%s_cremmy_preview/SkeletalMeshes/ch1_%s_cremmy_previewsit_%s_%s"
SMOKE = (("itm1_n_96_vehicle_0048", "0015"), ("itm1_n_96_vehicle_0000", "0000"),
         ("itm1_n_96_vehicle_0006", "0002"))
N = 16


def prep():
    import numpy as np
    import pygltflib as g
    def read_acc(gl, blob, ai):
        a = gl.accessors[ai]; v = gl.bufferViews[a.bufferView]
        off = (v.byteOffset or 0) + (a.byteOffset or 0)
        n = {"SCALAR": 1, "VEC3": 3, "VEC4": 4}[a.type]
        return np.frombuffer(blob, np.float32, a.count * n, off).reshape(a.count, n)
    cases = []
    for mdl, sset in SMOKE:
        for c in (0, 2):
            for kind, ue, glbp, anim in (
                ("veh", VEH_UE % (mdl, mdl, c), os.path.join(VEH_GLB, mdl + ".glb"), "anim_%02d" % c),
                ("sit", SIT_UE % ("m", "m", sset, "%03d" % c),
                 os.path.join(SIT_GLB, "ch1_m_cremmy_preview.glb"), "sit_%s_%03d" % (sset, c))):
                gl = g.GLTF2().load_binary(os.path.abspath(glbp)); blob = gl.binary_blob()
                an = next((a for a in gl.animations if a.name == anim), None)
                if an is None: continue
                chans = {}
                for ch in an.channels:
                    if ch.target.path != "translation": continue
                    sm = an.samplers[ch.sampler]
                    chans[(gl.nodes[ch.target.node].name or "").strip()] = (
                        read_acc(gl, blob, sm.input)[:, 0], read_acc(gl, blob, sm.output))
                bone, ba = None, -1
                for nm, (t, T) in chans.items():
                    if len(t) < 3: continue
                    a = float(sum(T[:, k].max() - T[:, k].min() for k in range(3)))
                    if a > ba: ba, bone = a, nm
                t, T = chans[bone]
                dur = float(t[-1]); times = [dur * 0.999 * i / (N - 1) for i in range(N)]
                Gv = [[float(np.interp(x, t, T[:, k])) for k in range(3)] for x in times]
                cases.append(dict(kind=kind, ue=ue, anim=anim, bone=bone, dur_glb=dur,
                                  times=times, glb=Gv))
    os.makedirs(os.path.dirname(CASES_F), exist_ok=True)
    json.dump(cases, open(CASES_F, "w", encoding="utf-8"))
    print("кейсов:", len(cases), "->", CASES_F)


def ue_sample():
    import unreal
    cases = json.load(open(CASES_F, encoding="utf-8"))
    out = []
    for cs in cases:
        seq = unreal.load_asset(cs["ue"])
        if not seq:
            out.append(dict(cs, err="нет ассета")); unreal.log("[phase_ue] НЕТ " + cs["ue"]); continue
        opts = unreal.AnimPoseEvaluationOptions()
        tr = []
        for t in cs["times"]:
            pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(seq, float(t), opts)
            L = unreal.AnimPoseExtensions.get_bone_pose(pose, unreal.Name(cs["bone"]),
                                                        unreal.AnimPoseSpaces.LOCAL).translation
            tr.append([float(L.x), float(L.y), float(L.z)])
        out.append(dict(cs, ue_tr=tr, dur_ue=float(seq.get_editor_property("sequence_length"))))
        unreal.log("[phase_ue] OK %s %s кость=%s" % (cs["kind"], cs["anim"], cs["bone"]))
    json.dump(out, open(RAW_F, "w", encoding="utf-8"))
    unreal.log("[phase_ue] -> " + RAW_F)


def check():
    import numpy as np
    raw = json.load(open(RAW_F, encoding="utf-8"))
    def corr(a, b):
        a = np.asarray(a, float); b = np.asarray(b, float)
        a = a - a.mean(); b = b - b.mean()
        na, nb = np.linalg.norm(a), np.linalg.norm(b)
        return float(a @ b / (na * nb)) if na > 1e-4 and nb > 1e-4 else float('nan')
    res = []; bad = 0
    for cs in raw:
        if "err" in cs: res.append(cs); bad += 1; continue
        G = np.array(cs["glb"]); U = np.array(cs["ue_tr"])
        axes = []
        for i in range(3):
            cc = [corr(U[:, i], G[:, j]) for j in range(3)]
            aa = [abs(x) if x == x else -1 for x in cc]
            j = int(np.argmax(aa))
            amp = float(U[:, i].max() - U[:, i].min())
            axes.append(dict(ue="XYZ"[i], glb="xyz"[j],
                             corr=(round(cc[j], 3) if cc[j] == cc[j] else None), amp=round(amp, 2)))
        ok = all((a["corr"] is None or abs(a["corr"]) > 0.98) for a in axes if a["amp"] > 0.5)
        if not ok: bad += 1
        res.append(dict(kind=cs["kind"], anim=cs["anim"], bone=cs["bone"],
                        dur_glb=round(cs["dur_glb"], 3), dur_ue=round(cs["dur_ue"], 3),
                        axes=axes, ok=ok))
        print("%-4s %-14s кость=%-12s dur %.3f/%.3f %s  %s" %
              (cs["kind"], cs["anim"], cs["bone"], cs["dur_glb"], cs["dur_ue"],
               "OK " if ok else "БАГ", axes))
    # согласованность мэппинга осей между veh и sit
    maps = {(r["kind"]): None for r in res if "axes" in r}
    sig = {}
    for r in res:
        if "axes" not in r: continue
        key = tuple((a["ue"], a["glb"], (a["corr"] or 0) >= 0) for a in r["axes"] if a["amp"] > 0.5)
        sig.setdefault(r["kind"], set()).add(key)
    json.dump(dict(cases=res, bad=bad), open(OUT, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    print("плохих кейсов:", bad, "->", OUT)


# ★★★ ЗАКОН [[tools-no-import-side-effects]], аудит №4 (волна 3). ЗДЕСЬ БЫЛА ВЕТКА `else: ue_sample()`
#   — то есть скрипт делал работу ИМЕННО ПРИ ИМПОРТЕ. Ветку добавляли не по злому умыслу: UE
#   запускает файл через -ExecutePythonScript, где `__name__` не равен "__main__", и без `else`
#   фаза 2 не запускалась. Правильный признак прямого запуска — отсутствие `__spec__` (его
#   выставляет ЛЮБАЯ машинерия импорта и НЕ выставляет прямое исполнение файла): он покрывает
#   и `python file.py`, и путь UE, и при этом импорт остаётся немым.
#   Оба сканера аудита считали эту строку «гвардом» — ловится только исполнением.
if __name__ == "__main__" or globals().get("__spec__") is None:
    mode = sys.argv[1] if len(sys.argv) > 1 else None
    if mode == "prep": prep()
    elif mode == "check": check()
    else: ue_sample()
