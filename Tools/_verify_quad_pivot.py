# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""Реплика центра квад-эмиттера: ТЕКУЩИЙ порт vs ЭТАЛОН (дизасм 0x120AEB0).
Считает в GAME-осях (до Xf актора). Выводит радиус орбиты и Δ порт↔эталон."""
import json, os, math, sys

MAPS = r'@@ASTORY_DEV_ROOT@@\Data\Export\maps'
D = math.pi/180.0

# --- порт (историческая карт-ветка, AsdaEffectActor.cpp:1657-1843) ---
def RXp(P,A):
    c,s=math.cos(A),math.sin(A); return (P[0], P[1]*c+P[2]*s, -P[1]*s+P[2]*c)
def RYp(P,A):
    c,s=math.cos(A),math.sin(A); return (P[0]*c-P[2]*s, P[1], P[0]*s+P[2]*c)
def RZp(P,A):
    c,s=math.cos(A),math.sin(A); return (P[0]*c+P[1]*s, -P[0]*s+P[1]*c, P[2])
# --- эталон D3DX row-vector (RotationX/Y/Z @0xFF4ADD/BDA/CED) ---
def RXd(P,A):
    c,s=math.cos(A),math.sin(A); return (P[0], P[1]*c-P[2]*s, P[1]*s+P[2]*c)
def RYd(P,A):
    c,s=math.cos(A),math.sin(A); return (P[0]*c+P[2]*s, P[1], -P[0]*s+P[2]*c)
def RZd(P,A):
    c,s=math.cos(A),math.sin(A); return (P[0]*c-P[1]*s, P[0]*s+P[1]*c, P[2])

def lerp(a,b,k): return [a[i]+(b[i]-a[i])*k for i in range(len(a))]

QUAD_TYPES=(1,2,0xB,0xC,0x13,0x14,0x18,0x19)

def nodes_of(p, life):
    """(t, roll, rot(3), pos(3), basePos(3)) как их печёт asda_place_effects.build_emitter"""
    tn = p.get("trackNodes") or []
    out=[]
    if tn:
        out.append((0.0,(p.get("rotY1") or [0])[0],
                    [(p.get("rotX") or [0])[0],(p.get("rotY") or [0])[0],(p.get("rotZ") or [0])[0]],
                    list(p.get("posStart",[0,0,0])), list(p.get("basePosStart",[0,0,0]))))
        for n in tn:
            out.append((min(n[0],life), n[28], [n[30],n[32],n[34]],
                        [n[7],n[9],n[11]], [n[1],n[3],n[5]]))
        r1=p.get("rotY1",[0,0,0]); rX=p.get("rotX",[0,0,0]); rY=p.get("rotY",[0,0,0]); rZ=p.get("rotZ",[0,0,0])
        out.append((life, r1[1], [rX[1],rY[1],rZ[1]],
                    list(p.get("posEnd",[0,0,0])), list(p.get("basePosEnd",[0,0,0]))))
        out.sort(key=lambda x:x[0])
    else:
        r1=p.get("rotY1",[0,0,0]); rX=p.get("rotX",[0,0,0]); rY=p.get("rotY",[0,0,0]); rZ=p.get("rotZ",[0,0,0])
        out=[(0.0, r1[0], [rX[0],rY[0],rZ[0]], list(p.get("posStart",[0,0,0])), list(p.get("basePosStart",[0,0,0]))),
             (life, r1[1], [rX[1],rY[1],rZ[1]], list(p.get("posEnd",[0,0,0])),  list(p.get("basePosEnd",[0,0,0])))]
    return out

def sample(nd, t):
    if t<=nd[0][0]: n=nd[0]; return n[1],n[2],n[3],n[4]
    for i in range(len(nd)-1):
        a,b=nd[i],nd[i+1]
        if a[0]<=t<=b[0]:
            k=0.0 if b[0]<=a[0] else (t-a[0])/(b[0]-a[0])
            return (a[1]+(b[1]-a[1])*k, lerp(a[2],b[2],k), lerp(a[3],b[3],k), lerp(a[4],b[4],k))
    n=nd[-1]; return n[1],n[2],n[3],n[4]

def centers(p, holder_deg, billboard):
    life=min(max((p.get("life") or [1.0])[0],0.1),10.0)
    nd=nodes_of(p,life)
    P,Eo=[],[]
    N=72
    for i in range(N):
        t=life*i/N
        roll,rot,pos,bp=sample(nd,t)
        # --- ПОРТ ---
        if billboard:
            g=tuple(pos)                                  # без вращений вообще
        else:
            g=RYp(tuple(pos), roll*D)
            g=RXp(g,rot[0]*D); g=RYp(g,rot[1]*D); g=RZp(g,rot[2]*D)
            g=RYp(g,holder_deg*D)
        # --- ЭТАЛОН: posChan · RotX·RotY·RotZ · N48 ---
        e=RXd(tuple(pos),rot[0]*D); e=RYd(e,rot[1]*D); e=RZd(e,rot[2]*D)
        e=RYd(e,holder_deg*D)
        P.append(g); Eo.append(e)
    return P,Eo,life

def stat(pts):
    m=[sum(q[i] for q in pts)/len(pts) for i in range(3)]
    r=max(math.dist(q,m) for q in pts)
    return m,r

rows=[]
for mp in sorted(os.listdir(MAPS)):
    f=os.path.join(MAPS,mp,'effects.json')
    if not os.path.isfile(f): continue
    try: d=json.load(open(f,encoding='utf-8'))
    except Exception: continue
    jeb=d.get('jeb') or {}
    for eff,ed in jeb.items():
        hdr=ed.get('header') or {}
        hrot=(hdr.get('rotY') or [0,0,0])
        holder=float(hrot[0]) if hrot else 0.0
        for em in ed.get('emitters',[]):
            t=em['type']; p=em['params']
            if t not in QUAD_TYPES or not p.get('growQuad'): continue
            bill = int(p.get('billboardMode',0) or 0)!=0
            P,E,life=centers(p,holder,bill)
            mp_,rp=stat(P); me_,re_=stat(E)
            dmax=max(math.dist(P[i],E[i]) for i in range(len(P)))
            if rp<1e-3 and dmax<1e-3: continue
            rows.append((mp,eff,em['name'],t,bill,round(rp,1),round(re_,1),round(dmax,1),life,
                         tuple(round(v,1) for v in p.get('posStart',[0,0,0])),
                         tuple(round(v,1) for v in (p.get('rotY1',[0])[:2]+p.get('rotX',[0])[:2]+p.get('rotY',[0])[:2]+p.get('rotZ',[0])[:2]))))

rows.sort(key=lambda r:-r[7])
print("map | effect | emitter | type | bill | Rорб_порт | Rорб_эталон | Δmax | life | pos | rots")
for r in rows:
    print(" | ".join(str(x) for x in r))
print("ВСЕГО затронутых квад-эмиттеров:", len(rows))
orb=[r for r in rows if r[5]>1.0 and r[6]<1.0]
print("ОРБИТА вместо вращения на месте (Rпорт>1, Rэталон~0):", len(orb))
