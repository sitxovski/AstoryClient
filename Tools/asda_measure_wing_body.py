# Copyright (c) 2026 @sitxovski (Discord)
# asda_measure_wing_body.py — ЖЕНСКИЙ СЛУЧАЙ: сидит ли крыло правильно ОТНОСИТЕЛЬНО ТЕЛА.
# Оригинал (замер по ch4_{m|f}_00_*.jbb): в клипах idle02/idle03/sitdown01/death01 ВСЯ ЖЕНСКАЯ ПОЗА
# авторена развёрнутой на ~52..54° внутри модельного пространства (плечи 7→_7: муж −0.0°, жен +51.8°),
# и кость крыла "G" развёрнута ВМЕСТЕ с телом (размах +54.0° при плечах +51.8° = расхождение 2.2°).
# ⇒ 57° — это разворот ПОЗЫ, а не ошибка кости. Скрипт проверяет то же на НАШЕМ импорте:
# азимут размаха крыла (−Z кости в клиентском Y-up) vs азимут плеч — должен совпадать у обоих полов.
import sys, math
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

OUT = r"@@ASTORY_DEV_ROOT@@\Reports\wing_body_measure.txt"
L = []
def log(m):
    L.append(str(m)); unreal.log_warning("[wingbody] %s" % m)

def quat_to_m(q):
    x,y,z,w = q
    return [[1-2*(y*y+z*z), 2*(x*y+w*z),   2*(x*z-w*y)],
            [2*(x*y-w*z),   1-2*(x*x+z*z), 2*(y*z+w*x)],
            [2*(x*z+w*y),   2*(y*z-w*x),   1-2*(x*x+y*y)]]
def m_mul(A,B): return [[sum(A[i][k]*B[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
def m_transpose(M): return [[M[j][i] for j in range(3)] for i in range(3)]
def v_mul(v,M): return [sum(v[k]*M[k][j] for k in range(3)) for j in range(3)]
def norm(v):
    n = math.sqrt(sum(x*x for x in v)) or 1.0
    return [x/n for x in v]

# наш сырой скелет Z-up → клиентский Y-up: F = (x, z, −y)
F = [[1,0,0],[0,0,-1],[0,1,0]]

def pose_at(anim_path, t):
    anim = unreal.EditorAssetLibrary.load_asset(anim_path)
    if not anim: return None
    return unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, float(t), unreal.AnimPoseEvaluationOptions())
def bone_cs(pose, name):
    xf = unreal.AnimPoseExtensions.get_bone_pose(pose, name, unreal.AnimPoseSpaces.WORLD)
    q = xf.rotation; p = xf.translation
    return (q.x,q.y,q.z,q.w),(p.x,p.y,p.z)
def azi(v): return math.degrees(math.atan2(v[2], v[0]))   # азимут в клиентской горизонтали (X-Z)

# ассеты: 12=idle01 13=idle02 14=idle03 15=sitdown01 16=walk01 17=run01 18=death01 19=back01 35=move_00
CLIPS = [(12,"idle01"),(13,"idle02"),(14,"idle03"),(15,"sitdown01"),(16,"walk01"),(17,"run01"),
         (18,"death01"),(19,"back01"),(35,"move_00")]
BASE = "/Game/AsdaAssets/Models/Skeletal/Characters/ch1_%s_cremmy_preview/SkeletalMeshes/ch1_%s_cremmy_previewanim_%02d"

def main():
    log("Проверка: размах крыла (−Z кости 'M') vs латераль плеч (7→_7), клиентский Y-up, CS")
    log("ОРИГИНАЛ (ch4_*.jbb): муж idle02 размах −3.0° плечи −0.0° (Δ3.0°); жен idle02 размах +54.0° плечи +51.8° (Δ2.2°)")
    for idx,nm in CLIPS:
        line = "  %-10s" % nm
        for g in ("m","f"):
            path = BASE % (g,g,idx)
            po = pose_at(path, 0.0)
            if not po: line += " | %s: НЕТ клипа" % g; continue
            names = [str(n) for n in unreal.AnimPoseExtensions.get_bone_names(po)]
            wb = next((n for n in names if n.lower()=="m"), None)
            s7 = next((n for n in names if n=="7"), None); s7m = next((n for n in names if n in ("_7","Null_7")), None)   # импорт переименовал зеркальные кости _X → Null_X
            if not (wb and s7 and s7m): line += " | %s: нет костей" % g; continue
            q,_ = bone_cs(po, wb)
            Rb = quat_to_m(q)
            RbY = m_mul(m_mul(m_transpose(F), Rb), F)          # в клиентский Y-up
            span = norm([-x for x in RbY[2]])                   # размах = −Z кости (канал rotY=90)
            _,p7 = bone_cs(po, s7); _,p7m = bone_cs(po, s7m)
            lat = norm(v_mul([p7m[i]-p7[i] for i in range(3)], F))
            d = abs(azi(span)-azi(lat)); d = min(d, 360-d); d = min(d, 180-d)
            line += " | %s: размах %+7.1f° плечи %+7.1f° Δ=%5.1f°" % (g, azi(span), azi(lat), d)
        log(line)
    with open(OUT,"w",encoding="utf-8") as f: f.write("\n".join(L))
    log("отчёт: %s" % OUT)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
