# Copyright (c) 2026 @sitxovski (Discord)
# _probe_mount_phase_glb.py — та же проба, но по СОБРАННЫМ .glb (то, что уходит в UE).
# Сverka: продольная компонента узла в glb против исходного .jbb (glTF z = -game z, ZH).
# Запуск: python _probe_mount_phase_glb.py [vehicle_model] [sit_set] [gender] [clip]
import sys, os, json
import numpy as np
import pygltflib as g

HERE = os.path.dirname(os.path.abspath(__file__))
VEH = sys.argv[1] if len(sys.argv) > 1 else "itm1_n_96_vehicle_0048"
SIT = sys.argv[2] if len(sys.argv) > 2 else "0015"
GEN = sys.argv[3] if len(sys.argv) > 3 else "m"
CLIP = sys.argv[4] if len(sys.argv) > 4 else "000"

VEH_GLB = os.path.join(HERE, "..", "RawAssets", "Models", "Vehicle", VEH + ".glb")
SIT_GLB = os.path.join(HERE, "..", "RawAssets", "Models", "Skeletal", "Characters", "sit", "ch1_%s_cremmy_preview.glb" % GEN)
ANIM_VEH = "anim_%02d" % int(CLIP)
ANIM_SIT = "sit_%s_%s" % (SIT, CLIP)

def read_acc(gl, blob, ai):
    a = gl.accessors[ai]; v = gl.bufferViews[a.bufferView]
    off = (v.byteOffset or 0) + (a.byteOffset or 0)
    n = {"SCALAR":1, "VEC3":3, "VEC4":4}[a.type]
    return np.frombuffer(blob, np.float32, a.count*n, off).reshape(a.count, n)

def anim_channel(path_glb, anim_name, bone_name=None):
    gl = g.GLTF2().load_binary(path_glb); blob = gl.binary_blob()
    an = next((a for a in gl.animations if a.name == anim_name), None)
    if an is None: raise SystemExit("нет анимации %s в %s (есть: %s)" % (anim_name, path_glb, [a.name for a in gl.animations][:8]))
    best = None; besta = -1
    for ch in an.channels:
        if ch.target.path != "translation": continue
        node = gl.nodes[ch.target.node]
        sm = an.samplers[ch.sampler]
        t = read_acc(gl, blob, sm.input)[:, 0]
        T = read_acc(gl, blob, sm.output)
        if bone_name is not None:
            if (node.name or "").strip().lower() == bone_name.lower():
                return node.name, t, T
            continue
        if len(t) < 3: continue
        amp = float(T[:, 2].max()-T[:, 2].min()) + float(T[:, 1].max()-T[:, 1].min())
        if amp > besta: besta = amp; best = (node.name, t, T)
    if best is None: raise SystemExit("канал не найден")
    return best

nb_veh = sys.argv[5] if len(sys.argv) > 5 else None


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    nm_v, t_v, T_v = anim_channel(os.path.abspath(VEH_GLB), ANIM_VEH, "Bone06")
    nm_s, t_s, T_s = anim_channel(os.path.abspath(SIT_GLB), ANIM_SIT, "_7")
    print("glb ТРАНСПОРТ: узел '%s' keys=%d dur=%.3f  glZ range [%.2f..%.2f]" % (nm_v, len(t_v), t_v[-1], T_v[:,2].min(), T_v[:,2].max()))
    print("glb СИДЯЧИЙ : узел '%s' keys=%d dur=%.3f  glZ range [%.2f..%.2f]" % (nm_s, len(t_s), t_s[-1], T_s[:,2].min(), T_s[:,2].max()))
    tt = np.linspace(0, 1, 64)
    va = np.interp(tt, t_v/t_v[-1], T_v[:, 2]); vb = np.interp(tt, t_s/t_s[-1], T_s[:, 2])
    va0 = va-va.mean(); vb0 = vb-vb.mean()
    c = float(va0@vb0/(np.linalg.norm(va0)*np.linalg.norm(vb0)))
    print("corr(glZ_veh, glZ_sit) = %+.3f" % c)
    print("\nкадры: glZ_veh glZ_sit")
    for q in (0.0, 0.25, 0.5, 0.75):
        print("  t=%.2f  %+8.3f %+8.3f" % (q, np.interp(q, t_v/t_v[-1], T_v[:,2]), np.interp(q, t_s/t_s[-1], T_s[:,2])))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
