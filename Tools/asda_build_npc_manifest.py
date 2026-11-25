# Copyright (c) 2026 @sitxovski (Discord)
# Строит npc_manifest.json для каждой карты: резолв модели (npc_list.BIN) + позиции (.npc) +
# высота Y.
# ★ ВЫСОТА: с 2026-08-01 берётся из ЛОГИЧЕСКОЙ сетки <map>.TER (asda_ter_height, 1:1 эталон
#   terrainInterpHeightAtXY@0x1130F90 и UAsdaWalkGrid::GetGroundY), а НЕ трейсом/сэмплом по рендер-мешу.
#   Причина: там, где над оврагом стоит меш-площадка, пол уже запечён в .TER, а рендер-террейн уходит
#   вниз (до 2403 юнитов на alpen) — сэмпл по мешу ставил NPC/мобов под видимый пол (Reports/ground_height_fix.md).
#   Фолбэк-сэмпл по рендер-мешу (HeightSampler) УДАЛЁН 2026-08-01: .TER есть у ВСЕХ 22 карт (проверено ter_path()),
#   ветка была мёртвой; единственный источник высоты — asda_ter_height (закон ground-height-from-ter).
#   Бэкап старой версии: RE/_refactor_bak/2026-08-01/asda_build_npc_manifest.py.orig
# Запуск (system python, вне UE).
import struct, os, json, glob, sys, re
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_ter_height as TERH

GAME      = _P.DATA
NPC_LIST  = GAME + r"\NPC\npc_list.BIN"
NPC_DIR   = _P.NPC_DATA_INFO
MAPRAW_DIR= _P.EXPORT_MAPS

# UE/mapraw имя карты -> имя файла .npc (нормализация: lower, без _/пробелов; override для расхождений)
NPC_OVERRIDE = {
    "silalis": "Sillaris",   # транслитерация клиента != сервера
    # ★ 09.11.2026 (P119r): связь карта → файл NPC — по NpcServer\ini\MapInfo.ini (NonPlayerInfo_NNN), а НЕ по похожему имени:
    #   клиентская nightvalley — карта 5 = LostValley.npc (Forgotten Canyon), alpen_dungeon — карта 4 = Night_Valley.npc (Night Valley).
    #   Поиск по имени отдавал nightvalley → Night_Valley.npc: в уровень каньона попадал NPC 22 карты 4, а 10 NPC каньона — нет.
    "nightvalley": "LostValley",
    "alpen_dungeon": "Night_Valley",
}

def read_cstr(b, off, maxlen=64):
    end = b.find(b'\x00', off)
    if end == -1 or end-off > maxlen: end = off+maxlen
    return b[off:end].decode('latin1', 'ignore')

NPC_META = {}          # id -> {typefunc, x, z} из npc_list.BIN (заполняет parse_npc_list)
# ★ Флаг совместимости: ASDA_NPC_TABLE_XZ=0 возвращает прежнее поведение (XZ всегда из тайла).
TABLE_XZ = os.environ.get("ASDA_NPC_TABLE_XZ", "1") != "0"

def parse_npc_list(path):
    b = open(path,'rb').read()
    stride, base = 0x88, 0x14
    out = {}
    for i in range((len(b)-base)//stride):
        rec = base + i*stride
        rid = struct.unpack_from('<H', b, rec)[0]
        # ★★★ 2026-09-11: кроме модели забираем typefunc (+0x7C) и СОБСТВЕННЫЕ мировые X/Z (+0x70/+0x74).
        #   Оригинал (npcMgr_spawnWorldEntityFromTable @0xF90600, ветка `case 1: case 3: case 5:`)
        #   для этих трёх типов ИГНОРИРУЕТ тайл из спавн-пакета и берёт координаты ИЗ ЭТОЙ ТАБЛИЦЫ:
        #     worldEntityInitByType(..., *(float*)(g_NpcTextTable + 136*id + 112),
        #                                *(float*)(g_NpcTextTable + 136*id + 116), ...)
        #   Для типов 0/2/4 — tile*100+50, как у нас и было.
        tf = struct.unpack_from('<I', b, rec+0x7C)[0]
        tbx, tbz = struct.unpack_from('<ff', b, rec+0x70)
        out[rid] = read_cstr(b, rec+0x46)
        NPC_META[rid] = {"typefunc": tf, "x": float(tbx), "z": float(tbz)}
    return out

def parse_npc_file(path):
    b = open(path,'rb').read()
    stride = 0xDD
    recs=[]
    for i in range(len(b)//stride):
        r = i*stride
        # ★ тайлы SIGNED: сервер LoadNpcInfo (GameServer.exe.c:26489): tileX<0 || tileZ<0 → NPC ОТКЛЮЧЁН
        #   (запись пропускается, в мир не спавнится).
        idx, tid, tx, tz, face = struct.unpack_from('<HxxHxxhhH', b, r)  # +0,+4,+8(i16),+A(i16),+C
        recs.append((idx, tid, tx, tz, face))
    return recs

def norm(s): return re.sub(r'[^a-z0-9]','', s.lower())

def find_npc_file(mapname):
    if mapname.lower() in NPC_OVERRIDE:
        cand = os.path.join(NPC_DIR, NPC_OVERRIDE[mapname.lower()]+".npc")
        return cand if os.path.exists(cand) else None
    target = norm(mapname)
    for f in os.listdir(NPC_DIR):
        if f.lower().endswith(".npc") and norm(f[:-4])==target:
            return os.path.join(NPC_DIR, f)
    return None

def main():
    npclist = parse_npc_list(NPC_LIST)
    only = sys.argv[1] if len(sys.argv)>1 else None
    maps = sorted(glob.glob(os.path.join(MAPRAW_DIR,"*.mapraw")))
    summary=[]
    for mp in maps:
        mapname = os.path.basename(mp)[:-7]
        if only and mapname!=only: continue
        npcf = find_npc_file(mapname)
        if not npcf:
            summary.append((mapname,"NO .npc",0,0)); continue
        recs = parse_npc_file(npcf)
        if not recs:
            summary.append((mapname,os.path.basename(npcf),0,0)); continue
        th = TERH.get(mapname)              # ★ ЕДИНСТВЕННЫЙ источник высоты — .TER
        if th is None:                      # карта без .TER — не угадываем высоту, пропускаем с явным логом
            print("!! %s: нет .TER — манифест НЕ строится" % mapname)
            summary.append((mapname, os.path.basename(npcf), 0, 0)); continue
        out=[]; nfar=0
        for idx,tid,tx,tz,face in recs:
            model = npclist.get(tid)
            disabled = (tx < 0 or tz < 0)   # серверный критерий «выключен»
            wx,wz = tx*100+50, tz*100+50
            # ★★★ 2026-09-11: типы 1/3/5 берут XZ ИЗ npc_list, а не из тайла спавна
            #   (эталон npcMgr_spawnWorldEntityFromTable @0xF90600). Расхождение до 140 юнитов.
            _m = NPC_META.get(tid)
            if TABLE_XZ and _m and _m["typefunc"] in (1, 3, 5) and not disabled:
                wx, wz = _m["x"], _m["z"]
            # клампы оригинала (terrainGetCellByPos @0x95A840): X/Z вне [100, 51100] подтягиваются
            if not disabled:
                wx = 100.0 if wx < 100.0 else (51100.0 if wx > 51100.0 else wx)
                wz = 100.0 if wz < 100.0 else (51100.0 if wz > 51100.0 else wz)
            if disabled:      gy,how = 0.0, "disabled"
            else:             gy,how = th.GroundY(wx,wz), "ter"
            out.append({"idx":idx,"type":tid,"model":model,
                        "gx":wx,"gy":round(gy,3),"gz":wz,"face":face,"tileX":tx,"tileZ":tz,
                        "disabled":int(disabled)})
        outdir = os.path.join(MAPRAW_DIR, mapname)
        os.makedirs(outdir, exist_ok=True)
        json.dump({"map":mapname,"npcFile":os.path.basename(npcf),"count":len(out),"npc":out},
                  open(os.path.join(outdir,"npc_manifest.json"),"w",encoding="utf-8"), ensure_ascii=False, indent=1)
        nomodel = sum(1 for o in out if not o["model"])
        summary.append((mapname,os.path.basename(npcf),len(out),nomodel,nfar))
    print("%-22s %-22s %6s %8s %6s" % ("map",".npc","count","no_model","near"))
    for row in summary:
        if len(row)==4: m,f,c,nm=row; nf=0
        else: m,f,c,nm,nf=row
        print("%-22s %-22s %6s %8s %6s" % (m,f,c,nm,nf))

if __name__=="__main__":
    main()
