# Copyright (c) 2026 @sitxovski (Discord)
# _probe_mount_phase.py — ФАКТ-ПРОБА рассинхрона качания «транспорт vs сидящий персонаж».
# Источник истины: .jbb из CharMotion.baz (game-space, row-vector, S.eng_row_matrix).
# Сравниваем траекторию КОРНЕВОЙ кости (макс. амплитуда продольного хода) обоих idle-клипов
# и то же в собранных .glb (glTF-space, S.jointWorld = ZH·M^T·ZH).
# Запуск: python _probe_mount_phase.py [vehicle_model] [sit_set] [clip]
import sys, os, zipfile, importlib.util, json
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = _P.DATA
MOTION = GAME + r"\Character\Motion\CharMotion.baz"

spec = importlib.util.spec_from_file_location("S", os.path.join(HERE, "asda_skinned_to_gltf.py"))
S = importlib.util.module_from_spec(spec); sys.modules["S"] = S; spec.loader.exec_module(S)

VEH = sys.argv[1] if len(sys.argv) > 1 else "itm1_n_96_vehicle_0048"
SIT = sys.argv[2] if len(sys.argv) > 2 else "0015"
CLIP = sys.argv[3] if len(sys.argv) > 3 else "000"

def bone_tracks(jbb):
    """[(имя, keyCount, T[K,3] мировые game-транс, pitch[K] рад)] по каждой кости."""
    out = []
    for bn in jbb['bones']:
        kc = bn['keyCount']
        if kc < 1: continue
        Ts = []; Ps = []
        for k in range(kc):
            M = S.eng_row_matrix(bn['quat'][k], bn['pos'][k], bn['scale'][k], bn['conv'])
            Ts.append(M[3, :3])                      # row-vector: трансляция в строке 3
            # pitch = наклон локальной оси Z (вперёд) относительно горизонта: asin(z-компонента оси Z)
            fz = M[2, :3] / (np.linalg.norm(M[2, :3]) + 1e-12)   # строка 2 = образ оси Z
            Ps.append(np.arcsin(np.clip(fz[1], -1, 1)))          # подъём вперёд-оси над горизонтом (game Y=вверх)
        out.append((bn['name'].strip(), kc, np.array(Ts), np.array(Ps), bn['t'][:kc].copy()))
    return out

def pick_root(tracks):
    """Кость с макс. амплитудой продольного хода (game Z) + вертикали — «корневая» качалка."""
    best = None; besta = -1
    for nm, kc, T, P, t in tracks:
        if kc < 3: continue
        amp = (T[:, 2].max() - T[:, 2].min()) + (T[:, 1].max() - T[:, 1].min()) + 50.0*(P.max()-P.min())
        if amp > besta: besta = amp; best = (nm, kc, T, P, t)
    return best

def summarize(tag, tr):
    nm, kc, T, P, t = tr
    dur = float(t[-1]) if len(t) else 0
    print("%s: кость '%s' keys=%d dur=%.3fs" % (tag, nm, kc, dur))
    print("   game X range [%.2f..%.2f] Y range [%.2f..%.2f] Z range [%.2f..%.2f] pitch range [%.3f..%.3f] rad"
          % (T[:,0].min(), T[:,0].max(), T[:,1].min(), T[:,1].max(), T[:,2].min(), T[:,2].max(), P.min(), P.max()))
    return dur

def resample(t, v, tt):
    return np.interp(tt, t, v)

veh_fn = "%s_%s.jbb" % (VEH, CLIP)
sit_m = "ch1_n_sit_%s_m_%s.jbb" % (SIT, CLIP)


res = {}


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global Ps, tt
    zm = zipfile.ZipFile(MOTION)
    print("=== ПАРА: %s  +  %s ===" % (veh_fn, sit_m))
    jv = S.parse_jbb(zm.read(veh_fn))
    js = S.parse_jbb(zm.read(sit_m))
    tv = bone_tracks(jv); ts = bone_tracks(js)
    print("транспорт: костей=%d | сидячий: костей=%d" % (len(tv), len(ts)))
    rv = pick_root(tv); rs = pick_root(ts)
    dv = summarize("ТРАНСПОРТ", rv); ds = summarize("СИДЯЧИЙ  ", rs)
    # фаза: нормируем время каждого клипа на его длительность, сравниваем корреляцию компонент
    tt = np.linspace(0, 1, 64)
    nmv, _, Tv, Pv, tv_ = rv; nms, _, Ts_, Ps, ts_ = rs
    for lbl, a, b in (("gameX", Tv[:,0], Ts_[:,0]), ("gameY(верт)", Tv[:,1], Ts_[:,1]),
                      ("gameZ", Tv[:,2], Ts_[:,2]), ("pitch", Pv, Ps)):
        va = resample(tv_/max(dv,1e-6), a, tt); vb = resample(ts_/max(ds,1e-6), b, tt)
        va = va - va.mean(); vb = vb - vb.mean()
        na = np.linalg.norm(va); nb = np.linalg.norm(vb)
        c = float(va @ vb / (na*nb)) if na > 1e-6 and nb > 1e-6 else float('nan')
        res[lbl] = (c, float(a.max()-a.min()), float(b.max()-b.min()))
        print("  %-12s corr=%+.3f | амплитуда трансп=%.3f сидяч=%.3f" % (lbl, c, a.max()-a.min(), b.max()-b.min()))
    # несколько кадров цифрами (продольная=gameZ и pitch)
    print("\nкадры (нормированное время): trZ_veh trZ_sit | pitch_veh pitch_sit")
    for q in (0.0, 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875):
        zv = resample(tv_/max(dv,1e-6), Tv[:,2], [q])[0]; zs = resample(ts_/max(ds,1e-6), Ts_[:,2], [q])[0]
        pv = resample(tv_/max(dv,1e-6), Pv, [q])[0]; ps = resample(ts_/max(ds,1e-6), Ps, [q])[0]
        print("  t=%.3f  %+8.3f %+8.3f | %+7.4f %+7.4f" % (q, zv, zs, pv, ps))
    json.dump({k: v for k, v in res.items()}, open(os.path.join(HERE, "_out", "mount_phase_jbb.json"), "w"))
    print("\nJSON -> Tools/_out/mount_phase_jbb.json")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
