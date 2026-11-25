#!/usr/bin/env python3
# Copyright (c) 2026 @sitxovski (Discord)
# asda_gen_effect_meshbones.py — генерирует маппинг attach-кость→ДЕКОРИРУЕМАЯ меш-кость для хирального фикса
# эффектов type-2 attached. Пишет export/maps/<map>/effect_meshbones.json (читается asda_place_effects.py).
#
# ЗАЧЕМ: меш анимированного объекта проходит per-bone negate game-Z в bone-local (asda_mapanim_to_gltf:190) +
# зеркальный импорт скелета (actor scale.X<0). Эффект (basePos) хиральность НЕ наследует → садится ЗЕРКАЛЬНО
# относительно декорируемого меша. asda_place_effects отражает basePos об local-Z ДЕКОРИРУЕМОЙ меш-кости —
# для этого нужно знать, какая меш-кость декорируется (лампа objgroupNN, а не attach-кость-джоинт Bone05).
#
# ЭВРИСТИКА декорируемой меш-кости: co-located (bind-dist<25 game-юнитов) с attach-костью, МАЛАЯ (5..200 скин-верт,
# т.е. декор-деталь, не тело >200). Нет такой → None (дефолт: отражать об game-Z, как радуга — эффект на своём якоре).
#
# Использование:  python asda_gen_effect_meshbones.py <map>    (по умолчанию alpen)
import json, struct, os, sys, numpy as np

RAW = r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/maps"

def load_glb(p):
    f=open(p,'rb'); f.read(4); struct.unpack('<II',f.read(8))
    jl,_=struct.unpack('<II',f.read(8)); js=json.loads(f.read(jl))
    bl,_=struct.unpack('<II',f.read(8)); b=f.read(bl)
    def acc(i):
        a=js['accessors'][i]; bv=js['bufferViews'][a['bufferView']]
        off=bv.get('byteOffset',0)+a.get('byteOffset',0); cnt=a['count']
        dt={5126:np.float32,5123:np.uint16,5125:np.uint32,5121:np.uint8}[a['componentType']]
        nc={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[a['type']]
        return np.frombuffer(b,dt,cnt*nc,off).reshape(cnt,nc)
    return js,acc

def decorated_meshbone(glbp, attach):
    js,acc=load_glb(glbp)
    nodes=js['nodes']; sk=js['skins'][0]; joints=sk['joints']; ibm=acc(sk['inverseBindMatrices'])
    n2j={nodes[jn].get('name'):ji for ji,jn in enumerate(joints)}
    if attach not in n2j: return None
    def bpos(ji): return np.linalg.inv(ibm[ji].reshape(4,4).T)[:3,3]   # world-bind позиция кости
    cnt={}
    for m in js['meshes']:
        for pr in m['primitives']:
            at=pr['attributes']
            if 'JOINTS_0' not in at: continue
            J=acc(at['JOINTS_0']); W=acc(at['WEIGHTS_0'])
            dom=J[np.arange(len(J)),np.argmax(W,axis=1)]           # доминантный джоинт вершины
            for ji in np.unique(dom): cnt[int(ji)]=cnt.get(int(ji),0)+int((dom==ji).sum())
    aji=n2j[attach]; ap=bpos(aji)
    cands=[]
    for ji,c in cnt.items():
        if ji==aji: continue
        d=float(np.linalg.norm(bpos(ji)-ap))
        if d<25 and 5<=c<=200: cands.append((ji,c,d))
    if cands:
        cands.sort(key=lambda x:(x[2],-x[1]))                      # ближайшая, потом больше верт
        return nodes[joints[cands[0][0]]].get('name')
    return None

def conv_bind(glbp, bone):
    # ★ bind-поворот кости для boneW-ориентации attached-эффекта: conv_bind = ZH·bind_glbᵀ·ZH; в печке
    #   boneW_rows = conv_bind · pmat_rows. Конвенция КАЛИБРОВАНА по радуге (совпадение с дамп-authored 4e-8).
    js,acc=load_glb(glbp)
    nodes=js['nodes']; sk=js['skins'][0]; joints=sk['joints']; ibm=acc(sk['inverseBindMatrices'])
    n2j={nodes[jn].get('name'):ji for ji,jn in enumerate(joints)}
    if bone not in n2j: return None
    W=np.linalg.inv(ibm[n2j[bone]].reshape(4,4).T)
    R=W[:3,:3].copy()
    for c in range(3):
        n=np.linalg.norm(R[:,c])
        if n>1e-6: R[:,c]/=n
    ZH=np.diag([1,1,-1.0])
    return [float(x) for x in (ZH@R.T@ZH).flatten()]

def main():
    mapname = sys.argv[1] if len(sys.argv)>1 else "alpen"
    eff=json.load(open(os.path.join(RAW,mapname,"effects.json"),encoding="utf-8"))
    mapping={}; binds={}
    for pl in eff["placements"]:
        if pl.get("type")!=2 or not pl.get("attached"): continue
        clip=pl.get("clip"); bone=pl.get("boneName")
        if clip is None or not bone: continue
        key="clip_%d:%s"%(clip,bone)
        if key in mapping: continue
        glb=os.path.join(RAW,mapname,"anim","clip_%d.glb"%clip)
        mapping[key]=decorated_meshbone(glb,bone) if os.path.isfile(glb) else None
        binds[key]=conv_bind(glb,bone) if os.path.isfile(glb) else None
    out=os.path.join(RAW,mapname,"effect_meshbones.json")
    json.dump(mapping, open(out,"w",encoding="utf-8"), ensure_ascii=False, indent=1)
    out2=os.path.join(RAW,mapname,"effect_bone_binds.json")
    json.dump(binds, open(out2,"w",encoding="utf-8"), indent=1)
    print("written %s (%d) + binds (%d)"%(mapname,len(mapping),len(binds)))

if __name__=="__main__":
    main()
