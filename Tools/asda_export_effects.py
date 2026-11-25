#!/usr/bin/env python3
# Copyright (c) 2026 @sitxovski (Discord)
# asda_export_effects.py — экспорт эффектов карт в JSON для UE (Niagara).
# Источники: <map>.mapraw секция effects (pos + selfMat rot/scale + type/attach/bone + timeSync) +
# .JEB параметры эмиттеров (asda_jeb_parse, верифицировано live.dmp 48/48). На выходе:
#   export/maps/<map>/effects.json = {placements:[...], jeb:{<effname>: {blendAdditive, emitters:[params]}}}
# Матрица/pos — СЫРЫЕ игровые (конверсию в UE делает шаг размещения, как у объектов/анимаций).
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import struct, os, json, sys, importlib.util as _iu

_spec = _iu.spec_from_file_location("jeb", os.path.join(os.path.dirname(os.path.abspath(__file__)), "asda_jeb_parse.py"))
jeb = _iu.module_from_spec(_spec); _spec.loader.exec_module(jeb)

# ★ Ридер .mapraw — ЕДИНЫЙ модуль asda_mapraw.py (было 5 копий class R по тулам).
from asda_mapraw import R


def read_effects(path):
    r = R(open(path, "rb").read())
    assert r.d[:4] == b"AMRW"; r.o = 4
    r.u32(); r.s_()                                              # ver, name
    r.u32(); r.f32(); r.f32(); r.fa(3); r.fa(3); r.fa(3)         # scene
    pc = r.u32(); ic = r.u32(); r.skip(2*ic)                     # terrain idx
    for _ in range(pc):
        vtx = r.u32()
        if vtx > 0: r.skip(vtx*3*4 + vtx*2*4 + vtx*2*4 + vtx*4)
        r.u32(); r.u32(); r.f32(); r.f32(); r.s_(); r.f32(); r.s_(); r.f32()
    mc = r.u32()
    for _ in range(mc):
        r.s_(); r.s_(); v = r.u32(); r.skip(v*3*4 + v*3*4 + v*2*4); icx = r.u32(); r.skip(icx*4)
    ni = r.u32()
    for _ in range(ni): r.u32(); r.skip(16*4); r.i32(); r.i32(); r.skip(4*4)   # + uvScroll
    bc = r.u32(); r.s_(); r.f32()
    for _ in range(bc):
        wv = r.u32(); r.skip(wv*3*4 + wv*2*4); wic = r.u32(); r.skip(wic*2)
    ec = r.u32(); effs = []
    for _ in range(ec):
        pos = r.fa(3); name = r.s_(); typ = r.i32(); bone = r.i32()
        mat = r.fa(16); tsync = r.f32(); attach = r.i32()
        pmat = r.fa(16); clip = r.i32(); attached = r.i32()     # родительская матрица + клип (type 1/2)
        bname = r.s_()                                          # имя кости (file-order) — сокет attach в UE
        effs.append({"name": name, "pos": pos, "matrix": mat, "type": typ,
                     "bone": bone, "boneName": bname, "attach": attach, "timeSync": tsync,
                     "pmat": pmat, "clip": clip, "attached": bool(attached)})
    return effs

def main():
    if len(sys.argv) < 3:
        print("usage: asda_export_effects.py <map.mapraw> <outRoot>"); return
    raw, outroot = sys.argv[1], sys.argv[2]
    mapname = os.path.splitext(os.path.basename(raw))[0]
    effs = read_effects(raw)
    jebs = {}; missing = set()
    for e in effs:
        nm = e["name"]
        if nm in jebs or nm in missing: continue
        r = jeb.parse_effect(nm)
        if not r: missing.add(nm); continue
        jebs[nm] = {"blendAdditive": r["blendAdditive"], "blendSrc": r["blendSrc"], "blendDst": r["blendDst"],
                    "blendMode": r["blendMode"],   # add / mod (D3D 6:4 тёмный дым) / alpha
                    "objectCount": r["objectCount"], "header": r.get("header", {}),
                    "emitters": [{"idx": em["idx"], "type": em["type"], "name": em["name"],
                                  "tex": em["tex"], "mesh": em["mesh"], "jem": em.get("jem", ""),
                                  "startDelay": em.get("startDelay", 0.0),
                                  "params": em["params"]}
                                 for em in r["emitters"]]}
    out = {"map": mapname, "placements": effs, "jeb": jebs,
           "missingJeb": sorted(missing), "count": len(effs), "uniqueEffects": len(jebs)}
    outdir = os.path.join(outroot, mapname); os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, "effects.json"), "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False)
    print("OK %-18s размещений=%d уник=%d нет.jeb=%d" % (mapname, len(effs), len(jebs), len(missing)))

if __name__ == "__main__":
    main()
