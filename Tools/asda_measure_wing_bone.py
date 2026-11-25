# Copyright (c) 2026 @sitxovski (Discord)
# asda_measure_wing_bone.py — ЧИСЛОВОЕ ДОКАЗАТЕЛЬСТВО формулы мировой матрицы крыльев (attach-10).
# Эталон (wings_soulmate_re.md §C): EffectWorld = boneLive("M"/"G") × charWorld.
# GROUND TRUTH live.dmp (char 0x27D30898, живая модель, кость "M" slot 1):
#   boneLive_cl = [(+0.0438 +0.0279 +0.9987) (−0.0012 +0.9996 −0.0279) (−0.9990 −0.0000 +0.0438)],
#   pos=(−1.15, 83.18, −0.31) — клиент держит скелет Y-up ×0.01 = F-сопряжение нашего Z-up скелета,
#   F=(x,z,−y). Отсюда константа порта: stored-fx (jem-импортёр ue=(fy,fx,fz)) → сырой скелет = SWAP(X,Y)·100.
#   Формула актора:  A = swapXY·100 · BoneCS_ue · Comp(ψ),  Comp = S(0.01,−0.01,0.01)·RotZ(ψ).
# Оригинальное крыло из дампа: размах 96 ГОРИЗОНТАЛЬНО, высота 75..129 см, pos-канал +25 вверх.
# Скрипт: кость M/G в обоих скелетах; поза anim_13 (мировой idle) t=0/0.5; F-сопряжение vs дамп;
# инвариант ориентации отн. персонажа при ψ=0/90/180; мировой bbox крыла (высота/размах/глубина).
import sys, math
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

OUT = r"@@ASTORY_DEV_ROOT@@\Reports\wing_bone_measure.txt"
L = []
def log(m):
    L.append(str(m)); unreal.log_warning("[wing] %s" % m)

# ── мини-матрицы (row-vector, как D3DX/UE FMatrix: v' = v·M) ──────────────────
def m_mul(A,B): return [[sum(A[i][k]*B[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
def v_mul(v,M): return [sum(v[k]*M[k][j] for k in range(3)) for j in range(3)]
def m_scale(sx,sy,sz): return [[sx,0,0],[0,sy,0],[0,0,sz]]
def m_rotz(deg):
    c,s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return [[c,s,0],[-s,c,0],[0,0,1]]
def m_det(M):
    return (M[0][0]*(M[1][1]*M[2][2]-M[1][2]*M[2][1])
          - M[0][1]*(M[1][0]*M[2][2]-M[1][2]*M[2][0])
          + M[0][2]*(M[1][0]*M[2][1]-M[1][1]*M[2][0]))
def m_norm_rows(M):
    out=[]
    for r in M:
        n = math.sqrt(sum(x*x for x in r)) or 1.0
        out.append([x/n for x in r])
    return out
def m_transpose(M): return [[M[j][i] for j in range(3)] for i in range(3)]
def quat_to_m(q):
    x,y,z,w = q
    return [[1-2*(y*y+z*z), 2*(x*y+w*z),   2*(x*z-w*y)],
            [2*(x*y-w*z),   1-2*(x*x+z*z), 2*(y*z+w*x)],
            [2*(x*z+w*y),   2*(y*z-w*x),   1-2*(x*x+y*y)]]
def m_to_quat(M):
    t = M[0][0]+M[1][1]+M[2][2]
    if t > 0:
        s = math.sqrt(t+1.0)*2; w = 0.25*s
        x = (M[1][2]-M[2][1])/s; y = (M[2][0]-M[0][2])/s; z = (M[0][1]-M[1][0])/s
    elif M[0][0] > M[1][1] and M[0][0] > M[2][2]:
        s = math.sqrt(1.0+M[0][0]-M[1][1]-M[2][2])*2
        x = 0.25*s; y = (M[1][0]+M[0][1])/s; z = (M[2][0]+M[0][2])/s; w = (M[1][2]-M[2][1])/s
    elif M[1][1] > M[2][2]:
        s = math.sqrt(1.0+M[1][1]-M[0][0]-M[2][2])*2
        x = (M[1][0]+M[0][1])/s; y = 0.25*s; z = (M[2][1]+M[1][2])/s; w = (M[2][0]-M[0][2])/s
    else:
        s = math.sqrt(1.0+M[2][2]-M[0][0]-M[1][1])*2
        x = (M[2][0]+M[0][2])/s; y = (M[2][1]+M[1][2])/s; z = 0.25*s; w = (M[0][1]-M[1][0])/s
    return (x,y,z,w)
def q_angle_between(a,b):
    d = min(1.0, abs(sum(x*y for x,y in zip(a,b))))
    return math.degrees(2*math.acos(d))
def fmt_m(M): return " | ".join("(%+.3f %+.3f %+.3f)"%tuple(r) for r in M)
def fmt_q(q): return "(%+.4f %+.4f %+.4f %+.4f)"%tuple(q)
def fmt_v(v): return "(%+.2f %+.2f %+.2f)"%tuple(v)

SWAP = [[0,1,0],[1,0,0],[0,0,1]]        # stored-fx → сырой скелет (оси; масштаб 100 сокращается с 0.01 при норм-ке)
F    = [[1,0,0],[0,0,-1],[0,1,0]]       # сырой Z-up → клиентский Y-up: (x,y,z)→(x,z,−y)
DUMP_BONE = [[+0.0438,+0.0279,+0.9987],[-0.0012,+0.9996,-0.0279],[-0.9990,-0.0000,+0.0438]]
DUMP_POS  = (-1.15, 83.18, -0.31)

def pose_at(anim_path, t):
    anim = unreal.EditorAssetLibrary.load_asset(anim_path)
    if not anim:
        log("НЕТ анимации: %s" % anim_path); return None
    opts = unreal.AnimPoseEvaluationOptions()
    return unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, float(t), opts)

def bone_cs(pose, name):
    xf = unreal.AnimPoseExtensions.get_bone_pose(pose, name, unreal.AnimPoseSpaces.WORLD)  # WORLD у AnimPose = component space
    q = xf.rotation; p = xf.translation
    return (q.x,q.y,q.z,q.w),(p.x,p.y,p.z)

def main():
    C = {
        "male":   ("M", "/Game/AsdaAssets/Models/Skeletal/Characters/ch1_m_cremmy_preview/SkeletalMeshes/ch1_m_cremmy_previewanim_13"),
        "female": ("G", "/Game/AsdaAssets/Models/Skeletal/Characters/ch1_f_cremmy_preview/SkeletalMeshes/ch1_f_cremmy_previewanim_13"),
    }
    # bbox крыла eff_wing_m_ss_00 (сохранённые UE-оси jem-импортёра) + каналы 0x12
    WING_BB = [(-0.1,-4.8,-3.3),(1.1,4.8,2.1)]
    CH_SCALE = 10.0; CH_ROTY = 90.0; POS_ST = (0.0,0.0,2.5)   # pos-канал (0,2.5,0) g → stored (−z,x,y) = (0,0,2.5)
    for sex,(bone, anim_path) in C.items():
        log("="*100)
        log("[%s] %s" % (sex, anim_path))
        pose0 = pose_at(anim_path, 0.0)
        if not pose0: continue
        names = [str(n) for n in unreal.AnimPoseExtensions.get_bone_names(pose0)]
        g_like = [n for n in names if "g" in n.lower()]
        log("костей: %d; с буквой G: %s" % (len(names), ", ".join(g_like) if g_like else "НЕТ"))
        found = next((n for n in names if n.lower() == bone.lower()), None)
        log("кость '%s': %s" % (bone, ("НАЙДЕНА как '%s'" % found) if found else "ОТСУТСТВУЕТ"))
        if not found:
            found = next((n for n in names if n.lower() == "m"), None)
            log("ФОЛБЭК на 'M': %s (рантайм логирует Warning)" % (("'%s'" % found) if found else "тоже нет!"))
            if not found: continue
        poses = {}
        for t in (0.0, 0.5):
            po = pose0 if t == 0.0 else pose_at(anim_path, t)
            q,p = bone_cs(po, found)
            poses[t] = (q,p)
            log("t=%.1f: кость '%s' CS pos=%s (сырые ед; ×0.01=%s см) quat=%s" %
                (t, found, fmt_v(p), fmt_v([x*0.01 for x in p]), fmt_q(q)))
        q0, p0 = poses[0.0]
        Rb = quat_to_m(q0)
        # ── СВЕРКА С ДАМПОМ: F-сопряжение нашей кости == живая матрица клиента ──
        if sex == "male":
            FC = m_mul(m_mul(m_transpose(F), Rb), F)     # F⁻¹·Rb·F
            pc = v_mul([x*0.01 for x in p0], F)
            log("F-сопряжение (наша кость → клиентский Y-up):")
            log("  порт: %s pos=%s" % (fmt_m(FC), fmt_v(pc)))
            log("  дамп: %s pos=%s" % (fmt_m(DUMP_BONE), fmt_v(DUMP_POS)))
            dq = q_angle_between(m_to_quat(FC), m_to_quat(m_norm_rows(DUMP_BONE)))
            log("  Δ ориентации порт↔дамп = %.2f° (поза дампа ≠ ровно t=0 клипа — допуск ~5°)" % dq)
        # ── ФОРМУЛА: A = SWAP·Rb·Comp(ψ) (норм-ка снимает 100·0.01) ──
        rel_ref = None; ok = True
        for psi in (0.0, 90.0, 180.0):
            Comp = m_mul(m_scale(0.01,-0.01,0.01), m_rotz(psi))
            A = m_mul(m_mul(SWAP, Rb), Comp)
            det = m_det(A); An = m_norm_rows(A)
            pos_w = v_mul(p0, Comp)
            qRel = m_to_quat(m_mul(An, m_transpose(m_rotz(psi))))
            log("ψ=%3.0f: det=%+.6f  A=%s" % (psi, det, fmt_m(An)))
            log("       posW=%s см  qRel(отн. персонажа)=%s" % (fmt_v(pos_w), fmt_q(qRel)))
            if rel_ref is None: rel_ref = qRel
            else:
                dang = q_angle_between(rel_ref, qRel)
                log("       ИНВАРИАНТ vs ψ=0: Δ=%.4f°  %s" % (dang, "OK" if dang < 0.01 else "НАРУШЕН"))
                if dang >= 0.01: ok = False
        log("ИНВАРИАНТ [%s]: %s" % (sex, "ДЕРЖИТСЯ (Δ<0.01°)" if ok else "НАРУШЕН!"))
        # позозависимость (t=0 vs t=0.5) — формула живёт на ЖИВОЙ позе, ориентация меняется с анимацией (как в оригинале)
        q5,_ = poses[0.5]
        d05 = q_angle_between(m_to_quat(m_norm_rows(m_mul(m_mul(SWAP,quat_to_m(q0)),m_mul(m_scale(0.01,-0.01,0.01),m_rotz(0))))),
                              m_to_quat(m_norm_rows(m_mul(m_mul(SWAP,quat_to_m(q5)),m_mul(m_scale(0.01,-0.01,0.01),m_rotz(0))))))
        log("живость позы: ориентация t=0 ↔ t=0.5 отличается на %.2f° (дыхание идла — норма)" % d05)
        # ── МИРОВОЙ BBOX КРЫЛА: RotChain((v+pos)·S) → A → +posW (ψ=0, персонаж лицом +Y мира) ──
        Comp0 = m_mul(m_scale(0.01,-0.01,0.01), m_rotz(0.0))
        An0 = m_norm_rows(m_mul(m_mul(SWAP, Rb), Comp0))
        pw0 = v_mul(p0, Comp0)
        c90,s90 = math.cos(math.radians(CH_ROTY)), math.sin(math.radians(CH_ROTY))
        def chain(v):
            x = [(v[i]+POS_ST[i])*CH_SCALE for i in range(3)]
            x = [x[0]*c90 - x[1]*s90, x[0]*s90 + x[1]*c90, x[2]]   # RotChain rotY(game) = вокруг stored-ueZ
            w = v_mul(x, An0)
            return [w[i]+pw0[i] for i in range(3)]
        corners = [(x,y,z) for x in (WING_BB[0][0],WING_BB[1][0]) for y in (WING_BB[0][1],WING_BB[1][1]) for z in (WING_BB[0][2],WING_BB[1][2])]
        ws = [chain(cn) for cn in corners]
        xs=[w[0] for w in ws]; ys=[w[1] for w in ws]; zs=[w[2] for w in ws]
        log("BBOX мира (ψ=0, лицо=+Y): X(латераль) %.1f..%.1f  Y(вперёд-назад) %.1f..%.1f  Z(высота) %.1f..%.1f см" %
            (min(xs),max(xs), min(ys),max(ys), min(zs),max(zs)))
        log("  эталон-дамп: размах 96 горизонтально, высота 74.7..129.2, глубина ~33; рост персонажа ~156")
    with open(OUT,"w",encoding="utf-8") as f: f.write("\n".join(L))
    log("отчёт: %s" % OUT)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
