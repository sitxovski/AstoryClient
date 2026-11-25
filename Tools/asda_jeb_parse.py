#!/usr/bin/env python3
# Copyright (c) 2026 @sitxovski (Discord)
# asda_jeb_parse.py — парсер .JEB (эффект-эмиттеры) из Data/Effect/Model/EffectModel.bnz (ZIP).
# Формат RE: FUN_0117e8c0 (header) + ридеры типов; пер-типовые офсеты = RE/reports/effects_runtime/SPEC_DISKMAP_ALL.md
# (CONFIRMED дизасмом + проверено на реальных .jeb). Каждый объект .JEB = ОТДЕЛЬНЫЙ эмиттер частиц.
#
# Header 0x4C: +0x04 version, +0x0C objectCount, +0x44/+0x48 blend src/dst (dst==1 → additive).
# Затем u32 type-array[objectCount], затем objectCount записей переменного размера (таблица ниже).
# Запись: name@+0x00(char32), tex@+0x24(char100, SINGLE/HYBRID). Параметры эмиттера — ПЕР-ТИПОВО.
import struct, zipfile, os, json, sys
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P

BNZ = os.environ.get("ASDA_EFFECT_BNZ", _P.EFFECT_MODEL_BNZ)

SINGLE = {1:0x174,2:0x178,3:0x1DC,4:0x18C,5:0x164,0xF:0x1F8,0x11:0x1D8,0x13:0x17C,0x14:0x180,0x15:0x194,0x16:0x1DC,0x17:0x1DC,0x20:0x204}
MULTI  = {6:0x114,7:0x118,8:0x12C,9:0x17C,0xA:0x1A0}                       # count@+0x24, +count*0x64
HYBRID = {0xB:(0x178,0xA0),0xC:(0x17C,0xA0),0xD:(0x1E0,0xE8),0xE:(0x190,0xB8),0x10:(0x1BC,0x100),
          0x12:(0x1DC,0xA0),0x18:(0x180,0xA0),0x19:(0x184,0xA0),0x1A:(0x198,0xB8),0x1B:(0x1E0,0xE8),
          0x1C:(0x1BC,0x100),0x1F:(0x1DC,0xA0)}                            # count@fixed-4, +count*elem
MESH_TYPES = {0x11,0x12,0x1F}                                             # модель-эмиттеры (.jem MeshRenderer)

import asda_bintable as _BT   # общий разбор C-строк (волна 2 аудита дублей)
def _cstr(d,o,n): return _BT.cstr(d, o, n, "latin1")
def _f(d,o): return struct.unpack_from("<f",d,o)[0] if o+4<=len(d) else 0.0
def _u32(d,o): return struct.unpack_from("<I",d,o)[0] if o+4<=len(d) else 0
def _v3(d,b,x,y,z): return [_f(d,b+x),_f(d,b+y),_f(d,b+z)]

def rec_size(d, ro, t):
    if t in SINGLE: return SINGLE[t]
    if t in MULTI:  return MULTI[t] + _u32(d,ro+0x24)*0x64
    if t in HYBRID:
        fixed,elem = HYBRID[t]; return fixed + _u32(d,ro+fixed-4)*elem
    return None

def extract_modern(d, r, sh=0):
    # MODERN (тип 0xD HYBRID / 3 SINGLE / 0x1B со сдвигом sh=8). Семантика верифицирована дампом
    # (effects_system_verification.md §1) + аудит 2026-07-27 (audit_2026-07-27/loaders_formats.md):
    # 0xD = TRACK-эмиттер: параметры — «birth/death» пары (march за период/жизнь); coneDir-полей НЕТ
    # (ось конуса = (0,1,0) дефолт ctor / track-node[53..55]); 0xF0..0xF8 = УГОЛ конуса (град).
    # ★ 0x1B (T1B @0x117CBA0): после atlas cols/rows@0x88/0x8C все поля сдвинуты на +8 → sh=8.
    # ★ alpha birth = @0xC0 (лоадер T0D: 0xC0→+0x4BC alphaMin; прежний 0x9C был дублем colorR — маскировалось 1.0).
    def F(o): return _f(d, r+o+sh)
    def V3(x,y,z): return [F(x),F(y),F(z)]
    return {
        "period":   F(0x94),                                               # период эмиттера, сек (время death-узла)
        "spawnRate":[F(0x124), F(0x128)],                                  # частиц/сек birth→target (V: sodol 15)
        # ★ ЖИЗНЬ частицы = eleLife@0x118 s/E/v (лоадер-трейс; выхлоп 5с); НОЛЬ → жизнь = длительность эмиттера.
        "partLife": [(F(0x118) if F(0x118)>0.01 else F(0x94)), F(0x120)],
        "alpha":    [F(0xC0), F(0xC4), F(0xC8)],                           # birth/death/var (АУДИТ: 0xC0, не 0x9C)
        "sizeX":    [F(0xCC), F(0xD0), F(0xD4)],                           # birth/death/var (half-extent)
        "sizeY":    [F(0xD8), F(0xDC), F(0xE0)],
        "speed":    [F(0xE4), F(0xE8), F(0xEC)],                           # скорость конуса birth/death/var
        "coneAngle":[F(0xF0), F(0xF4), F(0xF8)],                           # УГОЛ birth/death/range, ГРАДУСЫ
        # ★ ОСЬ КОНУСА С ДИСКА: thetaSpread-блок @0xFC/0x100/0x104 (XYZ game). Ноль → дефолт вверх.
        "coneAxis": ([F(0xFC), F(0x100), F(0x104)]
                     if abs(F(0xFC))+abs(F(0x100))+abs(F(0x104))>1e-4 else [0.0,1.0,0.0]),
        # ★ ЦВЕТА: START@0x9C, VAR@0xA8, END@0xB4.
        "colorBirth":[F(0x9C), F(0xA0), F(0xA4)],
        "colorVar": [F(0xA8), F(0xAC), F(0xB0)],
        "colorDeath":[F(0xB4), F(0xB8), F(0xBC)],
        "gravity":  [0.0, 0.0, 0.0],                                       # оффсет в записи не подтверждён (см. partLife)
        "velBase":  V3(0x194,0x198,0x19C),                                 # spawnMode=2 (эталон: тот же конус — vel не используется)
        "velTarget":V3(0x1A0,0x1A4,0x1A8),
        "velRange": V3(0x1AC,0x1B0,0x1B4),
        # ★★★ ТОЧКА ЭМИССИИ = стандартная тройка (СТАРТ, КОНЕЦ, разброс), а НЕ константа.
        #   Эталон `CEmitter_InitReset@0x120F0E0`: emitPos = posBase + rand_pm·posRange,
        #   emitPosVel = (posTarget − emitPos)/период; `CEmitter_Update_v3@0x1211C20`:
        #   emitPos += dt·emitPosVel. Порт до 2026-08-14 читал только posBase ⇒ у 72 из 113
        #   партикл-эмиттеров канала предметов пропадало ВСЁ движение (см. item_fx_audit.md §2).
        "posBase":  V3(0x1B8,0x1BC,0x1C0),                                 # СТАРТ точки эмиссии
        "posTarget":V3(0x1C4,0x1C8,0x1CC),                                 # КОНЕЦ (марш за период)
        "posRange": V3(0x1D0,0x1D4,0x1D8),                                 # разброс СТАРТА (rand_pm)
        # ★★★ ВРАЩЕНИЕ ЭМИТТЕРА (градусы, game-оси; тройки старт/КОНЕЦ/разброс, марш за период).
        #   Раньше эти поля не читались ВООБЩЕ. Диск→рантайм (SPEC_DISKMAP_ALL §3, CONFIRMED
        #   дизасмом лоадера `effectLoadNodeType13`): 0x170..0x178 → +0x564..+0x56C,
        #   0x17C→+0x570 / 0x180→+0x578 / 0x184→+0x574, 0x188→+0x57C / 0x18C→+0x584 / 0x190→+0x580.
        #   Именно эти слоты в `CEmitter_InitReset` питают rotAngle[0] (rotX), rotAngle[2] (rotY) и
        #   colorInterpGB[4] (rotZ), из которых `CEmitter_Update_v3` строит R = RotX·RotY·RotZ и
        #   вращает ею ТОЧКУ ЭМИССИИ. Hex-Rays подписал слоты «colorBaseG/colorEndG/…» — миснейм
        #   union'а: ЦВЕТ живёт по 0x9C/0xA8/0xB4 (вердикт SPEC §6), а здесь лежат УГЛЫ.
        #   Второй метод (сплошной скан 3236 эмиттеров архива): по 0x17C/0x180/0x188/0x18C только
        #   канонические углы (±45/±90/±120/±135/±180/225/270/300/315/360/500/720/2880), по
        #   «разбросам» 0x178/0x184/0x190 — НОЛЬ во всём архиве.
        "rotX":     [F(0x170), F(0x174), F(0x178)],                        # старт/КОНЕЦ/разброс, °
        "rotY":     [F(0x17C), F(0x180), F(0x184)],
        "rotZ":     [F(0x188), F(0x18C), F(0x190)],
        "spawnMode": _u32(d,r+0x8C+sh),                                    # spawnModeDefault
        "endMode":  _u32(d,r+0x88+sh), "billboardMode": _u32(d,r+0x90+sh),
        "conf": "C/V (дамп 48/48 + аудит 2026-07-27)",
    }
def extract_type20(d, r):
    # ★ ТИП 0x20 — партикл-эмиттер класса 0xE0C0, лоадер CEffect_LoadEmitterType32@0x117E190 (SINGLE 0x204).
    #   Родня 0xD (0xE074) / 0x1B (0xE098), но раскладка — НЕ сдвиг, а ПЕРЕСТАНОВКА: голова 0x88..0xE0
    #   совпадает с 0xD байт-в-байт, затем 3 лишних дворда 0xE4..0xEC (node+0xE0A0..0xE0A8, поля класса
    #   0x20), а дальше блоки идут в другом порядке. Таблица восстановлена трассировкой node-оффсетов
    #   лоадера (v8..v95 → node), сверена с таблицей 0xD из effect_loaders_pertype.md §5:
    #     0x88 endMode(+0x560) | 0x8C eleCount(+0x490) | 0x90 eleRepeat(+0x164) | 0x94 lifeMin(+0x120)
    #     0x98 lifeMax(+0x5D0) | 0x9C..A4 RGB START | 0xA8..B0 RGB VAR | 0xB4..BC RGB END
    #     0xC0/C4/C8 alpha min/max/var | 0xCC.. sizeX | 0xD8.. sizeY
    #     0xF0..F8 speed(+0x4F8..500) | 0xFC..104 coneAngle(+0x510..518) | 0x108..110 coneAxis(+0x51C..524)
    #     0x114/0x118 spawnPosMode/Radius(+0x140/144) | 0x12C/0x130 thetaMin/Max(+0x508/50C)
    #     0x134/0x138/0x13C eleLife min/max/var(+0x498/4A0/49C) | 0x140/0x144 rate(+0x5D4/5D8)
    #     0x148/0x14C uvTile(+0x10C/110) | 0x17C.. rot(+0x564..584)
    #   Векторные каналы сопоставлены ПО NODE-СЛОТУ с 0xD (иначе перепутались бы местами):
    #     node 0x5AC→0x1A0, 0x5C4→0x1AC, 0x5B8→0x1B8, 0x588→0x1C4, 0x5A0→0x1D0, 0x594→0x1DC.
    #   Хвост 0x1E8..0x204 (7 двордов) лоадером НЕ читается.
    def F(o): return _f(d, r+o)
    def V3(o): return [F(o), F(o+4), F(o+8)]
    life0 = F(0x134)
    return {
        "period":   F(0x94),
        "spawnRate":[F(0x140), F(0x144)],
        "partLife": [(life0 if life0 > 0.01 else F(0x94)), F(0x13C)],
        "alpha":    [F(0xC0), F(0xC4), F(0xC8)],
        "sizeX":    [F(0xCC), F(0xD0), F(0xD4)],
        "sizeY":    [F(0xD8), F(0xDC), F(0xE0)],
        "speed":    [F(0xF0), F(0xF4), F(0xF8)],
        "coneAngle":[F(0xFC), F(0x100), F(0x104)],                         # градусы
        "coneAxis": (V3(0x108) if abs(F(0x108))+abs(F(0x10C))+abs(F(0x110)) > 1e-4 else [0.0,1.0,0.0]),
        "colorBirth":[F(0x9C), F(0xA0), F(0xA4)],
        "colorVar": [F(0xA8), F(0xAC), F(0xB0)],
        "colorDeath":[F(0xB4), F(0xB8), F(0xBC)],
        "gravity":  [0.0, 0.0, 0.0],                                       # у 0xD тоже не подтверждён
        "velBase":  V3(0x1C4), "velTarget": V3(0x1A0), "velRange": V3(0x1B8),
        "posBase":  V3(0x1DC), "posTarget": V3(0x1D0), "posRange": V3(0x1AC),
        "spawnMode": _u32(d,r+0x8C),
        "endMode":  _u32(d,r+0x88), "billboardMode": _u32(d,r+0x90),
        "spawnPosMode": _u32(d,r+0x114), "spawnPosRadius": F(0x118),
        "conf": "C (трассировка node-оффсетов CEffect_LoadEmitterType32@0x117E190, 2026-08-07)",
    }

def extract_t17(d, r):
    # ТИП 0x17 — grid-частицы «блёстки» крыльев (SINGLE 0x1DC). Раскладка = wings_soulmate_re.md §D1
    # (CONFIRMED дизасм effectEmitterLoadFromFile_Type23@0x117AB00 / gridParticleEmitterCtor@0x12426B0;
    # проверено на eff_wing_m_ss_00 = §D3). Как 0xD, но SINGLE (без track-узлов) + атлас-раскадровка UV.
    # Ключи в стиле extract_modern (0xD) → потребляет ветка build_emitter t in (0xD,3,0x1B,0x17).
    def F(o): return _f(d, r+o)
    period = F(0x9C)                                                       # период эмиттера base (var @0xA0)
    life0  = F(0x118)                                                      # жизнь частицы base (var @0x11C)
    axis = [F(0x104), F(0x108), F(0x10C)]                                  # ось конуса XYZ (знак = направление)
    if abs(axis[0])+abs(axis[1])+abs(axis[2]) < 1e-4: axis = [0.0, 1.0, 0.0]
    return {
        "period":   period,
        "spawnRate":[F(0x12C), F(0x130)],                                  # FLOAT start/END, част/сек
        "partLife": [(life0 if life0 > 0.01 else period), F(0x11C)],       # base(→period если 0)/var
        "alpha":    [F(0xC8), F(0xCC), F(0xD0)],                           # start/END/var
        "sizeX":    [F(0xD4), F(0xD8), F(0xDC)],                           # half-extent start/END/var
        "sizeY":    [F(0xE0), F(0xE4), F(0xE8)],
        "speed":    [F(0xEC), F(0xF0), F(0xF4)],                           # скорость конуса start/END/var
        "coneAngle":[F(0xF8), F(0xFC), F(0x100)],                          # угол конуса start/END/var, ГРАДУСЫ
        "coneAxis": axis,
        "colorBirth":[F(0xA4), F(0xA8), F(0xAC)],                          # START RGB
        "colorDeath":[F(0xB0), F(0xB4), F(0xB8)],                          # END RGB
        "colorVar": [F(0xBC), F(0xC0), F(0xC4)],                           # VAR RGB
        "gravity":  [F(0x120)*9.8, F(0x124)*9.8, F(0x128)*9.8],            # start/END/var ×9.8, ось −Y мир
        "velBase":  _v3(d,r,0x194,0x198,0x19C),                            # хвост как 0xD
        "velTarget":_v3(d,r,0x1A0,0x1A4,0x1A8),
        "velRange": _v3(d,r,0x1AC,0x1B0,0x1B4),
        "posBase":  _v3(d,r,0x1B8,0x1BC,0x1C0),                            # локальный разброс спавна
        "posTarget":_v3(d,r,0x1C4,0x1C8,0x1CC),
        "posRange": _v3(d,r,0x1D0,0x1D4,0x1D8),
        "spawnMode": _u32(d,r+0x94),                                       # spawnModeDefault (2=track)
        "endMode":  _u32(d,r+0x90), "billboardMode": _u32(d,r+0x98),
        "atlasCols": _u32(d,r+0x88), "atlasRows": _u32(d,r+0x8C),          # атлас cols/rows (шаг UV 1/cols,1/rows)
        # ★ КРУГ 3 (2026-08-03, live cdb-захват + дизасм): 0x110 = spawnPosMode (u32, таблица методов
        #   emitter+0x148, ctor 0x120DE20; дефолт 0, радиус-дефолт 10.0), 0x114 = spawnPosRadius (float).
        #   Мода 5 @0x1213340: мировая точка эмиссии = база + rand_pm(треуг.)·R ПО КАЖДОЙ ОСИ (куб ±R в
        #   МИРОВЫХ осях, новая точка КАЖДЫЙ КАДР). У ВСЕХ крыльев mode=5 R=50 — облако ±50 юн вокруг
        #   персонажа = «блёстки у крыльев»; без этого поля все частицы рождались в одной точке у спины.
        "spawnPosMode": _u32(d,r+0x110), "spawnPosRadius": _f(d,r+0x114),
        "conf": "C (§D wings_soulmate_re, дизасм 0x117AB00)",
    }

def read_track_nodes(d, r, t):
    # хвост типа 0xD/0x1B: count×0xE8 = ГОТОВЫЕ track-узлы 58×float (verification §2):
    # [0]=time; пары set/rate: [1..6] velSpawn, [7..12] emitPos, [13/14] alpha, [15/16] sizeX,
    # [17/18] sizeY, [19..21] RGB, [34/35] speed, [36/37] coneAngle, [47/48] gravity, [49/50] spawnRate,
    # [53..55] coneAxis. При count=0 рантайм строит 2 дефолт-узла birth/death из fixed-полей.
    fixed, elem = HYBRID[t]
    cnt = _u32(d, r+fixed-4)
    nodes = []
    nf = elem // 4                                   # 0xE8=58f (0xD, ГОТОВЫЕ узлы); 0xA0=40dw (0xB/0xC/0x18,
    for i in range(min(cnt, 64)):                    # разворачиваются в 232 при загрузке); 0xB8=46; 0x100=64
        base = r + fixed + i*elem
        if base + elem > len(d): break
        nodes.append([_f(d, base+4*k) for k in range(nf)])
    return nodes
# LEGACY-семейство (тип 1/0xB/0xC/0xE/0x18) — единая раскладка типа-1 со сдвигами по namebuf.
# velBase НЕ читается (поток через КОНУС: ось gap_51C + speed base/var). ВАЖНО (verification §9):
# lifeBaseSeed(+0x120) = ПЕРИОД эмиттера (время death-узла), НЕ rate; фактический rate = track-node[49].
LEGACY = {
    # offsets: period=lifeBaseSeed; RGB base(+0x500/570/57C) — для 0xB это FALLBACK-цвет квада (0xA0/A4/A8→+534..53C)
    1:    dict(period=0x8C, sizeBase=0x104,sizeTgt=0x94,sizeRng=0x98, cR=0xFC,cG=0x134,cB=0x14C, coneX=0x9C,coneZ=0xAC, aCur=0xCC,aMax=0xC8, pX=0x140,pY=0x10C,pZ=0x114, end=0x160, sX=0xD8,sY=0xE8),
    0xB:  dict(period=0x8C, sizeBase=0x104,sizeTgt=0x94,sizeRng=0x98, cR=0xFC,cG=0x134,cB=0x14C, coneX=0x9C,coneZ=0xAC, aCur=0xCC,aMax=0xC8, pX=0x140,pY=0x10C,pZ=0x114, end=0x160, sX=0xD8,sY=0xE8),
    0xC:  dict(period=0x90, sizeBase=0x108,sizeTgt=0x98,sizeRng=0x9C, cR=0x100,cG=0x138,cB=0x150, coneX=0xA0,coneZ=0xB0, aCur=0xCC,aMax=0xD0, pX=0x144,pY=0x110,pZ=0x118, end=0x164, sX=0xDC,sY=0xE8),
    0xE:  dict(period=0x8C, sizeBase=0xF8, sizeTgt=0x94,sizeRng=0x98, cR=0xF0, cG=0x14C,cB=0x164, coneX=0x9C,coneZ=0xAC, aCur=0xC8,aMax=0xCC, pX=0x158,pY=0x124,pZ=0x128, end=0x178, sX=None,sY=0xD8),
    0x18: dict(period=0x94, sizeBase=0x10C,sizeTgt=0x9C,sizeRng=0xA0, cR=0x104,cG=0x13C,cB=0x154, coneX=0xA4,coneZ=0xB4, aCur=0xD0,aMax=0xD4, pX=0x148,pY=0x114,pZ=0x11C, end=0x168, sX=0xE0,sY=0xEC),
}
def extract_ring(d, r, sh=0):
    # тип 0xE = КОЛЬЦО-ЛЕНТА (усечённый конус, 30 пар вершин) — SPEC §11 (CONFIRMED дизасм+файлы).
    # Порядок ВСЕХ дисковых троек: (start, END, var)! Каналы маршируют start→END за lifetime.
    # ★ sh — сдвиг всей раскладки вперёд на пару atlasCols/atlasRows@0x88/0x8C (типы 0x15/0x1A).
    #   Атлас вставлен ПЕРЕД loopMode ⇒ сдвигается ВСЁ, включая loopMode (в отличие от квадов 0xC,
    #   где billboardMode вставлен ПОСЛЕ endMode). Подтверждено лоадером sceneNodeLoadType21@0x1179E80
    #   (0x15: v8/v9→node+4624/4628 атлас, v10→node+1208 loopMode, v11→node+288 duration) и сырыми
    #   байтами (drainb01_Cyl_00: +0x88=3 +0x8C=3 атлас, +0x90=0 loopMode, +0x94=1.0 duration).
    def tri(o): return [_f(d,r+o+sh), _f(d,r+o+4+sh), _f(d,r+o+8+sh)]     # start/END/var
    return {
        "ring": True,
        "loopMode": _u32(d,r+0x88+sh),                            # 0=die 1=respawn 2=ping-pong 3=loop
        "life": [_f(d,r+0x8C+sh), _f(d,r+0x90+sh)],               # base/var, кламп 0.1..10
        "colorStart": [_f(d,r+0x94+sh),_f(d,r+0x98+sh),_f(d,r+0x9C+sh)],
        "colorEnd":   [_f(d,r+0xA0+sh),_f(d,r+0xA4+sh),_f(d,r+0xA8+sh)],
        "alpha": tri(0xB8), "scale": tri(0xC4),
        "rollY": tri(0xD0), "rotX": tri(0xDC), "rotY": tri(0xE8), "rotZ": tri(0xF4),
        "height": tri(0x100), "topR": tri(0x10C), "botR": tri(0x118),
        "uScroll": tri(0x124), "vScroll": tri(0x130),
        "uTile": _f(d,r+0x13C+sh), "vA": _f(d,r+0x140+sh),
        # ВЕКТОРНЫЕ каналы: подряд СТАРТ-вектор XYZ, END-вектор, var-вектор (подтв. вьюером P(0x144/148/14C))
        "basePosStart": _v3(d,r+sh,0x144,0x148,0x14C), "basePosEnd": _v3(d,r+sh,0x150,0x154,0x158),
        "posStart":     _v3(d,r+sh,0x168,0x16C,0x170), "posEnd":     _v3(d,r+sh,0x174,0x178,0x17C),
        "conf": "C/V (§11)",
    }

def extract_wave(d, r, sh=0, atlas_sh=0):
    # Квад-семейство 0xB (sh=0) / 0xC (sh=4, доп.поле billboardMode@0x8C). ИСТИННАЯ раскладка (лоадер-трейс
    # modelLoadNodeType11@0x1174A60 / sceneElem_LoadFromFile@0x1175250, effect_loaders_pertype.md).
    # ДВА офсет-канала: basePos = СЫРОЙ МИР (перезапись трансляции); pos = орбитальный (канальные вращ.+матрица узла).
    # rot: RotY(вторич)/RotX/RotY(осн. плоский спин)/RotZ; композиция RotY·RotX·RotY·RotZ.
    #
    # ★ ДВА РАЗНЫХ СДВИГА (реверс 2026-08-07, jeb_emitters_0x14_0x19.md). Дополнительные поля втыкаются
    #   в запись в ДВУХ разных местах, и путать их нельзя:
    #     atlas_sh — пара atlasCols/atlasRows@0x88/0x8C, вставлена ПЕРЕД endMode ⇒ сдвигает и endMode тоже;
    #     sh       — полный сдвиг всех каналов от duration и дальше (atlas_sh + 4, если есть billboardMode).
    #   billboardMode (когда есть) лежит СРАЗУ ПОСЛЕ endMode, т.е. на 0x8C+atlas_sh.
    #     | тип        | atlasCols/Rows | endMode | billboardMode | duration | sh | atlas_sh |
    #     | 0xB / 1    | —              | 0x88    | —             | 0x8C     |  0 |    0     |
    #     | 0xC / 2    | —              | 0x88    | 0x8C          | 0x90     |  4 |    0     |
    #     | 0x13/0x18  | 0x88/0x8C      | 0x90    | —             | 0x94     |  8 |    8     |
    #     | 0x14/0x19  | 0x88/0x8C      | 0x90    | 0x94          | 0x98     | 12 |    8     |
    #   Подтверждено ДВАЖДЫ: (1) порядок node-оффсетов в лоадерах effectLoadNode_Type19@0x11795B0 (0x13),
    #   sceneNodeLoadType20@0x1179A10 (0x14), effectEmitterLoadFromFile@0x117B1F0 (0x18),
    #   sceneLoadMeshNodeType25@0x117BA00 (0x19) — у всех node+1208(endMode) идёт ПОСЛЕ пары атласа и
    #   ПЕРЕД node+1596/+0x63C(billboardMode) и node+288(duration); (2) сырыми байтами реальных .jeb
    #   (bali_a_co_ma_01 bil1 0x14: 4,4,3,2,0.8 | ch1_a_4th_ac_01_1 BS_001 0x19: 4,4,0,2,1.4).
    #   БЫЛ БАГ: endMode читался по r+0x88 без atlas_sh ⇒ у 0x13/0x18/0x14/0x19 возвращался atlasCols.
    def tri(o): return [_f(d,r+o+sh), _f(d,r+o+sh+4), _f(d,r+o+sh+8)]     # start/END/var
    out = {
        "singleQuad": True, "growQuad": True,
        "endMode": _u32(d,r+0x88+atlas_sh),                      # 0=стоп на END, 1=луп-реинит, 3=hold (2 не сущ.)
        "life":       [_f(d,r+0x8C+sh), _f(d,r+0x90+sh)],        # duration base/var, сек (кламп 0.1..10)
        "colorStart": [_f(d,r+0x94+sh),_f(d,r+0x98+sh),_f(d,r+0x9C+sh)],
        "colorEnd":   [_f(d,r+0xA0+sh),_f(d,r+0xA4+sh),_f(d,r+0xA8+sh)],
        "alpha":  tri(0xB8),
        "scaleX": tri(0xC4),                                     # полу-экстент X (halfW)
        "scaleY": tri(0xD0),                                     # полу-экстент Z (halfH)
        "rotY1": tri(0xDC), "rotX": tri(0xE8), "rotY": tri(0xF4), "rotZ": tri(0x100),
        "uScroll": tri(0x10C), "vScroll": tri(0x118),            # СИДЫ (аккум только при kf-узлах)
        "uvTile": [_f(d,r+0x124+sh), _f(d,r+0x128+sh)],
        "basePosStart": _v3(d,r+sh,0x12C,0x130,0x134),           # basePos-канал: СЫРОЙ МИР
        "basePosEnd":   _v3(d,r+sh,0x138,0x13C,0x140),
        "posStart":     _v3(d,r+sh,0x150,0x154,0x158),           # pos-канал: орбитальный
        "posEnd":       _v3(d,r+sh,0x15C,0x160,0x164),
        "conf": "C (лоадер-трейс, 2026-07-21)",
    }
    if sh - atlas_sh == 4:
        # 0=нет, 1/3=сферический биллборд (transpose XZ), 2=lookAt с up=(0,1,0); 3 меняет порядок умножения.
        # 0xC/2 → 0x8C; 0x14/0x19 → 0x94 (после атласа и endMode).
        out["billboardMode"]=_u32(d,r+0x8C+atlas_sh)
    return out

def extract_mesh12(d, r):
    # Меш-семейство 0x11/0x12/0x1F (CEffectMesh, лоадер sceneLoadMeshNodeType18@0x1178530). jem@0x88.
    # W = Scale(0x128/134/140) · RotX(0x14C) · RotY(0x158) · RotZ(0x164) · parent; трансляция += offsetWorld(0x190,
    # сырой мир) + parent; локальный pos(0x1B4) вращается матрицей. jewels_a_00: dur=10, endMode=1, scale 0.9,
    # rotY 0→360, локальный pos (0,20,0).
    def tri(o): return [_f(d,r+o), _f(d,r+o+4), _f(d,r+o+8)]     # start/END/var
    return {
        "mesh12": True,
        "endMode": _u32(d,r+0xEC),
        "life":  [_f(d,r+0xF0), _f(d,r+0xF4)],
        "colorStart": [_f(d,r+0xF8),_f(d,r+0xFC),_f(d,r+0x100)],
        "colorEnd":   [_f(d,r+0x104),_f(d,r+0x108),_f(d,r+0x10C)],
        "alpha":  tri(0x11C),
        "scaleX": tri(0x128), "scaleYm": tri(0x134), "scaleZ": tri(0x140),
        "rotX": tri(0x14C), "rotY": tri(0x158), "rotZ": tri(0x164),
        "offsetWorldStart": _v3(d,r,0x190,0x194,0x198),          # сырой мир
        "offsetWorldEnd":   _v3(d,r,0x19C,0x1A0,0x1A4),
        "posStart": _v3(d,r,0x1B4,0x1B8,0x1BC),                  # локальный (вращается матрицей)
        "posEnd":   _v3(d,r,0x1C0,0x1C4,0x1C8),
        "conf": "C (лоадер-трейс 0x1178530, 2026-07-21)",
    }

def extract_legacy(d, r, t):
    m = LEGACY[t]
    out = {
        "period": _f(d,r+m["period"]),                                   # период эмиттера (сек); rate = node[49]
        "sizeBase": _f(d,r+m["sizeBase"]), "sizeTarget": _f(d,r+m["sizeTgt"]), "sizeRange": _f(d,r+m["sizeRng"]),
        "colorBase": [_f(d,r+m["cR"]), _f(d,r+m["cG"]), _f(d,r+m["cB"])],
        "coneAxis": [_f(d,r+m["coneX"]), 0.0, _f(d,r+m["coneZ"])],        # X,(Y нет в legacy-fixed),Z
        "alpha": [_f(d,r+m["aCur"]), _f(d,r+m["aMax"])],                  # cur, max
        "posBase": [_f(d,r+m["pX"]), _f(d,r+m["pY"]), _f(d,r+m["pZ"])],
        "sizeYcur": _f(d,r+m["sY"]),
        "endMode": _u32(d,r+m["end"]),
        "velFromDisk": False,
        "conf": "C/V" if t in (0xB,0xC,0xE) else "C",
    }
    if m["sX"] is not None: out["sizeXcur"] = _f(d,r+m["sX"])
    if t == 0xE: out["line"] = True
    if t == 0xB:
        # 0xB = single-quad keyframe-биллборд (частиц НЕ спавнит; verification §4, live spawnRate=0).
        # Цвет: track-node RGB=node[19..21]; FALLBACK без узлов: RGB @0xA0/0xA4/0xA8, alpha @0x104.
        out["singleQuad"] = True
        out["quadColorFallback"] = [_f(d,r+0xA0), _f(d,r+0xA4), _f(d,r+0xA8)]
        out["quadAlphaFallback"] = _f(d,r+0x104)
    return out

def parse_jeb(data):
    if len(data) < 0x4C: return None
    cnt = _u32(data,0x0C)
    if cnt == 0 or cnt > 4096: return None
    types = [_u32(data,0x4C+4*i) for i in range(cnt)]
    # header-анимация холдера (аудит §12): duration@0x08 (сек), pos s/e/v (vec3 @0x10/0x1C/0x28),
    # rotY start/end/var @0x34/0x38/0x3C (град), endMode@0x40. angle0=start±var; speed=(end−angle0)/duration.
    header = {"duration": _f(data,0x08),
              "posStart": _v3(data,0,0x10,0x14,0x18), "posEnd": _v3(data,0,0x1C,0x20,0x24), "posVar": _v3(data,0,0x28,0x2C,0x30),
              "rotY": [_f(data,0x34), _f(data,0x38), _f(data,0x3C)],
              "endMode": _u32(data,0x40)}
    ro = 0x4C + 4*cnt
    emitters = []
    for i in range(cnt):
        t = types[i]; sz = rec_size(data, ro, t)
        if sz is None or ro+sz > len(data): break
        em = {"idx": i, "type": t, "name": _cstr(data,ro,32),
              "tex": _cstr(data,ro+0x24,100) if (t in SINGLE or t in HYBRID) else "",
              "startDelay": _f(data,ro+0x20),               # задержка старта эмиттера, сек (аудит §12 №11)
              "mesh": t in MESH_TYPES}
        if em["mesh"]:
            em["jem"] = _cstr(data,ro+0x88,100)                 # имя .jem @record+0x88 (jem-mesh-format)
        if t in (0xD, 3):                 p = extract_modern(data, ro)
        elif t == 0x1B:                   # АУДИТ 2026-07-27: 0xD-раскладка со сдвигом +8 после atlas@0x88/0x8C
            p = extract_modern(data, ro, 8)
            p["atlasCols"] = _u32(data,ro+0x88); p["atlasRows"] = _u32(data,ro+0x8C)
        elif t == 0xE:                    p = extract_ring(data, ro)       # кольцо-лента (§11)
        elif t == 4:
            # ★ РЕВЕРС 2026-08-07: тип 4 = кольцо 0xE БЕЗ keyframe-хвоста (SINGLE 0x18C).
            #   Доказательства: (1) sceneNodeLoadType4@0x1171EA0 пишет РОВНО тот же порядок node-слотов,
            #   что и 0xE (включая ring-блок +0x11D4/11E0/11EC = cylHeight/upCycle/downCycle) и НЕ пишет
            #   halfH; (2) арифметика: контент 0xE = 0x190−4(kfCount) = 0x18C = размер записи типа 4.
            p = extract_ring(data, ro)
        elif t == 0x15:
            # ★ РЕВЕРС 2026-08-07: 0x15 = кольцо + пара atlasCols/Rows@0x88/0x8C, БЕЗ kf-хвоста.
            #   0x18C + 8 = 0x194 (размер записи, точно). Лоадер sceneNodeLoadType21@0x1179E80.
            p = extract_ring(data, ro, 8)
            p["atlasCols"] = _u32(data,ro+0x88); p["atlasRows"] = _u32(data,ro+0x8C)
        elif t == 0x1A:
            # ★ РЕВЕРС 2026-08-07: 0x1A = кольцо + атлас + kf-хвост 0xB8 (как у 0xE).
            #   0x18C + 8 + 4(kfCount) = 0x198 fixed (точно). Лоадер sceneLoadNodeType26@0x117C220.
            p = extract_ring(data, ro, 8)
            p["atlasCols"] = _u32(data,ro+0x88); p["atlasRows"] = _u32(data,ro+0x8C)
        elif t == 0x20:                   p = extract_type20(data, ro)     # партикл-эмиттер класса 0xE0C0
        elif t == 0xB:                    p = extract_wave(data, ro)       # keyframe-квад (истинная раскладка)
        elif t == 0xC:                    p = extract_wave(data, ro, 4)    # квад + billboardMode@0x8C (сдвиг +4)
        elif t == 1:                      p = extract_wave(data, ro)       # АУДИТ: тип 1 = копия 0xB-квада без kf (T01 @0x1170EE0)
        elif t == 2:                      p = extract_wave(data, ro, 4)    # ★ тип 2 = тип 1 + поле@0x8C (SPEC_DISKMAP_ALL §1: resLoader_readNode2FromStream@0x1180540, fixed 0x178, «раскладка идентична типу 1 со сдвигом»); класс B QUAD-ORIENTED = близнец 0xC ⇒ поле@0x8C = billboardMode (sh=4 его и читает). Нужен cmchar01 (эффект появления, CRG_F00/F01/SW00)
        elif t in (0x13, 0x18):           # вода-флипбук/квад-атлас = 0xB-раскладка со сдвигом +8 после atlas
            p = extract_wave(data, ro, 8, 8)
            p["atlasCols"] = _u32(data,ro+0x88); p["atlasRows"] = _u32(data,ro+0x8C)
        elif t in (0x14, 0x19):
            # ★ РЕВЕРС 2026-07-31 (weapon_glow_re.md §D), уточнён 2026-08-07 (jeb_emitters_0x14_0x19.md):
            #   0x14 = SINGLE 0x180, 0x19 = HYBRID (0x184, kf 0xA0). Иерархия классов из ctor'ов:
            #   0xB→0x18 (+atlasCols/Rows), 0xC→0x19 (+atlasCols/Rows); 0x14 = «0x19 без kf-хвоста».
            #   Раскладка = 0xB со сдвигом +12: пара atlas@0x88/0x8C ПЕРЕД endMode@0x90, затем
            #   billboardMode@0x94 ПОСЛЕ него (см. таблицу в шапке extract_wave — оба сдвига разные).
            #   Арифметика записи: 0xB-контент 0x174 + 12 = 0x180 (=0x14) и +4 kfCount = 0x184 (=0x19).
            #   Рендер (vtbl[15]): 0x19 → spriteBuildTransformAndDraw@0x120CF10 — БУКВАЛЬНО функция 0xC;
            #   0x14 → effectEmitterRenderBillboardQuad@0x123F3E0 — та же функция минус UV-скролл-матрица.
            #   Геометрия у обоих = квад в плоскости XY (V=(∓halfW,±halfH,0)), roll-канал@0xDC идёт в
            #   RotZ (спин вокруг нормали спрайта), rot2/3/4 = RotX/RotY/RotZ — идентично 0xC.
            #   ⇒ отдельный рендерер НЕ нужен, конверсия = ровно ветка 0xC + атлас-раскадровка.
            p = extract_wave(data, ro, 12, 8)
            p["atlasCols"] = _u32(data,ro+0x88); p["atlasRows"] = _u32(data,ro+0x8C)
        elif t == 0x17:                   p = extract_t17(data, ro)        # grid-частицы «блёстки» крыльев (§D, атлас-UV)
        elif t in (0x11, 0x12, 0x1F):     p = extract_mesh12(data, ro)     # меш-каналы (jewels; jem@0x88)
        elif t in LEGACY:                 p = extract_legacy(data, ro, t)  # (остаточные LEGACY-типы)
        else:                             p = {"conf": "—(тип 0x%X не реализован)"%t}
        if t in HYBRID:
            tn = read_track_nodes(data, ro, t)
            if tn: p["trackNodes"] = tn                                   # 58f-узлы (0xD/0x1B: готовые; 0xB/0xC/0x18: 40dw→232 при загрузке)
        em["params"] = p
        emitters.append(em); ro += sz
    # Бленд пер-контейнер: диск+1 = D3DBLEND (лоадер effects_particles.c:113269). Матрица в данных карт
    # (скан 2026-07-28): dst=1(ONE)=additive [src 1/2/4]; dst=5(INVSRCALPHA)=классическая альфа (alpenjewels);
    # dst=3(INVSRCCOLOR, src=5=INVSRCALPHA)=мультипликативное затемнение «тёмный дым» (flammio-a_09/10,
    # worker02-a_00/01): Final = srcC×(1−srcA) + dst×(1−srcC) → в UE пара секций Modulate+Additive.
    bd = _u32(data,0x48)
    mode = "add" if bd==1 else ("mod" if bd==3 else "alpha")
    return {"objectCount": cnt, "blendSrc": _u32(data,0x44), "blendDst": bd,
            "blendAdditive": bd==1, "blendMode": mode, "header": header,
            "emitters": emitters, "parsedAll": len(emitters)==cnt}

_zip = None
def parse_effect(name):
    global _zip
    if _zip is None: _zip = zipfile.ZipFile(BNZ)
    low = {n.lower(): n for n in _zip.namelist()}
    real = low.get((name+".jeb").lower())
    return parse_jeb(_zip.read(real)) if real else None

if __name__ == "__main__":
    import io
    if "--validate" in sys.argv:
        # эталонные значения = effects_system_verification.md (дизасм ридера из live.dmp + живые эмиттеры)
        allok = True
        def chk(lbl, got, exp, tol=0.5):
            global_ok = all(abs(got[k]-exp[k])<tol for k in exp)
            print("%s: %s"%(lbl,"✓"if global_ok else "✗"), {k:round(got[k],2) for k in exp}); return global_ok
        # 0xD smoke02 (verification §1): coneAngle=90°, sizeX 20→50, speed birth=30, rate=15
        p = parse_effect("eff_0_mon_99_smoke02-a_00")["emitters"][0]["params"]
        allok &= chk("0xD smoke02", {"coneAngleB":p["coneAngle"][0],"sizeXB":p["sizeX"][0],"sizeXD":p["sizeX"][1],
                    "speedB":p["speed"][0],"rate":p["spawnRate"][0],"alphaB":p["alpha"][0]},
                    {"coneAngleB":90.0,"sizeXB":20.0,"sizeXD":50.0,"speedB":30.0,"rate":15.0,"alphaB":1.0})
        # 0xD alpen_smoke Object1 (live-эмиттер 48/48): period=5, sizeX 75→300, speed 300, partLife 2.5, rate 1
        r = parse_effect("eff_mdel_alpen_smoke_00")
        p1 = next(e["params"] for e in r["emitters"] if e["name"].lower().startswith("object1") and e["type"]==0xD)
        allok &= chk("0xD alpen_smoke Obj1", {"period":p1["period"],"sizeXB":p1["sizeX"][0],"sizeXD":p1["sizeX"][1],
                    "speedB":p1["speed"][0],"partLife":p1["partLife"][0],"rate":p1["spawnRate"][0],"coneB":p1["coneAngle"][0]},
                    {"period":5.0,"sizeXB":75.0,"sizeXD":300.0,"speedB":300.0,"partLife":2.5,"rate":1.0,"coneB":30.0})
        # 0xB glow_003 (verification §4): single-quad, fallback RGB=(1,1,0) жёлтый, период 0x8C=4
        p = parse_effect("eff_glow_003")["emitters"][0]["params"]
        allok &= chk("0xB glow_003", {"qR":p["quadColorFallback"][0],"qG":p["quadColorFallback"][1],
                    "qB":p["quadColorFallback"][2],"period":p["period"]}, {"qR":1.0,"qG":1.0,"qB":0.0,"period":4.0})
        okQ = p.get("singleQuad") is True; allok &= okQ
        print("0xB singleQuad-флаг: %s"%("✓"if okQ else "✗"))
        # 0xE ches_water: ПЕРИОДЫ 2/3/5/7 (переинтерпретировано: +0x120=период, не rate)
        r = parse_effect("eff_ches_water_a_01"); pers=[e["params"].get("period") for e in r["emitters"] if e["type"]==0xE]
        okE = pers[:4]==[2.0,3.0,5.0,7.0]; allok &= okE
        print("0xE ches_water периоды: %s"%("✓"if okE else "✗"), pers[:6])
        print("=== ИТОГ: %s ==="%("✓ ВСЁ СОВПАЛО (verification-эталон)"if allok else "✗ ЕСТЬ РАСХОЖДЕНИЯ"))
        sys.exit(0 if allok else 1)
    for nm in (sys.argv[1:] or ["eff_glow_004","eff_chesslight_a_00","eff_ches_water_a_01"]):
        r = parse_effect(nm)
        if not r: print("%-26s НЕТ .jeb"%nm); continue
        print("%-26s objs=%d add=%s parsedAll=%s"%(nm,r["objectCount"],r["blendAdditive"],r["parsedAll"]))
        for e in r["emitters"][:4]:
            print("   [%d] 0x%-2X %-14s tex=%-22s %s"%(e["idx"],e["type"],e["name"][:14],e["tex"][:22],
                  {k:(round(v,1) if isinstance(v,float) else v) for k,v in e["params"].items() if k in ("rate","sizeBase","coneDir","colorBase","conf")}))
