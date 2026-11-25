# Copyright (c) 2026 @sitxovski (Discord)
# Строит mob_manifest.json для каждой карты: серверные MonsterData_Info\<Map>.svr (авторские позиции,
#   те же файлы читает СЕРВЕР — 1:1 с рантаймом) + резолв модели (Mob list.BIN, поле **+0x04**) +
#   высота Y (★ из ЛОГИЧЕСКОЙ сетки <map>.TER, как NPC-манифест;
#   фолбэк на сэмпл рендер-меша только для карт без .TER).
# Формат .svr (65_npc_mob_placement.md §2.2, GameServer LoadMonster@19463): u32 count@0 + stride 0x4A:
#   +0 idx u16, +4 tileX u16, +8 tileZ u16, +0xC regen u32, +0x10 type u16, +0x14 group i32 (-1=solo),
#   +0x20 roam-rect 4×u32 [minX,minZ,maxX,maxZ] (тайлы), +0x31 путь патруля u16-пары (≤5, wp0=спавн),
#   +0x49 enable u8. Мир = тайл*100+50.
# Запуск (вне UE): python asda_build_mob_manifest.py
import struct, os, json, glob, sys, importlib.util
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P

HERE      = os.path.dirname(os.path.abspath(__file__))
GAME      = _P.DATA
MOB_LIST  = GAME + r"\Mob\Mob list.BIN"
SVR_DIR   = _P.MONSTER_DATA_INFO
MAPRAW_DIR= _P.EXPORT_MAPS

# Высота земли — только asda_ter_height через NM.TERH (закон ground-height-from-ter).
spec = importlib.util.spec_from_file_location("NM", os.path.join(HERE, "asda_build_npc_manifest.py"))
NM = importlib.util.module_from_spec(spec); sys.modules["NM"] = NM
# NB: у NPC-манифеста main() под if __name__ — импорт безопасен.
spec.loader.exec_module(NM)

# ★ 11.10.2026: пара nightvalley→Night_Valley НЕВЕРНА (сверка wave0_t2_nightvalley.md: клиентская nightvalley = серверная
#   LostValley, Night_Valley = alpen_dungeon). Сейчас nightvalley пропускается целиком (реестр карт, "mob_bake_off" —
#   решение владельца), поэтому пара не действует; при снятии запрета брать имя из server.mapinfo_name реестра.
SVR_OVERRIDE = {"silalis": "Sillaris", "nightvalley": "Night_Valley", "dragonlair": "DragonLaiR",
                "Mountain Flamion": "MountainFlamion", "castle_of_chaos": "Castle_of_Chaos",
                "firewayforest": "FirewayForest", "StagnantDesert": "StagnantDesert"}

def parse_mob_list(path):
    b = open(path, "rb").read()
    count = struct.unpack_from("<I", b, 4)[0]
    stride, base = 0x110, 0x14
    out = {}
    for i in range(count):
        r = base + i*stride
        if r + stride > len(b): break
        rid = struct.unpack_from("<H", b, r)[0]
        # ★ МОДЕЛЬ — поле +0x04, НЕ +0x24 (исправлено 2026-08-02).
        #   Эталон CMap_LoadMOB@0xF71A50 (world_map.c): qmemcpy(spawnParams, tbl + 272*type + 4, 32);
        #   String_Append(&FileName, ".jbm"); dataMgr_EnsureMeshFile(..., MobModel.bnz, FileName, ...).
        #   Поле +0x24 — база КЛИПОВ .jbb (Effect_BuildJbbSuffixByType@0xF717C0), +0x44 — база звука.
        #   У 117 из 854 записей поля различаются; на данных: число сабмешей .jbm совпадает с числом
        #   пар в <map>.MLK у поля +0x04 в 703/705 случаев, у +0x24 — лишь в 650/705.
        model = b[r+0x04:r+0x24].split(b"\0")[0].decode("latin1", "ignore")
        out[rid] = model
    return out

def find_svr(mapname):
    cand = SVR_OVERRIDE.get(mapname, mapname)
    for n in (cand, mapname, mapname.lower(), cand.lower()):
        p = os.path.join(SVR_DIR, n + ".svr")
        if os.path.isfile(p): return p
    # нормализация: lower, без _/пробелов
    norm = lambda s: s.lower().replace("_", "").replace(" ", "")
    want = norm(mapname)
    for f in os.listdir(SVR_DIR):
        if f.lower().endswith(".svr") and norm(f[:-4]) == want:
            return os.path.join(SVR_DIR, f)
    return None

def parse_svr(path):
    b = open(path, "rb").read()
    count = struct.unpack_from("<I", b, 0)[0]
    stride = 0x4A
    recs = []
    for i in range(count):
        r = 4 + i*stride
        if r + stride > len(b): break
        idx   = struct.unpack_from("<H", b, r)[0]
        tx    = struct.unpack_from("<H", b, r+0x04)[0]
        tz    = struct.unpack_from("<H", b, r+0x08)[0]
        regen = struct.unpack_from("<I", b, r+0x0C)[0]
        tid   = struct.unpack_from("<H", b, r+0x10)[0]
        grp   = struct.unpack_from("<i", b, r+0x14)[0]
        roam  = struct.unpack_from("<4I", b, r+0x20)
        wps   = []
        for k in range(5):
            wx, wz = struct.unpack_from("<HH", b, r+0x31+k*4)
            if wx == 0 and wz == 0: break
            wps.append([wx, wz])
        enable = b[r+0x49]
        recs.append((idx, tid, tx, tz, regen, grp, roam, wps, enable))
    return recs

def main():
    moblist = parse_mob_list(MOB_LIST)
    maps = sorted(glob.glob(os.path.join(MAPRAW_DIR, "*.mapraw")))
    # ★ 11.10.2026: карты с запретом запекания мобов (реестр, "mob_bake_off") — манифест НЕ строится и прежний не трогается.
    import asda_map_registry as _REG
    bake_off = {k.lower(): v for k, v in _REG.mob_bake_off().items()}
    summary = []
    for mp in maps:
        mapname = os.path.splitext(os.path.basename(mp))[0]
        if mapname.lower() in bake_off:
            print("-- %s: пропуск, mob_bake_off: %s" % (mapname, bake_off[mapname.lower()]))
            summary.append((mapname, "SKIP mob_bake_off", 0, 0)); continue
        svr = find_svr(mapname)
        if not svr:
            summary.append((mapname, "NO .svr", 0, 0)); continue
        recs = parse_svr(svr)
        if not recs:
            summary.append((mapname, os.path.basename(svr), 0, 0)); continue
        th = NM.TERH.get(mapname)           # ★ ЕДИНСТВЕННЫЙ источник высоты — .TER (см. шапку asda_build_npc_manifest.py)
        if th is None:                      # карта без .TER — не угадываем высоту
            print("!! %s: нет .TER — манифест НЕ строится" % mapname)
            summary.append((mapname, os.path.basename(svr), 0, 0)); continue
        out = []
        for idx, tid, tx, tz, regen, grp, roam, wps, enable in recs:
            model = moblist.get(tid)
            wx, wz = tx*100+50, tz*100+50
            gy = th.GroundY(wx, wz)
            out.append({"idx": idx, "type": tid, "model": model, "gx": wx, "gy": round(gy, 3), "gz": wz,
                        "tileX": tx, "tileZ": tz, "regen": regen, "group": grp,
                        "roam": list(roam), "patrol": wps, "enable": enable})
        outdir = os.path.join(MAPRAW_DIR, mapname)
        os.makedirs(outdir, exist_ok=True)
        json.dump({"map": mapname, "svrFile": os.path.basename(svr), "count": len(out), "mob": out},
                  open(os.path.join(outdir, "mob_manifest.json"), "w", encoding="utf-8"),
                  ensure_ascii=False, indent=1)
        nomodel = sum(1 for o in out if not o["model"])
        summary.append((mapname, os.path.basename(svr), len(out), nomodel))
    print("%-22s %-24s %6s %8s" % ("map", ".svr", "count", "no_model"))
    for m, f, c, nm in summary:
        print("%-22s %-24s %6s %8s" % (m, f, c, nm))

if __name__ == "__main__":
    main()
