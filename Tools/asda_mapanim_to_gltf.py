# Copyright (c) 2026 @sitxovski (Discord)
# asda_mapanim_to_gltf.py — аним-объекты карты (.mapraw ver=2) → skeletal glTF (.glb) + манифест размещений.
# Переиспользует ПРОВЕРЕННЫЙ build() из asda_skinned_to_gltf (плоский скелет, jointWorld == движок EngBoneWorld,
# Δ=0 vs UE-тюнер). Группировка: на КЛИП — один skeletal-меш (геометрия в bone-local одинакова у всех
# инстансов), размещения = список objPlace (мировые матрицы группы) на каждый инстанс.
# Запуск (системный python с numpy+pygltflib): python asda_mapanim_to_gltf.py <map.mapraw> <outDir>
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import sys, os, struct, importlib.util
import numpy as np
import pygltflib as g

def load_conv():
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "asda_skinned_to_gltf.py")
    spec = importlib.util.spec_from_file_location("skconv", p); m = importlib.util.module_from_spec(spec)
    sys.modules["skconv"] = m; spec.loader.exec_module(m); return m

# ★ Ридер .mapraw — ЕДИНЫЙ модуль asda_mapraw.py (было 5 копий class R).
#   Здесь fa() исторически возвращал tuple (в остальных копиях — list) → сохраняем через fa_t.
from asda_mapraw import R as _MapRawR

class R(_MapRawR):
    def fa(s, n): return _MapRawR.fa_t(s, n)   # tuple, как было в локальной копии


def _read_full(path):
    r = R(open(path,"rb").read())
    assert r.d[:4]==b"AMRW"; r.o=4
    ver=r.u32(); name=r.s_()
    r.u32(); r.f32(); r.f32(); r.fa(3); r.fa(3); r.fa(3)            # scene
    pc=r.u32(); ic=r.u32(); r.u16a(ic)                              # terrain idx
    for _ in range(pc):
        vtx=r.u32()
        if vtx>0: r.fa(vtx*3); r.fa(vtx*2); r.fa(vtx*2); r.o+=vtx*4
        r.u32(); r.u32(); r.f32(); r.f32(); r.s_(); r.f32(); r.s_(); r.f32()
    mc=r.u32()
    for _ in range(mc):
        r.s_(); r.s_(); v=r.u32(); r.fa(v*3); r.fa(v*3); r.fa(v*2); icx=r.u32(); r.u32a(icx)
    ni=r.u32()
    for _ in range(ni):
        r.u32(); r.fa(16); r.i32(); r.i32(); r.fa(4)   # + uvScroll
    bc=r.u32(); r.s_(); r.f32()                                     # water
    for _ in range(bc):
        wv=r.u32(); r.fa(wv*3); r.fa(wv*2); wic=r.u32(); r.u16a(wic)
    ec=r.u32()
    for _ in range(ec):                                             # effects (формат 2026-07-03)
        r.fa(3); r.s_(); r.i32(); r.i32(); r.fa(16); r.f32(); r.i32()  # pos,name,type,bone,selfMat,timeSync,attachInst
        r.fa(16); r.i32(); r.i32(); r.s_()                             # pmat,clipIdx,attached,boneName
    sc=r.u32()
    for _ in range(sc): r.fa(3); r.s_(); r.f32(); r.f32(); r.f32(); r.i32(); r.i32(); r.i32()   # sounds (+trigR/flags/attach)
    # --- АНИМАЦИИ (ver=2) ---
    skin=[]; nsk=r.u32()
    for _ in range(nsk):
        tex=r.s_(); clip=r.i32(); obj=r.fa(16); nv=r.u32()
        P=np.empty((nv,3),np.float32); N=np.empty((nv,3),np.float32); UV=np.empty((nv,2),np.float32)
        J0=np.empty(nv,np.int64); J1=np.empty(nv,np.int64); W0=np.empty(nv,np.float32)
        for vi in range(nv):
            px,py,pz,nx,ny,nz,u,v2=r.fa(8); b0=r.i32(); b1=r.i32(); w=r.f32()
            P[vi]=(px,py,pz); N[vi]=(nx,ny,nz); UV[vi]=(u,v2)
            J0[vi]=max(b0,0); J1[vi]=b1 if b1>=0 else max(b0,0); W0[vi]=w
        nidx=r.u32(); idx=np.array(r.u16a(nidx),np.uint32)
        skin.append(dict(tex=tex,clip=clip,obj=obj,pos=P,nrm=N,uv=UV,j0=J0,j1=J1,w0=W0,idx=idx))
    anim=[]; nam=r.u32()
    for _ in range(nam):
        tex=r.s_(); model=r.s_(); clip=r.i32(); bone=r.i32(); m=r.fa(16); obj=r.fa(16); nv=r.u32()
        if nv>0: P=np.array(r.fa(nv*3),np.float32).reshape(nv,3); N=np.array(r.fa(nv*3),np.float32).reshape(nv,3); UV=np.array(r.fa(nv*2),np.float32).reshape(nv,2)
        else: P=np.zeros((0,3),np.float32); N=P.copy(); UV=np.zeros((0,2),np.float32)
        nidx=r.u32(); idx=np.array(r.u16a(nidx),np.uint32)
        anim.append(dict(tex=tex,model=model,clip=clip,bone=bone,obj=obj,pos=P,nrm=N,uv=UV,idx=idx))
    clips=[]; nc=r.u32()
    for _ in range(nc):
        cname=r.s_(); dur=r.f32()/1000.0; nb=r.u32(); bones=[]   # мс→сек
        for _ in range(nb):
            bn=r.s_(); par=r.i32(); conv=r.f32(); nk=r.u32()
            t=np.empty(nk,np.float32); pos=np.empty((nk,3),np.float32); quat=np.empty((nk,4),np.float32); scl=np.empty((nk,3),np.float32)
            for ki in range(nk):
                tt=r.f32(); px,py,pz=r.fa(3); qx,qy,qz,qw=r.fa(4); sx,sy,sz=r.fa(3)
                t[ki]=tt/1000.0; pos[ki]=(px,py,pz); quat[ki]=(qx,qy,qz,qw); scl[ki]=(sx,sy,sz)  # мс→сек (как jbb)
            bones.append(dict(name=bn, parent=par,
                              conv=conv if (conv == conv and abs(conv) > 1e-6) else 1.0,
                              keyCount=nk, t=t, pos=pos, quat=quat, scale=scl))
        clips.append(dict(name=cname,duration=dur,B=nb,bones=bones,fps=30.0))
    return dict(name=name,skin=skin,anim=anim,clips=clips)

def read_ani_flags(mapname):
    # Флаг скелета (заголовок ani_list.ani, +0x3c). Бит 0x2 СТОИТ → ПЛОСКИЙ; СБРОШЕН → ИЕРАРХИЯ.
    # (Подтверждено эталоном: CClass_DispatchByFlag@0x99EDC0 / CShaderArray_LoadFile@0x1129490.)
    base = os.environ.get("ASDA_CLIENT_MAP", _P.MAP_DIR)
    p = os.path.join(base, mapname, "ani_list.ani")
    out = {}
    if not os.path.isfile(p):
        return out
    d = open(p, "rb").read()
    if len(d) < 12:
        return out
    cnt = struct.unpack_from("<I", d, 8)[0]; blob = 12
    for c in range(cnt):
        e = blob + c * 0x40
        if e + 0x40 > len(d):
            break
        nm = os.path.splitext(d[e:e+0x20].split(b"\x00")[0].decode("latin1", "ignore"))[0]
        out[nm] = struct.unpack_from("<I", d, e + 0x3c)[0]
    return out

def make_jw(bones, conv, hier):
    # flat: кейфрейм = world (jointWorld). hier: кейфрейм = LOCAL → world = world[parent]·local (column-conv,
    # = nodeApplyTransformAndScale движка для равномерного масштаба). Корни: world=local (иерархия их не меняет).
    flat = lambda i, k: conv.jointWorld(bones[i], k)
    if not hier:
        return flat
    cache = {}
    def jw(i, k):
        key = (i, k)
        if key in cache:
            return cache[key]
        L = flat(i, k)
        p = bones[i].get("parent", -1)
        W = (jw(p, k) @ L) if (0 <= p < len(bones) and p != i) else L
        cache[key] = W
        return W
    return jw

def compute_anchor_reloc(jbm, bones, jw0, B, K=3.0):
    # Отсев «якорей»: КОРНЕВАЯ (parent<0) СТАТИЧНАЯ кость, чьи вершины улетели далеко от тела (>K·размер тела)
    # и держат малую долю геометрии → прижать к телу (сохранить направление, ограничить ~1.5·размер).
    # Лечит «стратосферу» (далёкие статичные корневые якоря в данных) — иерархия их не двигает.
    pts = []; owncnt = [0]*B
    for me in jbm["meshes"]:
        used = np.unique(me["idx"]); pos = me["pos"][used].astype(np.float64)
        j0 = np.clip(me["j0"][used], 0, B-1)
        for vi in range(len(used)):
            M = jw0(int(j0[vi]), 0)
            pts.append((M @ np.array([pos[vi,0], pos[vi,1], -pos[vi,2], 1.0]))[:3])
            owncnt[int(j0[vi])] += 1
    if not pts:
        return {}
    pts = np.array(pts); ntot = len(pts)
    centroid = np.median(pts, axis=0)
    d = np.linalg.norm(pts - centroid, axis=1)
    extent = float(np.percentile(d, 75)) or 1.0
    reloc = {}
    for i in range(B):
        if bones[i].get("parent", -1) >= 0:
            continue                                  # только корни
        kc = bones[i].get("keyCount", 0)
        ps = np.asarray(bones[i].get("pos"))
        static = kc <= 1 or (ps.shape[0] > 1 and float(np.ptp(ps, axis=0).max()) < 1e-3)
        t = jw0(i, 0)[:3, 3]; dist = float(np.linalg.norm(t - centroid))
        frac = owncnt[i] / max(ntot, 1)
        if static and dist > K * extent and frac < 0.25:
            dirv = (t - centroid) / dist
            reloc[i] = centroid + dirv * (1.5 * extent)
    return reloc

def build_map(jbm, jbb, out, conv, hier=False):
    # СКЕЛЕТНЫЙ glTF для аним-объекта КАРТЫ. Отличие от персонажного build(): вершины карты в BONE-LOCAL
    # (а не bind/компонент), inverse-bind НЕ применяется движком (world = boneWorld·localVert). Поэтому
    # ПРЕДУМНОЖАЕМ вершины на bind[bone] (→ компонент-простр.), затем стандартный UE-скиннинг
    # jointWorld(k)·inv(bind)·(bind·local) = jointWorld(k)·local == вьювер. jointWorld == EngBoneWorld (conv).
    B = jbb['B']; bones = jbb['bones']
    jw0 = make_jw(bones, conv, hier)                 # flat или ИЕРАРХИЯ (flag bit0x2)
    reloc = compute_anchor_reloc(jbm, bones, jw0, B) # далёкие статичные корневые якоря → к телу
    def jw(i, k):
        W = jw0(i, k)
        if i in reloc:
            W = W.copy(); W[:3, 3] = reloc[i]
        return W
    bind = [jw(i, 0) for i in range(B)]
    invBind = [np.linalg.inv(bind[i]) for i in range(B)]
    bins = bytearray(); accessors = []; views = []
    def view(data, target=None):
        off = len(bins); bins.extend(data)
        while len(bins) % 4: bins.append(0)
        views.append(g.BufferView(buffer=0, byteOffset=off, byteLength=len(data), target=target)); return len(views)-1
    def acc(data, ct, count, at, mn=None, mx=None, target=None):
        v = view(data, target); a = g.Accessor(bufferView=v, componentType=ct, count=count, type=at)
        if mn is not None: a.min = mn; a.max = mx
        accessors.append(a); return len(accessors)-1
    # кости (плоско, ref-поза = bind) + Armature
    nodes = []; joint_node = []
    for i in range(B):
        t, q, s = conv.decompose(bind[i])
        nodes.append(g.Node(name=bones[i]['name'] or ("bone%d" % i), translation=t.tolist(),
                            rotation=[float(x) for x in q], scale=s.tolist()))
        joint_node.append(len(nodes)-1)
    nodes.append(g.Node(name="Armature", children=list(joint_node))); root_idx = len(nodes)-1
    # меши: верт в КОМПОНЕНТ-простр. = bind[j0] · (negate-z bone-local vert)
    prims = []; mats = []
    for me in jbm['meshes']:
        used, inv = np.unique(me['idx'], return_inverse=True)
        pos = me['pos'][used].astype(np.float64).copy(); pos[:, 2] *= -1
        nrm = me['nrm'][used].astype(np.float64).copy(); nrm[:, 2] *= -1
        uv  = me['uv'][used].astype(np.float32).copy()
        j0 = np.clip(me['j0'][used], 0, B-1); j1 = np.clip(me['j1'][used], 0, B-1); w0 = me['w0'][used].astype(np.float32)
        P = np.zeros((len(pos), 3), np.float32); N = np.zeros((len(nrm), 3), np.float32)
        for vi in range(len(pos)):
            M = bind[int(j0[vi])]
            P[vi] = (M @ np.array([pos[vi,0], pos[vi,1], pos[vi,2], 1.0]))[:3]
            N[vi] = (M @ np.array([nrm[vi,0], nrm[vi,1], nrm[vi,2], 0.0]))[:3]
        J4 = np.zeros((len(P), 4), np.uint16); J4[:,0] = j0; J4[:,1] = j1
        W4 = np.zeros((len(P), 4), np.float32); W4[:,0] = w0; W4[:,1] = 1.0 - w0
        I = inv.astype(np.uint32).reshape(-1,3)[:, ::-1].reshape(-1)
        aP = acc(P.tobytes(), g.FLOAT, len(P), g.VEC3, P.min(0).tolist(), P.max(0).tolist(), g.ARRAY_BUFFER)
        aN = acc(N.astype("<f4").tobytes(), g.FLOAT, len(N), g.VEC3, target=g.ARRAY_BUFFER)
        aU = acc(uv.tobytes(), g.FLOAT, len(uv), g.VEC2, target=g.ARRAY_BUFFER)
        aJ = acc(J4.tobytes(), g.UNSIGNED_SHORT, len(P), g.VEC4, target=g.ARRAY_BUFFER)
        aW = acc(W4.tobytes(), g.FLOAT, len(P), g.VEC4, target=g.ARRAY_BUFFER)
        aI = acc(I.astype("<u4").tobytes(), g.UNSIGNED_INT, len(I), g.SCALAR, target=g.ELEMENT_ARRAY_BUFFER)
        mats.append(g.Material(name=os.path.splitext(os.path.basename(me['tex']))[0] or "mat",
                    pbrMetallicRoughness=g.PbrMetallicRoughness(metallicFactor=0.0, roughnessFactor=1.0)))
        prims.append(g.Primitive(attributes=g.Attributes(POSITION=aP, NORMAL=aN, TEXCOORD_0=aU, JOINTS_0=aJ, WEIGHTS_0=aW),
                                indices=aI, material=len(mats)-1))
    ibm = np.stack([invBind[i].T for i in range(B)]).astype("<f4")
    aIBM = acc(ibm.tobytes(), g.FLOAT, B, g.MAT4)
    skin = g.Skin(inverseBindMatrices=aIBM, joints=joint_node, skeleton=root_idx)
    nodes.append(g.Node(name="mesh", mesh=0, skin=0)); mesh_idx = len(nodes)-1
    # анимация (один клип): per bone декомпозиция jointWorld(k)
    samplers = []; channels = []
    for i in range(B):
        if bones[i]['keyCount'] < 1: continue
        kc = bones[i]['keyCount']; Ts = []; Qs = []; Ss = []
        for k in range(kc):
            t, q, s = conv.decompose(jw(i, k)); Ts.append(t); Qs.append(np.asarray(q, np.float64)); Ss.append(s)
        for k in range(1, len(Qs)):
            if np.dot(Qs[k], Qs[k-1]) < 0: Qs[k] = -Qs[k]
        Qs = [[float(x) for x in q] for q in Qs]
        times = bones[i]['t'].astype("<f4")
        aT = acc(times.tobytes(), g.FLOAT, len(times), g.SCALAR, [float(times.min())], [float(times.max())])
        aTr = acc(np.array(Ts, "<f4").tobytes(), g.FLOAT, len(Ts), g.VEC3)
        aRo = acc(np.array(Qs, "<f4").tobytes(), g.FLOAT, len(Qs), g.VEC4)
        aSc = acc(np.array(Ss, "<f4").tobytes(), g.FLOAT, len(Ss), g.VEC3)
        for path, a in (("translation", aTr), ("rotation", aRo), ("scale", aSc)):
            samplers.append(g.AnimationSampler(input=aT, output=a, interpolation="LINEAR"))
            channels.append(g.AnimationChannel(sampler=len(samplers)-1, target=g.AnimationChannelTarget(node=joint_node[i], path=path)))
    anims = [g.Animation(name=jbb.get('name') or "anim", samplers=samplers, channels=channels)] if channels else []
    modelname = os.path.splitext(os.path.basename(out))[0]
    gltf = g.GLTF2(scene=0, scenes=[g.Scene(nodes=[root_idx, mesh_idx])], nodes=nodes,
                   meshes=[g.Mesh(primitives=prims, name=modelname)], materials=mats, skins=[skin],
                   animations=anims, accessors=accessors, bufferViews=views, buffers=[g.Buffer(byteLength=len(bins))])
    gltf.set_binary_blob(bytes(bins)); gltf.save_binary(out)

def objkey(m):  # ключ инстанса по objPlace (округлённая трансляция + грубый поворот)
    return (round(m[12],1), round(m[13],1), round(m[14],1),
            round(m[0],2), round(m[2],2), round(m[8],2))

def main():
    if len(sys.argv)<3:
        print("usage: asda_mapanim_to_gltf.py <map.mapraw> <outDir>"); return
    raw, outroot = sys.argv[1], sys.argv[2]
    conv = load_conv()
    data = _read_full(raw)
    name = data["name"]
    ani_flags = read_ani_flags(name)                 # {имя_клипа: флаг скелета}; бит0x2=плоский, иначе иерархия
    outdir = os.path.join(outroot, name, "anim"); os.makedirs(outdir, exist_ok=True)
    clips = data["clips"]
    # сгруппировать skin+anim по клипу
    by_clip = {}
    for sk in data["skin"]:
        by_clip.setdefault(sk["clip"], {"skin":[], "anim":[]})["skin"].append(sk)
    for am in data["anim"]:
        by_clip.setdefault(am["clip"], {"skin":[], "anim":[]})["anim"].append(am)

    manifest = {"map": name, "clips": []}
    built = 0
    for ci, grp in sorted(by_clip.items()):
        if ci < 0 or ci >= len(clips):
            continue
        clip = clips[ci]
        # инстансы по objPlace
        insts = {}
        for sk in grp["skin"]: insts.setdefault(objkey(sk["obj"]), sk["obj"])
        for am in grp["anim"]: insts.setdefault(objkey(am["obj"]), am["obj"])
        # меши ОДНОГО инстанса (первого ключа) — геометрия bone-local одинакова у всех
        if not insts: continue
        first = next(iter(insts))
        meshes = []
        for sk in grp["skin"]:
            if objkey(sk["obj"]) != first: continue
            meshes.append(dict(pos=sk["pos"], nrm=sk["nrm"], uv=sk["uv"], idx=sk["idx"],
                               j0=sk["j0"], j1=sk["j1"], w0=sk["w0"], tex=sk["tex"]))
        for am in grp["anim"]:
            if objkey(am["obj"]) != first: continue
            nv=len(am["pos"]); bone=max(am["bone"],0)
            meshes.append(dict(pos=am["pos"], nrm=am["nrm"], uv=am["uv"], idx=am["idx"],
                               j0=np.full(nv,bone,np.int64), j1=np.full(nv,bone,np.int64),
                               w0=np.ones(nv,np.float32), tex=am["tex"]))
        if not meshes: continue
        tex0 = meshes[0]["tex"]
        jbm = dict(tex=tex0, meshes=meshes)
        jbb = dict(B=clip["B"], bones=clip["bones"], fps=clip["fps"], duration=clip["duration"],
                   name=clip["name"] or ("clip_%d" % ci))
        glb = os.path.join(outdir, "clip_%d.glb" % ci)
        cname = os.path.splitext(clip["name"] or "")[0]
        flag = ani_flags.get(cname, 3)               # по умолч. 3 = плоский (безопасно, если флага нет)
        hier = (flag & 2) == 0                        # бит 0x2 СБРОШЕН → иерархия
        try:
            build_map(jbm, jbb, glb, conv, hier=hier)
            built += 1
        except Exception as e:
            print("  clip %d (%s) FAIL: %s" % (ci, clip["name"], e)); continue
        # текстуры по сабмешам (порядок == порядок примитивов в glb)
        texes = [os.path.splitext(os.path.basename(m["tex"]))[0] for m in meshes]
        manifest["clips"].append({
            "clip": ci, "name": clip["name"], "glb": "anim/clip_%d.glb" % ci,
            "textures": texes,
            "placements": [list(m) for m in insts.values()],
        })
    import json
    with open(os.path.join(outroot, name, "anim_manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f)
    print("OK %s: клипов собрано=%d, объектов(инстансов)=%d -> %s" %
          (name, built, sum(len(c["placements"]) for c in manifest["clips"]), outdir))

if __name__ == "__main__":
    main()
