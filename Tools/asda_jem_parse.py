#!/usr/bin/env python3
# Copyright (c) 2026 @sitxovski (Discord)
# asda_jem_parse.py — парсер .JEM (меш модель-эмиттеров, JEB типы 0x11/0x12/0x1F) из EffectModel.bnz.
# ПОРТ проверенного C++ asda_formats.cpp::ParseJem (байт-в-байт валидирован на 3 файлах).
# Выход: список мешей {verts:[(x,y,z)...UE-локально], normals, uvs, tex, scale}; треугольники де-индексированы
# (verts последовательно ×3). Оси: .jem = 3dsMax Z-up → game Y-up (x,z,-y) → UE (-z,x,y) ⇒ итог UE=(y,x,z).
import struct, zipfile, os, math, bisect
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P

BNZ = os.environ.get("ASDA_EFFECT_BNZ", _P.EFFECT_MODEL_BNZ)

def _base(p):
    p = p.replace("\\", "/").rsplit("/", 1)[-1]
    return os.path.splitext(p)[0].lower()

def parse_jem(d):
    n = len(d); o = [0]
    def u32():
        v = struct.unpack_from("<I", d, o[0])[0] if o[0]+4 <= n else 0; o[0] += 4; return v
    def f32():
        v = struct.unpack_from("<f", d, o[0])[0] if o[0]+4 <= n else 0.0; o[0] += 4; return v
    def s100():
        s = d[o[0]:o[0]+100].split(b"\x00")[0].decode("latin1","ignore") if o[0]+100 <= n else ""; o[0] += 100; return s
    out = []
    if n < 0x98: return out
    magic = u32(); ver = f32(); countD = u32(); countE = u32()
    if magic != 0x1f90 or abs(ver-1.0) > 1e-4: return out
    s100()
    for _ in range(6): u32()
    cA = u32(); cB = u32(); cC = u32()
    for _ in range(cA): s100()
    for _ in range(cB): s100()
    # ★ cC = ПАЛИТРА КОСТЕЙ: список node-id (у крыла [0,2,3,4,5,6] — все ноды кроме меша).
    #   Индекс кости в скине (FACE-блок) = индекс В ЭТОЙ ПАЛИТРЕ, не node-id (сверено: скин ссылается
    #   на палитр-идексы 2 и 4 → ноды 3 и 5 = ДВЕ МАШУЩИЕ кости крыла, симметрично 49+49 вершин).
    palette = [u32() for _ in range(cC)]
    nodes = {}          # node-id → {parent, pos/quat/scale-треки}
    tex = ""
    for _ in range(countD):
        for _ in range(14): u32()
        for _ in range(4):
            s = s100()
            if not tex and s: tex = _base(s)
    if countE > 4096: return out
    for _ in range(countE):
        if o[0] + 0x145 > n: break
        nodeName = s100()
        nodeId = u32(); u32(); parentRaw = u32()   # a[0]=node-id, a[2]=parent node-id (0xFFFFFFFF = корень)
        # ★ 30.10.2026: байт записи +0x70 = ПРИЗНАК СКИНА узла (эталон CEffectMesh_ctor@0x125B150 читает subMesh+40:
        #   Editable_mesh с байтом ≠ 0 → скинованный CMeshRenderBuffer, с 0 → жёсткий CEffectMesh_DrawLeaf@0x129B5D0).
        skinFlag = d[o[0]] if o[0] < n else 0
        o[0] += 1; u32()
        tm = [f32() for _ in range(37)]
        scale = tm[5] if tm[5] > 1e-4 else 1.0
        V1 = u32(); V2 = u32(); UV = u32(); u32(); u32()
        A3 = u32(); A4 = u32(); FACE = u32(); u32(); u32()
        A5 = u32(); A6 = u32(); A7 = u32(); A8 = u32(); A9 = u32()
        if V1 > 1000000 or A3 > 2000000: break
        posOff = o[0]; o[0] += V1*12
        nrmOff = o[0]; o[0] += V2*12
        uvOff  = o[0]; o[0] += UV*8
        a3Off  = o[0]; o[0] += A3*16
        o[0] += A4*12
        # ★ FACE-блок = СКИН (по одной записи на исходную вершину): a индексов палитры + b весов.
        #   У крыла a=b=1 у всех 98 вершин (жёсткая привязка), сумма весов 1.0.
        skinRaw = []
        for _ in range(FACE):
            a = u32(); b = u32()
            idx = list(struct.unpack_from("<%dI" % a, d, o[0])) if (a and o[0]+a*4 <= n) else []
            o[0] += a*4
            wts = list(struct.unpack_from("<%df" % b, d, o[0])) if (b and o[0]+b*4 <= n) else []
            o[0] += b*4
            skinRaw.append((idx, wts))
            if o[0] > n: o[0] = n; break
        # ★ A5/A6/A7 = АНИМАЦИОННЫЕ ТРЕКИ ноды (сверено дампом eff_wing_m_ss_00: 76 ключей, время 0..75
        #   = НОМЕРА КАДРОВ): A5 = (t,posX,posY,posZ) 16б, A6 = (t,qx,qy,qz,qw) 20б, A7 = (t,sX,sY,sZ) 16б.
        #   Именно эти треки и есть ВЗМАХ крыла (ноды 3/5 крутятся до 8.4°/7.7°) — эталон гоняет их через
        #   animTracks_evaluateAtTime(pMesh, animFrame) в effectNode_updateAndRenderMesh@0x1229810.
        trPos = [list(struct.unpack_from("<4f", d, o[0]+i*16)) for i in range(A5)] if o[0]+A5*16 <= n else []
        o[0] += A5*16
        trRot = [list(struct.unpack_from("<5f", d, o[0]+i*20)) for i in range(A6)] if o[0]+A6*20 <= n else []
        o[0] += A6*20
        trScl = [list(struct.unpack_from("<4f", d, o[0]+i*16)) for i in range(A7)] if o[0]+A7*16 <= n else []
        o[0] += A7*16
        a8Off = o[0]; o[0] += A8*48
        o[0] += A9*12
        # ★ tm[5..20] = ЛОКАЛЬНАЯ BIND-матрица ноды (4×4 row-major, трансляция в row3 совпадает с pos-треком),
        #   tm[21..36] = МИРОВАЯ bind-матрица. ДОКАЗАНО численно: |L_n · W_родителя − W_n| = 0.0000 для всех
        #   нод ⇒ композиция W = L·W_parent (row-vector), а не W_parent·L.
        #   Трек-кватернион = ДЕЛЬТА (в кадре 0 всегда единичный!) в РОДИТЕЛЬСКОМ пространстве —
        #   доказано зеркальной симметрией: у зеркальных костей крыла (ноды 3 и 5) дельты связаны
        #   сопряжением зеркала родителя q₅=(−x,+y,−z,w) от q₃ (при локальной трактовке они были бы РАВНЫ).
        nodes[nodeId] = dict(name=nodeName, parent=(-1 if parentRaw == 0xFFFFFFFF else parentRaw),
                             pos=trPos, rot=trRot, scl=trScl, scale=scale, skinned=bool(skinFlag),
                             bind=[list(tm[5+4*r:5+4*r+4]) for r in range(4)],
                             bindWorld=[list(tm[21+4*r:21+4*r+4]) for r in range(4)])
        if V1 == 0 or A3 == 0: continue
        def fat(off): return struct.unpack_from("<f", d, off)[0] if off+4 <= n else 0.0
        def uat(off): return struct.unpack_from("<I", d, off)[0] if off+4 <= n else 0
        # ★ ИСТИННАЯ схема (сверено оффлайн-рендером fanrender_combo, misses=0, 2026-07-27):
        #   ГЕОМЕТРИЯ = tri-блок @0x11D {flag,i0,i1,i2} (индексы в pos; у статики валиден);
        #   UV = из A8 РАЗВЁРНУТОГО вершбуфера (48б/корнер: pos3f + 4f мусор + normal3f@28 + uv2f@40)
        #   ПОДБОРОМ ПО ПОЗИЦИИ — A8 идёт в СВОЁМ порядке (не порядок tri-блока! тройки A8 подряд дают
        #   гигантские/вырожденные треугольники). Повтор позиции → очередной кандидат по порядку.
        #   Прежние схемы ломались: uv[corner] — UV-счётчик < корнеров (creation01: 50 на 126 → «жёсткие
        #   лезвия»); uv[vert] — мазня; A8 подряд — каша из гигантских треугольников.
        a8map = {}
        for c in range(A8):
            b = a8Off + c*48
            key = (round(fat(b),1), round(fat(b+4),1), round(fat(b+8),1))
            a8map.setdefault(key, []).append((fat(b+40), fat(b+44), fat(b+28), fat(b+32), fat(b+36)))
        a8used = {}
        verts=[]; norms=[]; uvs=[]; vbone=[]
        for t in range(A3):
            p = [uat(a3Off+t*16+4), uat(a3Off+t*16+8), uat(a3Off+t*16+12)]
            for k in range(3):
                vi = p[k] if p[k] < V1 else 0
                x=fat(posOff+vi*12); y=fat(posOff+vi*12+4); z=fat(posOff+vi*12+8)
                if vi < V2: nx=fat(nrmOff+vi*12); ny=fat(nrmOff+vi*12+4); nz=fat(nrmOff+vi*12+8)
                else: nx,ny,nz = 0.0,0.0,1.0
                u = v = 0.0
                key = (round(x,1), round(y,1), round(z,1))
                cand = a8map.get(key)
                if cand:
                    ui = a8used.get(key, 0)
                    rec = cand[min(ui, len(cand)-1)]
                    a8used[key] = ui + 1
                    u, v = rec[0], rec[1]
                    nx, ny, nz = rec[2], rec[3], rec[4]     # нормаль из того же A8-корнера
                else:
                    c = t*3+k
                    u = fat(uvOff+c*8) if c < UV else 0.0   # фолбэк: покорнерный uv-массив (старая схема)
                    v = fat(uvOff+c*8+4) if c < UV else 0.0
                # Max Z-up → game (x,z,-y) → UE (-z,x,y) ⇒ UE=(y,x,z); нормаль так же
                verts.append((y*scale, x*scale, z*scale))
                norms.append((ny, nx, nz))
                uvs.append((u, v))
                # скин-кость КОРНЕРА = доминирующий вес исходной вершины (у крыла всегда одна кость)
                if vi < len(skinRaw) and skinRaw[vi][0]:
                    ii, ww = skinRaw[vi]
                    best = max(range(len(ii)), key=lambda q: (ww[q] if q < len(ww) else 0.0))
                    vbone.append(int(ii[best]))
                else:
                    vbone.append(-1)
        out.append({"verts": verts, "normals": norms, "uvs": uvs, "tex": tex, "scale": scale, "tris": A3,
                    "vbone": vbone, "palette": palette, "nodes": nodes, "node": nodeId, "skinned": bool(skinFlag)})
    return out


# ── Скиннинг .jem: палитр-кость → матрицы по кадрам (STORED-UE оси, как verts) ────────────────
# Эталон: effectNode_updateAndRenderMesh@0x1229810 зовёт animTracks_evaluateAtTime(pMesh, animFrame),
# animFrame += 1.0 КАЖДЫЙ АПДЕЙТ-КАДР (effects_particles.c:103512) и заворачивается на trackSet[2..3]
# (= диапазон времён треков, у крыла 0..75). Bind-поза = КАДР 0 (вершины авторены в ней).
def _eng_row(q, pos, scl):
    """Движковая row-major матрица из (кватернион, позиция, масштаб) — как mathQuaternionToMatrix4x4."""
    x, y, z, w = q
    M = [[1-2*(y*y+z*z), 2*(x*y-w*z),   2*(x*z+w*y),   0.0],
         [2*(x*y+w*z),   1-2*(x*x+z*z), 2*(y*z-w*x),   0.0],
         [2*(x*z-w*y),   2*(y*z+w*x),   1-2*(x*x+y*y), 0.0],
         [pos[0], pos[1], pos[2], 1.0]]
    for r in range(3):
        for c in range(3): M[r][c] *= scl[r]
    return M

def _mm(A, B): return [[sum(A[i][k]*B[k][j] for k in range(4)) for j in range(4)] for i in range(4)]

def _inv(M):
    """Инверсия аффинной 4×4 (row-vector) через Гаусса — треки без сдвига/шира, но считаем общо."""
    n = 4
    A = [list(M[i]) + [1.0 if i == j else 0.0 for j in range(n)] for i in range(n)]
    for c in range(n):
        p = max(range(c, n), key=lambda r: abs(A[r][c]))
        A[c], A[p] = A[p], A[c]
        dv = A[c][c] or 1e-12
        A[c] = [v/dv for v in A[c]]
        for r in range(n):
            if r != c and A[r][c]:
                f = A[r][c]; A[r] = [v - f*u for v, u in zip(A[r], A[c])]
    return [r[n:] for r in A]

def _slerp(a, b, u):
    """Сферическая интерполяция кватернионов (x,y,z,w) — как пара ключей animSampleQuatTrack@0x12A2E70."""
    d = sum(p*q for p, q in zip(a, b))
    if d < 0.0: b = [-q for q in b]; d = -d
    if d > 0.9995:
        r = [p + (q-p)*u for p, q in zip(a, b)]
    else:
        th = math.acos(min(d, 1.0)); s = math.sin(th)
        ka = math.sin((1.0-u)*th)/s; kb = math.sin(u*th)/s
        r = [p*ka + q*kb for p, q in zip(a, b)]
    ln = math.sqrt(sum(c*c for c in r)) or 1.0
    return [c/ln for c in r]

def _key_at(track, frame, dim):
    """Значение трека на кадре `frame` ПО ВРЕМЕНИ КЛЮЧЕЙ (поле t = номер кадра); вне диапазона — крайний ключ.
    ★ 30.10.2026: эталон ищет ПАРУ ключей по времени и интерполирует (anim_FindKeyframePair* → vec3Lerp /
      кватернион — animTrack_SampleVec3@0x12A2DC0, animSampleQuatTrack@0x12A2E70). Раньше брался ключ с ИНДЕКСОМ
      = кадр: верно только для плотных треков (t_i = i — у всех .jem, кроме grened01 [0,100] и flammio-a_50 [0,100]),
      а у редких трек «прыгал» на последний ключ уже на кадре 1. Для плотных треков на целом кадре результат тот же."""
    if not track: return [0.0]*dim
    if frame <= track[0][0] or len(track) == 1: return track[0][1:1+dim]
    if frame >= track[-1][0]: return track[-1][1:1+dim]
    i = bisect.bisect_right([k[0] for k in track], frame) - 1
    a, b = track[i], track[i+1]
    if frame == a[0] or b[0] <= a[0]: return a[1:1+dim]
    u = (frame - a[0]) / (b[0] - a[0])
    if dim == 4: return _slerp(a[1:5], b[1:5], u)
    return [p + (q-p)*u for p, q in zip(a[1:1+dim], b[1:1+dim])]

# Узел-кость эталона: CEffectMesh_ctor@0x125B150 строит класс кости (cClass_updateInterpMatrix) для имён
#   BoneGeometry/Biped_Object/Bone и узла с id 0; Editable_mesh с байтом +0x70 ≠ 0 — скин; ВСЕ ПРОЧИЕ узлы
#   (жёсткий Editable_mesh, Dummy с id ≠ 0, Omnilight, …) — потомки aabbDrawer (aabbDrawer_construct@0x129A3D0).
_BONE_NODE_NAMES = ("BoneGeometry", "Biped_Object", "Bone")

def _is_rigid_node(nid, nd):
    """Узел с матрицей жёсткого узла эталона (aabbDrawer/CEffectMesh), а не кость и не скин."""
    return nid != 0 and not nd.get("skinned") and nd.get("name") not in _BONE_NODE_NAMES

def _rigid_world(nodes, nid, f, memo):
    """Мировая матрица ЖЁСТКОГО узла на кадре `f` — 1:1 objApplyAnimTransformToMatrix@0x129A5B0 (30.10.2026).
    aabbDrawer_construct кладёт ЛОКАЛЬНУЮ bind-матрицу узла (tm[5..20]) в матрицу отрисовки (её же умножает
    CEffectMesh_DrawLeaf@0x129B5D0 → MultiplyTransform); каждый кадр objBuildAnimTransform@0x12A2FE0 строит A(t):
    поворот из кватерниона трека, ДИАГОНАЛЬ × масштаб трека (абсолютный, не относительный), сдвиг из pos-трека;
    M = L·A, и сдвиг M = сдвиг A, а если он нулевой (трека нет) — сдвиг L; затем × матрица родителя.
    На плотных треках с масштабом 1 совпадает с веткой треков `_node_world` (проверено по всем .jem EffectModel.bnz)."""
    nd = nodes[nid]
    L = nd.get("bind") or [[1,0,0,0],[0,1,0,0],[0,0,1,0],[0,0,0,1]]
    A = [[1.0,0.0,0.0,0.0],[0.0,1.0,0.0,0.0],[0.0,0.0,1.0,0.0],[0.0,0.0,0.0,1.0]]
    if nd["rot"]:
        A = _eng_row(_key_at(nd["rot"], f, 4), [0.0, 0.0, 0.0], [1.0, 1.0, 1.0])
    if nd["scl"]:
        s = _key_at(nd["scl"], f, 3)
        for k in range(3): A[k][k] *= s[k]
    T = _key_at(nd["pos"], f, 3) if nd["pos"] else [0.0, 0.0, 0.0]
    M = _mm(L, A)
    M[3] = ([L[3][0], L[3][1], L[3][2], 1.0] if (T[0] == 0.0 and T[1] == 0.0 and T[2] == 0.0)
            else [T[0], T[1], T[2], 1.0])
    W = M if nd["parent"] < 0 else _mm(M, _node_world(nodes, nd["parent"], f, memo, True))
    memo[(nid, f)] = W
    return W

def _track_frames(nodes):
    """Число кадров анимации модели = последний ключ (по времени) самого длинного трека + 1 (эталон заворачивает
    animFrame на trackSet[2..3] — диапазон времён треков). Для плотных треков = длина трека."""
    last = [tr[-1][0] for nd in nodes.values() for tr in (nd["pos"], nd["rot"], nd["scl"]) if len(tr) >= 2]
    return int(round(max(last))) + 1 if last else 0

def _node_world(nodes, nid, f, memo, model_space=False):
    """Мировая матрица узла `nid` на кадре `f` (row-vector, файловые оси). Общая для skin_frames и model_mesh."""
    if (nid, f) in memo: return memo[(nid, f)]
    nd = nodes.get(nid)
    if nd is None: return [[1 if i == j else 0 for j in range(4)] for i in range(4)]
    if model_space and _is_rigid_node(nid, nd):
        return _rigid_world(nodes, nid, f, memo)
    if model_space and not nd["rot"]:
        # ★ 30.10.2026: узел БЕЗ треков (жёсткий меш на анимированной кости, Dummy-родитель) стоит в своей
        #   bind-локали с трансляцией: W = L·W_родителя (сверено: |L·W_родителя − W| ≤ 0.0002 на всех узлах
        #   многомешевых .jem). Ветка ниже даёт ему единичный поворот и нулевой сдвиг — для одномешевых
        #   .jem (кости палитры всегда с треками) поведение не меняем: она включается только у model_mesh().
        #   Жёсткие узлы (не кость, не скин) сюда уже не доходят — их считает _rigid_world (формула эталона).
        B = nd.get("bind") or [[1,0,0,0],[0,1,0,0],[0,0,1,0],[0,0,0,1]]
        W = [r[:] for r in B] if nd["parent"] < 0 else _mm(B, _node_world(nodes, nd["parent"], f, memo, model_space))
        memo[(nid, f)] = W
        return W
    # ★ ЛОКАЛЬ = BIND-поворот ноды × ДЕЛЬТА трека (родительское пр-во), трансляция = pos-трек.
    #   Раньше bind игнорировался (локаль = только дельта) ⇒ ось взмаха бралась в чужом базисе:
    #   крылья качались НЕСИММЕТРИЧНО (одно вперёд, другое назад) вместо синхронного взмаха.
    # scale-трек ДУБЛИРУЕТ масштаб, уже вшитый в строки bind (f_wing_big нода 2: трек 2.0 и длины
    #   строк bind 2.0) ⇒ берём ОТНОСИТЕЛЬНЫЙ масштаб scl(f)/scl(0), иначе двойное умножение.
    s0 = _key_at(nd["scl"], 0, 3) or [1, 1, 1]
    sf = _key_at(nd["scl"], f, 3) or [1, 1, 1]
    srel = [(sf[k] / s0[k]) if abs(s0[k]) > 1e-6 else 1.0 for k in range(3)]
    Rd = _eng_row(_key_at(nd["rot"], f, 4), [0.0, 0.0, 0.0], srel)
    B  = nd.get("bind") or [[1,0,0,0],[0,1,0,0],[0,0,1,0],[0,0,0,1]]
    Br = [B[0][:], B[1][:], B[2][:], [0.0, 0.0, 0.0, 1.0]]      # только поворот bind (без трансляции)
    L  = _mm(Br, Rd)
    p  = _key_at(nd["pos"], f, 3)
    L  = [L[0][:], L[1][:], L[2][:], [p[0], p[1], p[2], 1.0]]   # трансляция = pos-трек (пивот в родителе)
    W = L if nd["parent"] < 0 else _mm(L, _node_world(nodes, nd["parent"], f, memo, model_space))
    memo[(nid, f)] = W
    return W

def skin_frames(mesh):
    """[(frameIdx, paletteIdx) → матрица 4×4 в STORED-UE] + число кадров. Пусто, если скина/треков нет."""
    nodes = mesh.get("nodes") or {}; pal = mesh.get("palette") or []
    if not nodes or not pal: return [], 0
    model_space = bool(mesh.get("model_space"))   # меш из model_mesh(): узел без треков = его bind-локаль целиком
    # ★ 30.10.2026: у модели (model_mesh) кадров = диапазон ВРЕМЁН ключей всех треков (редкие ключи grened01 [0,100],
    #   трек только масштаба flammio-a_50); у прежнего одиночного меша — как было (длина трека поворота).
    nf = _track_frames(nodes) if model_space else max((len(nd["rot"]) for nd in nodes.values()), default=0)
    if nf < 2: return [], 0
    scale = mesh.get("scale", 1.0) or 1.0
    def world(nid, f, memo):
        return _node_world(nodes, nid, f, memo, model_space)
    memo = {}
    inv0 = {pi: _inv(world(nid, 0, memo)) for pi, nid in enumerate(pal)}
    # C = своп X↔Y (file→stored-UE, см. заголовок): S_ue = C·S_file·C, трансляция ×scale меша
    out = []
    for f in range(nf):
        for pi, nid in enumerate(pal):
            S = _mm(inv0[pi], world(nid, f, memo))
            R = [[S[1][1], S[1][0], S[1][2]],
                 [S[0][1], S[0][0], S[0][2]],
                 [S[2][1], S[2][0], S[2][2]]]                     # своп строк и столбцов X↔Y
            T = (S[3][1]*scale, S[3][0]*scale, S[3][2]*scale)
            out.append((R, T))
    return out, nf


# ── Многомешевый .jem как ОДНА модель (30.10.2026) ────────────────────────────────────────────
# Эталон рисует ВСЕ узлы модели, не только первый меш: CMesh_RenderNode@0x125C4C0 (режим 2 = эффекты)
#   обходит узлы, которых нет в палитре костей (список строит CEffectMesh_ctor@0x125B150). Вершины каждого
#   меша лежат в ЛОКАЛИ СВОЕГО УЗЛА:
#     · жёсткий меш (байт записи +0x70 = 0) — CEffectMesh_DrawLeaf@0x129B5D0 перед отрисовкой делает
#       MultiplyTransform(D3DTS_WORLD, матрица узла), а треки узла (A5/A6/A7) её двигают
#       (animTracks_evaluateAtTime@0x125C120): камни firqukb01 кольцом R≈200 поднимаются из z −417,
#       комья лопаты c-shovel02 летят дугой, туфля 206 кружит на кости;
#     · скинованный меш (байт ≠ 0) — та же локаль узла меша, затем палитра костей: у m_ball центр мяча
#       + сдвиг узла меша (−20.5 / 1.8 / 23.2) = позиция его кости (средняя невязка 15.2 → 0.0;
#       dragon 386 → 19, mage_stone 196 → 36); у eff_f_wing_big_00 матрица второго узла — ЗЕРКАЛО
#       (det −1): это левое крыло из правого.
#   Прежний путь брал meshes[0] и из матрицы узла только элемент [0][0] (`scale`) — поворот, сдвиг и
#   анимация узла терялись. Здесь: вершины = raw·W_узла(кадр 0) в пространстве модели; жёсткий узел с
#   анимацией (свой трек или трек предка) становится «костью» расширенной палитры (вес 1), и общий
#   скин-проход skin_frames() двигает его матрицей inv(W(0))·W(f) — ровно MultiplyTransform эталона.
#   Узел с det(W) < 0 зеркалит — у его треугольников меняется порядок обхода.
def _xf_point(p, W):
    return [p[0]*W[0][k] + p[1]*W[1][k] + p[2]*W[2][k] + W[3][k] for k in range(3)]

def _normal_matrix(W):
    """Матрица нормалей (row-vector): транспонированная обратная к 3×3 части W — верна и при неравном масштабе."""
    A = [W[0][:3] + [0.0], W[1][:3] + [0.0], W[2][:3] + [0.0], [0.0, 0.0, 0.0, 1.0]]
    I = _inv(A)
    return [[I[c][r] for c in range(3)] for r in range(3)]

def _det3(W):
    return (W[0][0]*(W[1][1]*W[2][2] - W[1][2]*W[2][1]) - W[0][1]*(W[1][0]*W[2][2] - W[1][2]*W[2][0])
            + W[0][2]*(W[1][0]*W[2][1] - W[1][1]*W[2][0]))

def model_mesh(meshes):
    """Все меши .jem → один меш в пространстве модели (поля как у parse_jem + model_space=True)."""
    if not meshes: return None
    nodes = meshes[0].get("nodes") or {}
    palette = list(meshes[0].get("palette") or [])
    memo0 = {}
    def node_w0(nid):
        """W узла на кадре 0 (bind-поза) — тот же расчёт, что у skin_frames."""
        return _node_world(nodes, nid, 0, memo0, True)
    def animated(nid):
        seen = set()
        while nid in nodes and nid not in seen:
            seen.add(nid)
            # ★ 30.10.2026: любой трек с ≥ 2 ключами (у flammio-a_50 узел анимирован ТОЛЬКО масштабом)
            if any(len(nodes[nid][k]) >= 2 for k in ("rot", "pos", "scl")): return True
            nid = nodes[nid]["parent"]
        return False
    verts, norms, uvs, vbone = [], [], [], []
    for m in meshes:
        s = m.get("scale", 1.0) or 1.0
        W = node_w0(m["node"])
        NM = _normal_matrix(W)
        mirror = _det3(W) < 0.0
        if m.get("skinned"):
            vb = list(m.get("vbone") or [])
        else:
            if animated(m["node"]):
                if m["node"] not in palette: palette.append(m["node"])
                bi = palette.index(m["node"])
            else:
                bi = -1
            vb = [bi] * len(m["verts"])
        mv, mn = [], []
        for v, nrm in zip(m["verts"], m["normals"]):
            # stored-UE (y·s, x·s, z·s) → файл (x, y, z) без «масштаба» [0][0] → ·W → обратно в stored-UE
            q = _xf_point((v[1]/s, v[0]/s, v[2]/s), W)
            mv.append((q[1], q[0], q[2]))
            f = (nrm[1], nrm[0], nrm[2])
            g = [f[0]*NM[0][k] + f[1]*NM[1][k] + f[2]*NM[2][k] for k in range(3)]
            ln = (g[0]*g[0] + g[1]*g[1] + g[2]*g[2]) ** 0.5 or 1.0
            mn.append((g[1]/ln, g[0]/ln, g[2]/ln))
        mu = list(m["uvs"])
        if len(vb) != len(mv): vb = [-1] * len(mv)
        if mirror:
            for t in range(0, len(mv) - 2, 3):
                for arr in (mv, mn, mu, vb):
                    arr[t+1], arr[t+2] = arr[t+2], arr[t+1]
        verts += mv; norms += mn; uvs += mu; vbone += vb
    return {"verts": verts, "normals": norms, "uvs": uvs, "tex": meshes[0]["tex"], "scale": 1.0,
            "tris": sum(m["tris"] for m in meshes), "vbone": vbone, "palette": palette, "nodes": nodes,
            "node": meshes[0]["node"], "skinned": any(m.get("skinned") for m in meshes),
            "model_space": True, "mesh_count": len(meshes)}

_zip = None
def load_jem(name):
    global _zip
    if _zip is None: _zip = zipfile.ZipFile(BNZ)
    low = {os.path.basename(k).lower(): k for k in _zip.namelist()}
    base = _base(name) + ".jem"
    real = low.get(base)
    return parse_jem(_zip.read(real)) if real else None

if __name__ == "__main__":
    import sys
    for nm in (sys.argv[1:] or ["eff_alpenjewels_a_00"]):
        ms = load_jem(nm)
        if ms is None: print("%s: НЕТ .jem" % nm); continue
        print("%s: мешей=%d" % (nm, len(ms)))
        for m in ms:
            bx=[v[0] for v in m["verts"]]; by=[v[1] for v in m["verts"]]; bz=[v[2] for v in m["verts"]]
            print("  tris=%d tex=%s scale=%.2f bbox=(%.1f..%.1f, %.1f..%.1f, %.1f..%.1f)"%(
                m["tris"], m["tex"], m["scale"], min(bx),max(bx),min(by),max(by),min(bz),max(bz)))
