# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""РЕШАЮЩИЙ ЗАМЕР: одинаково ли ориентированы МЕШ ОРУЖИЯ и ЭФФЕКТ на кости 8_a1.

  меш   — UE-композиция FTransform: Rel · Sk.GetSocketTransform(bone, WORLD)
  эффект— матричная математика порта: A = FxToBone · BoneCS · CompWorld, Q = normalize(A)

При зеркальном масштабе компонента (0.01,−0.01,0.01) FTransform-композиция НЕ равна
матричному произведению ⇒ ориентации расходятся. Мерим расхождение осей в градусах и
позиции точек клинка.
"""
import sys, math
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

SKEL = "/Game/AsdaAssets/Models/Skeletal/Characters/ch1_m_cremmy_preview/SkeletalMeshes/ch1_m_cremmy_preview"
BONES = ["8_a1", "_8_a1", "_7_a1"]
MESHSCALE = unreal.Vector(0.01, -0.01, 0.01)


def log(m): unreal.log_warning("[mirror] %s" % m)


def m_mul(a, b): return [[sum(a[i][k]*b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]
def v_rot(v, m): return [sum(v[k]*m[k][j] for k in range(3)) for j in range(3)]
def v_xf(v, m): return [v_rot(v, m)[j] + m[3][j] for j in range(3)]


def t_to_m(t):
    q = t.rotation; s = t.scale3d; l = t.translation
    x, y, z, w = q.x, q.y, q.z, q.w
    r = [[1-2*(y*y+z*z), 2*(x*y+z*w),   2*(x*z-y*w),   0],
         [2*(x*y-z*w),   1-2*(x*x+z*z), 2*(y*z+x*w),   0],
         [2*(x*z+y*w),   2*(y*z-x*w),   1-2*(x*x+y*y), 0],
         [0, 0, 0, 1]]
    for i in range(3):
        for j in range(3): r[i][j] *= (s.x, s.y, s.z)[i]
    r[3] = [l.x, l.y, l.z, 1]
    return r


def m_norm(m):
    o = [r[:] for r in m]
    for i in range(3):
        n = math.sqrt(sum(o[i][j]**2 for j in range(3))) or 1.0
        for j in range(3): o[i][j] /= n
    o[3] = [0, 0, 0, 1]
    return o


def ang(a, b):
    d = sum(a[i]*b[i] for i in range(3))
    na = math.sqrt(sum(c*c for c in a)); nb = math.sqrt(sum(c*c for c in b))
    return math.degrees(math.acos(max(-1.0, min(1.0, d/(na*nb or 1.0)))))


def main():
    sk = unreal.EditorAssetLibrary.load_asset(SKEL)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.new_level("/Game/Temp/_mirror_probe")
    a = eas.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(0, 0, 0))
    c = a.skeletal_mesh_component
    c.set_skeletal_mesh_asset(sk)
    # зеркальный масштаб MeshRoot порта
    c.set_world_scale3d(MESHSCALE)
    CompW = t_to_m(c.get_world_transform())
    log("CompWorld масштаб=%s" % c.get_world_transform().scale3d)
    FxToBone = [[0, 100, 0, 0], [100, 0, 0, 0], [0, 0, 100, 0], [0, 0, 0, 1]]
    for bn in BONES:
        try:
            Tc = c.get_socket_transform(unreal.Name(bn), unreal.RelativeTransformSpace.RTS_COMPONENT)
            Tw = c.get_socket_transform(unreal.Name(bn), unreal.RelativeTransformSpace.RTS_WORLD)
        except Exception as e:
            log("кость %s: %s" % (bn, e)); continue
        BoneCS = t_to_m(Tc)
        S = m_mul(BoneCS, CompW)                        # МАТРИЧНО (как считает эффект)
        Mue = t_to_m(Tw)                                # UE FTransform-композиция (как ставится МЕШ)
        A = m_mul(FxToBone, S)
        Q = m_norm(A)
        log("=" * 96)
        log("кость %s" % bn)
        log("  матрично  S: строки  X=(%.4f,%.4f,%.4f) Y=(%.4f,%.4f,%.4f) Z=(%.4f,%.4f,%.4f) T=(%.2f,%.2f,%.2f)"
            % (S[0][0], S[0][1], S[0][2], S[1][0], S[1][1], S[1][2], S[2][0], S[2][1], S[2][2], S[3][0], S[3][1], S[3][2]))
        log("  UE-сокет  M: строки  X=(%.4f,%.4f,%.4f) Y=(%.4f,%.4f,%.4f) Z=(%.4f,%.4f,%.4f) T=(%.2f,%.2f,%.2f)"
            % (Mue[0][0], Mue[0][1], Mue[0][2], Mue[1][0], Mue[1][1], Mue[1][2], Mue[2][0], Mue[2][1], Mue[2][2], Mue[3][0], Mue[3][1], Mue[3][2]))
        for i, nm in enumerate("XYZ"):
            log("    ось %s: расхождение матрично↔UE = %.2f°" % (nm, ang(S[i][:3], Mue[i][:3])))
        log("  Δтрансляции = %.3f" % math.sqrt(sum((S[3][k]-Mue[3][k])**2 for k in range(3))))
        # точка «конец клинка»: raw (0,0,100)
        raw = (0.0, 0.0, 100.0)
        p_mesh = v_xf([raw[0]*100.0, -raw[2]*100.0, raw[1]*100.0], Mue)     # меш: asset=(x,−z,y)*100 через UE-сокет
        p_mesh_m = v_xf([raw[0]*100.0, -raw[2]*100.0, raw[1]*100.0], S)     # то же, но матрично
        ue_fx = [-raw[2], raw[0], raw[1]]
        p_fx = [v_rot(ue_fx, Q)[k] + A[3][k] for k in range(3)]             # эффект
        log("  точка клинка raw(0,0,100): меш(UE)=(%.2f,%.2f,%.2f)  меш(матрично)=(%.2f,%.2f,%.2f)  эффект=(%.2f,%.2f,%.2f)"
            % (p_mesh[0], p_mesh[1], p_mesh[2], p_mesh_m[0], p_mesh_m[1], p_mesh_m[2], p_fx[0], p_fx[1], p_fx[2]))
        log("  |эффект − меш(UE)| = %.2f   |эффект − меш(матрично)| = %.2f"
            % (math.sqrt(sum((p_fx[k]-p_mesh[k])**2 for k in range(3))),
               math.sqrt(sum((p_fx[k]-p_mesh_m[k])**2 for k in range(3)))))
    log("ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
