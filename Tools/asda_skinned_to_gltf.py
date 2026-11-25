# Copyright (c) 2026 @sitxovski (Discord)
# asda_skinned_to_gltf.py — ФАЗА 2/3: .jbm (геометрия+скин) + .jbb (скелет+анимация) → skinned glTF (.glb).
# Спеки: Reports/jbm_skinned_layout.md (Pass1 геом, Pass2 скин), jbb_motion_format.md (.jbb TRS-кейфреймы).
# Координаты LH Y-up → RH Y-up: сопряжение локальных матриц костей Z·L·Z (Z=diag(1,1,-1,1)) + негейт z вершин + реверс winding.
# Bind-pose: key0 выбранного клипа (вершины .jbm в модельном пространстве bind). inverse-bind = inv(restWorld).
# Запуск: python asda_skinned_to_gltf.py <model.jbm> <clip.jbb> <out.glb>
import sys, os, struct, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import pygltflib as g
import asda_jbm as JBM   # ★ единый разбор .jbm (шапка + Pass1)
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass


# ---------- математика TRS/кватернионы ----------
def quat_to_mat(q):
    x, y, z, w = q
    n = x*x+y*y+z*z+w*w
    if n < 1e-12: return np.eye(3)
    s = 2.0/n
    return np.array([
        [1-s*(y*y+z*z), s*(x*y-z*w),   s*(x*z+y*w)],
        [s*(x*y+z*w),   1-s*(x*x+z*z), s*(y*z-x*w)],
        [s*(x*z-y*w),   s*(y*z+x*w),   1-s*(x*x+y*y)]])

def mat_to_quat(R):
    t = np.trace(R)
    if t > 0:
        s = np.sqrt(t+1.0)*2; w=0.25*s; x=(R[2,1]-R[1,2])/s; y=(R[0,2]-R[2,0])/s; z=(R[1,0]-R[0,1])/s
    elif R[0,0]>R[1,1] and R[0,0]>R[2,2]:
        s=np.sqrt(1+R[0,0]-R[1,1]-R[2,2])*2; w=(R[2,1]-R[1,2])/s; x=0.25*s; y=(R[0,1]+R[1,0])/s; z=(R[0,2]+R[2,0])/s
    elif R[1,1]>R[2,2]:
        s=np.sqrt(1+R[1,1]-R[0,0]-R[2,2])*2; w=(R[0,2]-R[2,0])/s; x=(R[0,1]+R[1,0])/s; y=0.25*s; z=(R[1,2]+R[2,1])/s
    else:
        s=np.sqrt(1+R[2,2]-R[0,0]-R[1,1])*2; w=(R[1,0]-R[0,1])/s; x=(R[0,2]+R[2,0])/s; y=(R[1,2]+R[2,1])/s; z=0.25*s
    q=np.array([x,y,z,w]); return q/ (np.linalg.norm(q)+1e-12)

def compose(t, q, s):
    M=np.eye(4); M[:3,:3]=quat_to_mat(q)*np.array(s); M[:3,3]=t; return M

def decompose(M):
    t=M[:3,3].copy(); R=M[:3,:3].copy()
    s=np.array([np.linalg.norm(R[:,0]),np.linalg.norm(R[:,1]),np.linalg.norm(R[:,2])])
    s[s<1e-12]=1e-12; Rn=R/s
    if np.linalg.det(Rn)<0: Rn[:,0]*=-1; s[0]*=-1
    return t, mat_to_quat(Rn), s

# ---------- .jbm: геометрия (Pass1) + скин (Pass2) ----------
def parse_jbm(b):
    # ★ Шапка + Pass1-геометрия — ЕДИНЫЙ модуль asda_jbm.py (общий с asda_jbm_to_gltf /
    #   asda_test_export_one). guard=False — строгое чтение, как было в локальной копии.
    #   Пер-меш материал (matIdx@0x1f4) сохранён. Pass2 (скин) ниже — без изменений.
    m = JBM.parse_geometry(b, guard=False)
    if m is None: return None
    tex0 = m["tex"]; meshes = m["meshes"]; geo = m["geoEnd"]
    # Pass2 (скин) — по meshType (приоритет старшего бита)
    p2=geo
    for me in meshes:
        V,M,B,mt=me['V'],me['M'],me['B'],me['mtype']
        o=p2; j0=np.zeros(V,np.int64); j1=np.zeros(V,np.int64); w0=np.ones(V,np.float32)
        bi0=np.zeros(V,np.int64); bi1=np.zeros(V,np.int64)   # СЫРЫЕ индексы палитры (0..M-1) per vertex
        me['names_layout']=[]                                # имена костей ПО LAYOUT (см. ниже)
        sel = 0x800 if mt&0x800 else 0x200 if mt&0x200 else 0x100 if mt&0x100 else 0x80 if mt&0x80 else 0
        # ★ ДЕТЕРМИНИРОВАННОЕ чтение таблицы имён костей: для 0x80/0x100 скин-блок НАЧИНАЕТСЯ с M записей
        # по 0x20 (имена костей палитры), затем идут веса/индексы. Так надёжнее regex-scan (работает и для
        # ЧИСЛОВЫХ имён cremmy `0/_9/8_a1`, и для `Bip01 …` — байт-в-байт совпало с regex на мобах).
        if sel in (0x80,0x100) and M>0:
            nm=[]
            for i in range(M):
                s=b[o+i*0x20:o+i*0x20+0x20]; z=s.find(0)
                nm.append(s[:z if z>=0 else 0x20].decode("latin1","ignore"))
            me['names_layout']=nm
        if sel==0x80:
            o+=0x20*M; o+=0xC*V; o+=0xC*V
            w0=np.frombuffer(b,np.float32,V,o).copy(); o+=4*V
            bi0=np.frombuffer(b,np.uint32,V,o).astype(np.int64); o+=4*V
            bi1=np.frombuffer(b,np.uint32,V,o).astype(np.int64); o+=4*V
            pal=np.frombuffer(b,np.int32,M,o); o+=4*M; o+=0x40*B
            j0=pal[np.clip(bi0,0,M-1)]; j1=pal[np.clip(bi1,0,M-1)]
        elif sel==0x100:
            o+=0x20*M; o+=0xC*V
            bi0=np.frombuffer(b,np.uint32,V,o).astype(np.int64); o+=4*V; bi1=bi0.copy()
            pal=np.frombuffer(b,np.int32,M,o); o+=4*M; o+=0x40*B
            j0=pal[np.clip(bi0,0,M-1)]; j1=j0.copy(); w0=np.ones(V,np.float32)
        elif sel==0x200:
            o+=0xC*V; pal=np.frombuffer(b,np.int32,3,o); o+=12; o+=0x40*3
            j0[:]=pal[0]; j1[:]=pal[0]
        elif sel==0x800:
            pal=np.frombuffer(b,np.int32,B,o); o+=4*B; o+=0x40*B
            j0[:]=pal[0] if B>0 else 0; j1[:]=j0
        w0=np.clip(np.nan_to_num(w0,nan=1.0),0.0,1.0)
        me['bi0']=np.clip(bi0,0,max(M-1,0)); me['bi1']=np.clip(bi1,0,max(M-1,0))
        me['j0']=np.clip(j0,0,None); me['j1']=np.clip(j1,0,None); me['w0']=w0
        p2=o
    # СКИН-ЛИНКОВКА ПО ИМЕНИ: в .jbm есть таблица имён костей на меш (M штук) — индекс палитры в неё.
    # Приоритет — имена ПО LAYOUT (детерминированно, из Pass2); regex-scan оставлен как fallback-страховка.
    me_names=_extract_mesh_bone_names(b,[m['M'] for m in meshes])
    for i,me in enumerate(meshes):
        me['names']=me.get('names_layout') or me_names.get(i,[])
    return dict(tex=tex0, meshes=meshes)

def _extract_mesh_bone_names(b, Ms):
    # имена костей — записи по 0x20 байт, группами (размер группы == matCount M меша), в порядке мешей.
    # Надёжно: seed по regex, затем расширяем группу по шагу 0x20 пока блок «похож на имя» (ловит ВСЕ имена,
    # включая нестандартные Box/Nub/Dummy/произвольные, которые regex бы пропустил → группы съезжали).
    def is_name(o):
        if o<0 or o+0x20>len(b): return False
        c0=b[o]
        if not (65<=c0<=90 or 97<=c0<=122): return False
        s=b[o:o+0x20]; z=s.find(0)
        return z>0 and all(32<=ch<127 for ch in s[:z])
    seeds=sorted(set(m.start() for m in
        re.finditer(rb'(Bip01 [A-Za-z0-9 ]+|Bone\d+|IK Chain\d+|Bip01)\x00', b)))
    runs={}; seen=set()
    for s in seeds:
        if s in seen: continue
        st=s
        while is_name(st-0x20): st-=0x20
        en=st; names=[]
        while is_name(en):
            names.append(b[en:en+0x20].split(b"\0")[0].decode("latin1")); seen.add(en); en+=0x20
        runs[st]=names
    runlist=[runs[k] for k in sorted(runs)]
    out={}; gi=0
    for mi,M in enumerate(Ms):
        if M>0 and gi<len(runlist) and len(runlist[gi])==M:
            out[mi]=runlist[gi]; gi+=1
    return out

def resolve_skin_by_name(jbm, jbb):
    # j0/j1 = .jbb-индекс ПО ИМЕНИ (сырой индекс палитры bi0/bi1 → имя кости .jbm → кость .jbb).
    norm=lambda s: re.sub(r'\s+',' ',s).strip().lower()
    jmap={}
    for i,bn in enumerate(jbb['bones']): jmap.setdefault(norm(bn['name']),i)
    fixed=0
    for me in jbm['meshes']:
        nm=me.get('names') or []
        if nm:   # СКИННУТЫЙ: палитра имён, индекс палитры → имя → кость
            idx=np.array([jmap.get(norm(n),0) for n in nm],np.int64)
            bi0=np.clip(me['bi0'],0,len(idx)-1); bi1=np.clip(me['bi1'],0,len(idx)-1)
            me['j0']=idx[bi0]; me['j1']=idx[bi1]; fixed+=1
        else:    # РИГИДНЫЙ узел (0x200, без палитры): кость = .jbb-кость с именем = nodeName меша
            node=me.get('node','')
            bi=jmap.get(norm(node))
            if bi is not None:
                me['j0']=np.full(len(me['pos']),bi,np.int64); me['j1']=me['j0'].copy(); fixed+=1
    return fixed

# ---------- .jbb: скелет + кейфреймы ----------
def parse_jbb(b):
    if struct.unpack_from("<I",b,0)[0]!=0x20050728: return None
    U=lambda o: struct.unpack_from("<I",b,o)[0]; F=lambda o: struct.unpack_from("<f",b,o)[0]
    # смещения АБСОЛЮТНЫЕ от начала файла (jbb_motion_format.md): boneCount@0x24, fps@0x34, flags@0x40, blob@0x44
    B=U(0x24); fps=F(0x34) or 30.0; flags=U(0x40); blob=0x44
    parents=np.frombuffer(b,np.int32,B,blob).copy(); o=blob+4*B
    bones=[]
    for i in range(B):
        bh=o+i*0x94
        cv=F(bh+0x90)                                  # CONVERT_MATRIX_VALUE (uniform scale 3×3 ротблока)
        bones.append(dict(name=b[bh:bh+0x20].split(b"\0")[0].decode("latin1","ignore"),
                          parent=int(parents[i]) if 0<=parents[i]<B and parents[i]!=i else -1,
                          keyCount=U(bh+0x20),
                          conv=cv if (abs(cv)>1e-6 and cv==cv) else 1.0))
    kf=o+B*0x94
    for bn in bones:
        kc=bn['keyCount']; arr=np.frombuffer(b,np.float32,kc*11,kf).reshape(kc,11) if kc else np.zeros((0,11),np.float32)
        bn['t']=arr[:,0].copy()/1000.0           # ms→сек
        bn['pos']=arr[:,1:4].copy(); bn['quat']=arr[:,4:8].copy(); bn['scale']=arr[:,8:11].copy()
        kf+=kc*0x2c
    dur=max((bn['t'][-1] if bn['keyCount'] else 0.0) for bn in bones) if bones else 0.0
    # ★★★ 2026-09-11: бит 0x2 flags СНЯТ → кейфреймы ЛОКАЛЬНЫЕ, переводим их в мировые по формуле оригинала
    #   (подробно — в шапке _compose_local_keys). После перевода все потребители (jointWorld, build,
    #   build_fixed) работают как с «абсолютным» клипом. Откат: ASDA_JBB_HIER=0.
    composed = False
    if JBB_HIER and not (flags & 2):
        _compose_local_keys(bones); composed = True
    return dict(B=B,fps=fps,flags=flags,bones=bones,duration=dur,hier_composed=composed)

# Флаг отката ветки «локальные кейфреймы» (бит 0x2 flags снят): ASDA_JBB_HIER=0 → всё читается как абсолютное.
JBB_HIER = os.environ.get("ASDA_JBB_HIER", "1") != "0"

def _qmul(a, b):
    # Произведение Гамильтона a·b для массивов кватернионов (x,y,z,w) по последней оси.
    ax,ay,az,aw = a[...,0],a[...,1],a[...,2],a[...,3]
    bx,by,bz,bw = b[...,0],b[...,1],b[...,2],b[...,3]
    return np.stack([aw*bx+ax*bw+ay*bz-az*by,
                     aw*by-ax*bz+ay*bw+az*bx,
                     aw*bz+ax*by-ay*bx+az*bw,
                     aw*bw-ax*bx-ay*by-az*bz], -1)

def _compose_local_keys(bones):
    # Ветка оригинала при СНЯТОМ бите 0x2 поля flags (.jbb @0x40):
    #   диспетчеры @0x9A2180 / @0x99EDC0 (`and ecx,2; je`) → animSkeletonAdvanceTime @0x112AC60 /
    #   animSkeletonSetFrame @0x112AA00: цикл по костям i=0..B-1, локальный SRT кадра, затем при
    #   parents[i]!=-1 nodeApplyTransformAndScale @0x112CC80:
    #     T_i = [T_i,1]·M_parent   (row-vector, ПОЛНАЯ 4×4 родителя: его conv/масштаб/трансляция)
    #     q_i = q_i ⊗ q_parent     (D3DXQuaternionMultiply(q_parent, q_i) = Гамильтон дочерний·родительский)
    #     S_i = S_i · S_parent     (покомпонентно)
    #   Результат пишется НА МЕСТО, поэтому дети видят уже мировые значения родителя.
    #   Матрица кости — тот же buildWorldMatrixFromSRT @0x112CAA0 (= eng_row_matrix), поэтому,
    #   записав в кейфрейм мировые (T,q,S), получаем РОВНО M_i оригинала через jointWorld.
    #   conv родителя входит в трансляцию ребёнка (через M_parent), но не во вращение — как в оригинале.
    #   Кадр родителя берётся с тем же индексом кадра (оригинал семплирует всех по одному времени),
    #   короткая дорожка держит последний кейфрейм. Кость без кейфреймов не трогается.
    #   Реверс: Reports/mob_ground/JBB_FLAGS_RE.md §B–C.
    B = len(bones); done = [False]*B
    def go(i, depth=0):
        if done[i] or depth > B: return
        bn = bones[i]; p = bn['parent']
        if 0 <= p < B and p != i:
            go(p, depth+1)
            P = bones[p]; kc = bn['keyCount']; kp = P['keyCount']
            if kc and kp:
                K = max(kc, kp); ki = np.minimum(np.arange(K), kc-1); kpi = np.minimum(np.arange(K), kp-1)
                Mp = np.stack([eng_row_matrix(P['quat'][k], P['pos'][k], P['scale'][k], P['conv']) for k in kpi])
                lt = np.c_[bn['pos'][ki].astype(np.float64), np.ones(K)]
                T = np.einsum('kr,krc->kc', lt, Mp)[:, :3]
                q = _qmul(bn['quat'][ki].astype(np.float64), P['quat'][kpi].astype(np.float64))
                s = bn['scale'][ki].astype(np.float64) * P['scale'][kpi].astype(np.float64)
                bn['t'] = (bn['t'][ki] if kc >= kp else P['t'][kpi]).copy()
                bn['pos'] = T.astype(np.float32); bn['quat'] = q.astype(np.float32); bn['scale'] = s.astype(np.float32)
                bn['keyCount'] = K
        done[i] = True
    for i in range(B): go(i)

# ---------- сборка glTF (skeletal_anim_math.md: ПЛОСКИЙ скелет, абсолютные трансляции, pivot-инвербайнд) ----------
ZH = np.diag([1.0, 1.0, -1.0, 1.0])   # LH→RH сопряжение (z-зеркало)

def auto_bind_frames(jbm, jbb, ABS_THRESH=15.0, REL=0.5):
    # ПЕР-КОСТЬ выбор bind-КАДРА для RIGID-мешей (0x200), чьи raw-верты авторены НЕ в frame0.
    # Проблема: экспортёр по умолчанию берёт bind=frame0 (invBind=inv(jointWorld[0])). Для моделей,
    #   где сырые вершины меша нарисованы художником в ПРОИЗВОЛЬНОЙ позе клипа (напр. шкатулка
    #   cre_creation01 — крышка нарисована ОТКРЫТОЙ), frame0-bind делает idle=авторская поза (открыто),
    #   а нужно idle=frame0 (закрыто) + проигрывание вперёд = открывание.
    # Критерий (rigid, тождество raw = W_bind · vlocal): истинный bind-кадр — тот, где локальные
    #   координаты вершин центрированы у самой кости (|centroid(inv(W_k)·raw)| минимален).
    #   Меняем bind ТОЛЬКО если найден кадр значимо лучше frame0 (nmin<ABS_THRESH И nmin<REL·n[frame0]) —
    #   иначе (скиннутые палитрой меши, напр. cre_creback: центроид большой во всех кадрах) bind=frame0.
    # Возвращает {j0: bind_frame}. Для не-переопределённых костей вызывающий использует frame0.
    bones = jbb['bones']
    bone_raw = {}
    for me in jbm['meshes']:
        if me.get('names'):           # скиннутый палитрой (>1 кость/вершину) — bind=frame0, не трогаем
            continue
        j0 = int(me['j0'][0])
        raw = me['pos'].copy(); raw[:, 2] *= -1
        bone_raw.setdefault(j0, []).append(raw)
    binds = {}
    for j0, lst in bone_raw.items():
        if not (0 <= j0 < len(bones)): continue
        bn = bones[j0]; kc = bn['keyCount']
        if kc < 2: continue
        raw = np.concatenate(lst, 0); rawh = np.c_[raw, np.ones(len(raw))]
        norms = [np.linalg.norm((np.linalg.inv(jointWorld(bn, k)) @ rawh.T).T[:, :3].mean(0)) for k in range(kc)]
        k0 = norms[0]; kb = int(np.argmin(norms)); nmin = norms[kb]
        if kb != 0 and nmin < ABS_THRESH and nmin < REL * k0:
            binds[j0] = kb
    return binds

def eng_row_matrix(q, pos, scale, conv):
    # ДОСЛОВНО mathQuaternionToMatrix4x4 + buildWorldMatrixFromSRT движка (row-major, v·M):
    # rotation с ДВИЖКОВЫМИ знаками, conv·3×3, diag(scale)·R (масштаб строк), трансляция в row3.
    x,y,z,w = q
    M = np.eye(4)
    M[0,0]=1-2*(y*y+z*z); M[0,1]=2*(x*y-w*z);   M[0,2]=2*(x*z+w*y)
    M[1,0]=2*(x*y+w*z);   M[1,1]=1-2*(x*x+z*z); M[1,2]=2*(y*z-w*x)
    M[2,0]=2*(x*z-w*y);   M[2,1]=2*(y*z+w*x);   M[2,2]=1-2*(x*x+y*y)
    M[:3,:3]*=float(conv)
    for r in range(3): M[r,:3]*=float(scale[r])
    M[3,0],M[3,1],M[3,2]=float(pos[0]),float(pos[1]),float(pos[2])
    return M

def jointWorld(bn, k):
    # FLAT абсолют кости в glTF (M·v, column-vector) = ZH · M_eng^T · ZH.
    # ДОКАЗАНО численно == тюнер UE (EngineExact), Δ=0 на всех вершинах (gltf_vs_tuner/v8_vs_tuner).
    if not bn['keyCount']: return np.eye(4)
    kk=min(k,bn['keyCount']-1)
    return ZH @ eng_row_matrix(bn['quat'][kk],bn['pos'][kk],bn['scale'][kk],bn['conv']).T @ ZH

def build(jbm, clips, out, bind_frames=None, blend_mat_suffix=None):
    # clips = [(имя_клипа, jbb), ...]. clips[0] — РЕФЕРЕНС: скелет, bind-поза, скин-линковка.
    # Меш+скелет строятся один раз; каждая анимация-клип добавляется отдельной g.Animation.
    # bind_frames: опц. {j0: kb} — ПЕР-КОСТЬ bind-КАДР (rigid-меш авторен не во frame0). invBind
    #   считается от этого кадра; rest-поза УЗЛА остаётся frame0 => idle(без анимации)=frame0-поза
    #   (для шкатулки закрыто), проигрывание клипа вперёд = раскрытие. Для не указанных костей — frame0.
    if isinstance(clips, dict): clips=[("clip",clips)]   # совместимость: одиночный jbb
    jbb=clips[0][1]; B=jbb['B']; bones=jbb['bones']
    bind_frames = bind_frames or {}
    # FLAT абсолютные world-матрицы кости по кадрам (== движок animSkeletonAdvanceTimeMtx); inverse-bind = inv(bind-кадра).
    W=[[jointWorld(bones[i],k) for k in range(max(bones[i]['keyCount'],1))] for i in range(B)]
    invBind=[np.linalg.inv(W[i][min(bind_frames.get(i,0), len(W[i])-1)]) for i in range(B)]

    bins=bytearray(); accessors=[]; views=[]
    def view(data,target=None):
        off=len(bins); bins.extend(data)
        while len(bins)%4: bins.append(0)
        views.append(g.BufferView(buffer=0,byteOffset=off,byteLength=len(data),target=target)); return len(views)-1
    def acc(data,ctype,count,atype,mn=None,mx=None,target=None):
        v=view(data,target); a=g.Accessor(bufferView=v,componentType=ctype,count=count,type=atype)
        if mn is not None: a.min=mn; a.max=mx
        accessors.append(a); return len(accessors)-1

    # ПЛОСКИЙ скелет: кости-сиблинги под Armature; local = jointWorld(0) (flat → local=world). ВСЕ кости, без обрезки.
    nodes=[]; joint_node=[]
    for i in range(B):
        t,q,s=decompose(W[i][0])
        nodes.append(g.Node(name=bones[i]['name'] or ("bone%d"%i),
                    translation=t.tolist(),rotation=[float(x) for x in q],scale=s.tolist()))
        joint_node.append(len(nodes)-1)
    nodes.append(g.Node(name="Armature", children=list(joint_node))); root_idx=len(nodes)-1

    # меш-примитивы; материалы УНИКАЛЬНЫЕ по имени текстуры меша (matIdx@0x1f4), не один tex0 на всех
    # ★ blend_mat_suffix (2026-08-18): подсетки, прошедшие `asda_jbm.is_alpha_blend` (рендер-бит
    #   mr+0x004 & 0x1|0x2 И техника tNoSkin), получают ОТДЕЛЬНЫЙ ключ материала. Здесь это
    #   критично: материалы ДЕДУПЛИЦИРУЮТСЯ по имени текстуры, и без метки полупрозрачная подсетка
    #   делит один слот с непрозрачным корпусом (та же текстура) — прозрачной ей стать нечем.
    #   Пример из данных: шкатулка `cre_creation01-a_00`, узел `c-04`, текстура `cre_creation01-a_03`
    #   (альфа РОВНО 51 = 0.2 у 100 % пикселей) — на masked-мастере с clip 0.5 она НЕ ВИДНА ВООБЩЕ.
    prims=[]; mats=[]; mat_by_tex={}
    def mat_for(texname, is_blend=False):
        key=os.path.splitext(texname or jbm['tex'])[0] or "mat"
        if blend_mat_suffix and is_blend: key += blend_mat_suffix
        if key not in mat_by_tex:
            mats.append(g.Material(name=key, alphaMode="MASK", alphaCutoff=0.5, doubleSided=True,
                        pbrMetallicRoughness=g.PbrMetallicRoughness(metallicFactor=0.0,roughnessFactor=1.0)))
            mat_by_tex[key]=len(mats)-1
        return mat_by_tex[key]
    for me in jbm['meshes']:
        used,inv=np.unique(me['idx'],return_inverse=True)
        P=me['pos'][used].copy(); P[:,2]*=-1
        N=me['nrm'][used].copy(); N[:,2]*=-1
        UVv=me['uv'][used].copy()
        J4=np.zeros((len(P),4),np.uint16); J4[:,0]=np.clip(me['j0'][used],0,B-1); J4[:,1]=np.clip(me['j1'][used],0,B-1)
        W4=np.zeros((len(P),4),np.float32); W4[:,0]=me['w0'][used]; W4[:,1]=1.0-me['w0'][used]
        I=inv.astype(np.uint32).reshape(-1,3)[:,::-1].reshape(-1)
        aP=acc(P.astype("<f4").tobytes(),g.FLOAT,len(P),g.VEC3,P.min(0).tolist(),P.max(0).tolist(),g.ARRAY_BUFFER)
        aN=acc(N.astype("<f4").tobytes(),g.FLOAT,len(N),g.VEC3,target=g.ARRAY_BUFFER)
        aU=acc(UVv.astype("<f4").tobytes(),g.FLOAT,len(UVv),g.VEC2,target=g.ARRAY_BUFFER)
        aJ=acc(J4.tobytes(),g.UNSIGNED_SHORT,len(P),g.VEC4,target=g.ARRAY_BUFFER)
        aW=acc(W4.astype("<f4").tobytes(),g.FLOAT,len(P),g.VEC4,target=g.ARRAY_BUFFER)
        aI=acc(I.astype("<u4").tobytes(),g.UNSIGNED_INT,len(I),g.SCALAR,target=g.ELEMENT_ARRAY_BUFFER)
        prims.append(g.Primitive(attributes=g.Attributes(POSITION=aP,NORMAL=aN,TEXCOORD_0=aU,JOINTS_0=aJ,WEIGHTS_0=aW),
                                 indices=aI,material=mat_for(me.get('tex'), JBM.is_alpha_blend(me))))

    ibm=np.stack([invBind[i].T for i in range(B)]).astype("<f4")   # column-major → транспон
    aIBM=acc(ibm.tobytes(),g.FLOAT,B,g.MAT4)
    skin=g.Skin(inverseBindMatrices=aIBM, joints=joint_node, skeleton=root_idx)
    nodes.append(g.Node(name="mesh", mesh=0, skin=0)); mesh_idx=len(nodes)-1

    # анимации: ОДНА g.Animation на клип. Кости клипа мапим к референс-скелету ПО ИМЕНИ (порядок может отличаться).
    nrm=lambda s:(s or "").strip().lower()
    anims=[]
    for cname, cjbb in clips:
        cbones=cjbb['bones']; cidx={nrm(cbones[j]['name']):j for j in range(len(cbones))}
        samplers=[]; channels=[]
        for i in range(B):
            ci = i if (i<len(cbones) and nrm(cbones[i]['name'])==nrm(bones[i]['name'])) else cidx.get(nrm(bones[i]['name']))
            if ci is None or cbones[ci]['keyCount']<1: continue
            cb=cbones[ci]; kc=cb['keyCount']
            Wc=[jointWorld(cb,k) for k in range(kc)]
            times=cb['t'].astype("<f4"); Ts=[];Qs=[];Ss=[]
            for k in range(kc):
                t,q,s=decompose(Wc[k]); Ts.append(t);Qs.append(np.asarray(q,np.float64));Ss.append(s)
            # НЕПРЕРЫВНОСТЬ ЗНАКА кватернионов (q и −q — один поворот; скачок знака → «длинный» SLERP → разлёт).
            for k in range(1,len(Qs)):
                if np.dot(Qs[k],Qs[k-1])<0: Qs[k]=-Qs[k]
            Qs=[[float(x) for x in q] for q in Qs]
            aT=acc(times.tobytes(),g.FLOAT,len(times),g.SCALAR,[float(times.min())],[float(times.max())])
            aTr=acc(np.array(Ts,"<f4").tobytes(),g.FLOAT,len(Ts),g.VEC3)
            aRo=acc(np.array(Qs,"<f4").tobytes(),g.FLOAT,len(Qs),g.VEC4)
            aSc=acc(np.array(Ss,"<f4").tobytes(),g.FLOAT,len(Ss),g.VEC3)
            for path,a in (("translation",aTr),("rotation",aRo),("scale",aSc)):
                samplers.append(g.AnimationSampler(input=aT,output=a,interpolation="LINEAR"))
                channels.append(g.AnimationChannel(sampler=len(samplers)-1,
                                target=g.AnimationChannelTarget(node=joint_node[i],path=path)))
        if channels: anims.append(g.Animation(name=cname,samplers=samplers,channels=channels))

    modelname=os.path.splitext(os.path.basename(out))[0]   # имя меша = имя модели (не "model")
    # ВАЖНО: сцену НЕ именуем — Interchange создаёт подпапку и по имени файла, И по имени сцены →
    # двойная одноимённая вложенность (NPC/<model>/<model>). Имя меша даёт имя ассету, папку не плодит.
    gltf=g.GLTF2(scene=0, scenes=[g.Scene(nodes=[root_idx, mesh_idx])],
                 nodes=nodes, meshes=[g.Mesh(primitives=prims,name=modelname)],
                 materials=mats, skins=[skin], animations=anims,
                 accessors=accessors, bufferViews=views, buffers=[g.Buffer(byteLength=len(bins))])
    gltf.set_binary_blob(bytes(bins)); gltf.save_binary(out)

def prune_bones(jbm, jbb):
    # Отрезать НЕ-скиннутые ЛИСТОВЫЕ кости (хелперы Biped: Ponytail/Box/Nub — анимированы, но меш не трогают;
    # в UE рисуются «летающими» линиями + раздувают bounds). Удаляем итеративно лист-за-листом + ремап индексов.
    B=jbb['B']; bones=jbb['bones']
    skinned=set()
    for me in jbm['meshes']:
        skinned|=set(int(x) for x in me['j0']); skinned|=set(int(x) for x in me['j1'])
    keep=[True]*B; changed=True
    while changed:
        changed=False; haschild=[False]*B
        for i in range(B):
            if keep[i] and bones[i]['parent']>=0: haschild[bones[i]['parent']]=True
        for i in range(B):
            if keep[i] and i not in skinned and not haschild[i]:
                keep[i]=False; changed=True
    newidx={};
    for i in range(B):
        if keep[i]: newidx[i]=len(newidx)
    nb=[]
    for i in range(B):
        if not keep[i]: continue
        bn=dict(bones[i]); pr=bones[i]['parent']
        bn['parent']=newidx[pr] if (pr in newidx) else -1
        nb.append(bn)
    for me in jbm['meshes']:
        me['j0']=np.array([newidx.get(int(x),0) for x in me['j0']],np.int64)
        me['j1']=np.array([newidx.get(int(x),0) for x in me['j1']],np.int64)
    removed=B-len(nb); jbb['bones']=nb; jbb['B']=len(nb)
    return removed

def main():
    jm,jb,out=sys.argv[1],sys.argv[2],sys.argv[3]
    mode=sys.argv[4] if len(sys.argv)>4 else "hier"
    strip=("strip" in sys.argv)   # ОБРЕЗКА ВЫКЛ по умолчанию (эталон НЕ обрезает кости; ломала правую сторону)
    jbm=parse_jbm(open(jm,"rb").read()); jbb=parse_jbb(open(jb,"rb").read())
    if not jbm or not jbb: print("parse fail"); return
    resolve_skin_by_name(jbm,jbb)   # скин-кости ПО ИМЕНИ (палитра) / по nodeName (ригид) — как движок
    rm=prune_bones(jbm,jbb) if strip else 0
    bf=auto_bind_frames(jbm,jbb) if ("autobind" in sys.argv) else None   # rigid: bind=авторский кадр
    build(jbm,[("clip",jbb)],out,bind_frames=bf)
    if rm: print("  отрезано не-скиннутых хелпер-костей: %d" % rm)
    tv=sum(len(m['pos']) for m in jbm['meshes'])
    print("OK -> %s | meshes=%d verts=%d bones=%d fps=%.0f dur=%.2fs" %
          (os.path.basename(out),len(jbm['meshes']),tv,jbb['B'],jbb['fps'],jbb['duration']))


def _script_main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    Z = np.diag([1.0, 1.0, -1.0, 1.0]).astype(np.float64)   # зеркало по Z (LH↔RH)
    main()


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    _script_main()
