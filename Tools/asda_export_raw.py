# Copyright (c) 2026 @sitxovski (Discord)
# asda_export_raw.py — экспорт .jbm+.jbb в ПЛОСКИЙ бинарь .asdaraw для живого C++-тюнера в UE.
# Парсинг берём из проверенного asda_skinned_to_gltf (parse_jbm/parse_jbb); НИКАКОЙ матовой
# математики тут нет — только сырьё. Всю конвенцию (flat/hier/quat/mirror/invbind) подбирает C++ актор.
#
# Формат .asdaraw (little-endian):
#   'ASDR' u32 version=1
#   u32 meshCount
#     per mesh: u32 vtx, u32 idxCount,
#               f32[vtx*8] (px,py,pz,nx,ny,nz,u,v), i32[vtx*2] (bone0,bone1), f32[vtx] (w0), u32[idxCount]
#   u32 boneCount
#     per bone: i32 parent, f32 conv, u32 keyCount, f32[keyCount*11] (time,px,py,pz,qx,qy,qz,qw,sx,sy,sz)
#   f32 fps
#   u32 nameLen + bytes  (имена костей, '\n'-разделитель — для отладки в редакторе)
import sys, os, struct, importlib.util, numpy as np

def load_conv():
    p=os.path.join(os.path.dirname(os.path.abspath(__file__)),"asda_skinned_to_gltf.py")
    spec=importlib.util.spec_from_file_location("conv",p); m=importlib.util.module_from_spec(spec)
    sys.modules["conv"]=m; spec.loader.exec_module(m); return m

def main():
    if len(sys.argv)<4:
        print("usage: asda_export_raw.py <jbm> <jbb> <out.asdaraw>"); return
    jm,jb,out=sys.argv[1],sys.argv[2],sys.argv[3]
    c=load_conv()
    jbm=c.parse_jbm(open(jm,"rb").read()); jbb=c.parse_jbb(open(jb,"rb").read())
    if not jbm or not jbb: print("parse fail"); return
    nfix=c.resolve_skin_by_name(jbm,jbb)   # скин-кости ПО ИМЕНИ (а не сырой индекс палитры)
    print("  скин-линковка по имени: мешей исправлено=%d" % nfix)
    o=bytearray()
    o+=b"ASDR"; o+=struct.pack("<I",1)
    meshes=jbm['meshes']; o+=struct.pack("<I",len(meshes))
    for me in meshes:
        V=len(me['pos']); idx=me['idx'].astype("<u4")
        o+=struct.pack("<II",V,len(idx))
        inter=np.zeros((V,8),np.float32)
        inter[:,0:3]=me['pos']; inter[:,3:6]=me['nrm']; inter[:,6:8]=me['uv']
        o+=inter.astype("<f4").tobytes()
        bones2=np.zeros((V,2),np.int32); bones2[:,0]=me['j0']; bones2[:,1]=me['j1']
        o+=bones2.astype("<i4").tobytes()
        o+=me['w0'].astype("<f4").tobytes()
        o+=idx.tobytes()
    bones=jbb['bones']; B=jbb['B']; o+=struct.pack("<I",B)
    names=[]
    for bn in bones:
        names.append(bn['name'] or "")
        kc=bn['keyCount']
        o+=struct.pack("<ifI",bn['parent'],float(bn['conv']),kc)
        if kc:
            arr=np.zeros((kc,11),np.float32)
            arr[:,0]=bn['t']*1000.0            # секунды→мс (как в файле)
            arr[:,1:4]=bn['pos']; arr[:,4:8]=bn['quat']; arr[:,8:11]=bn['scale']
            o+=arr.astype("<f4").tobytes()
    o+=struct.pack("<f",float(jbb['fps']))
    nm=("\n".join(names)).encode("utf-8"); o+=struct.pack("<I",len(nm)); o+=nm
    open(out,"wb").write(o)
    print("OK %s | meshes=%d verts=%d bones=%d fps=%.0f"%(os.path.basename(out),len(meshes),
          sum(len(m['pos']) for m in meshes),B,jbb['fps']))

if __name__=="__main__":
    main()
