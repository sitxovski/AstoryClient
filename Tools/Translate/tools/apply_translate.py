# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
apply_translate.py — сборка и применение перевода в клиент AsdaChroniclesInterlude_MINE.

1. Репак Data\Character\Bin.bbz: оригинальный ZIP, заменённые entry из out_client\_bbz_entries
   (deflate, имена entry 1:1 как в оригинале).
2. Применение в клиент С БЭКАПАМИ (<файл>.translate_bak, существующий бэкап не перезаписывается):
     Data\Character\Bin.bbz, system\error list.bin, loose Data\*.BIN, Data\NPC\Episode\Talk\*.BIN
3. Пост-проверка: чтение применённых файлов, структура + MD5-семплы.
"""
import os, sys, shutil, struct, zipfile, hashlib

T    = r"@@ASTORY_DEV_ROOT@@\Donors\TRANSLATE_unpacked"
OUT  = os.path.join(T, "out_client")
OUT_BBZ = os.path.join(OUT, "_bbz_entries")
CLIENT  = r"@@ASTORY_DEV_ROOT@@\Donors\AsdaChroniclesInterlude_MINE"
ORIG_BBZ = os.path.join(CLIENT, "Data", "Character", "Bin.bbz")
NEW_BBZ  = os.path.join(OUT, "Data", "Character", "Bin.bbz")

def log(*a): print(*a, flush=True)

# --- 1. репак Bin.bbz ---
os.makedirs(os.path.dirname(NEW_BBZ), exist_ok=True)
mod = {fn.lower(): os.path.join(OUT_BBZ, fn) for fn in os.listdir(OUT_BBZ)}
zin = zipfile.ZipFile(ORIG_BBZ, "r")
replaced = kept = 0
with zipfile.ZipFile(NEW_BBZ, "w", zipfile.ZIP_DEFLATED) as zout:
    for info in zin.infolist():
        low = info.filename.lower()
        if low in mod:
            data = open(mod[low], "rb").read()
            replaced += 1
        else:
            data = zin.read(info)
            kept += 1
        zi = zipfile.ZipInfo(info.filename, date_time=info.date_time)
        zi.compress_type = zipfile.ZIP_DEFLATED
        zi.external_attr = info.external_attr
        zout.writestr(zi, data)
zin.close()
log("[bbz] репак: заменено entry=%d, скопировано=%d -> %s (%.1f МБ)" %
    (replaced, kept, NEW_BBZ, os.path.getsize(NEW_BBZ)/1048576))

# санити: новый архив читается, count item_list на месте
z = zipfile.ZipFile(NEW_BBZ)
d = z.read("item_list.bin")
cnt = struct.unpack_from('<i', d, 4)[0]
rec = d[20:20+264]
assert hashlib.md5(rec[16:]).digest() == rec[:16], "md5 rec0 item_list после репака!"
assert cnt == 58878, cnt
nm = bytes(((b-1) & 0xFF) for b in d[20+16+4:20+16+68].split(b'\0')[0]).decode('cp1251')
log("[bbz] санити: item_list count=%d rec0='%s' md5 OK" % (cnt, nm))
z.close()

# --- 2. применение с бэкапами ---
def apply_file(src, dst):
    if not os.path.isfile(dst):
        log("  ! цели нет, пропуск:", dst); return 0
    bak = dst + ".translate_bak"
    if not os.path.isfile(bak):
        shutil.copy2(dst, bak)
    shutil.copy2(src, dst)
    return 1

n = 0
n += apply_file(NEW_BBZ, ORIG_BBZ)
n += apply_file(os.path.join(OUT, "system", "error list.bin"),
                os.path.join(CLIENT, "system", "error list.bin"))
# loose + talk: всё из out_client/Data, кроме Character (bbz уже применён)
for root, _, files in os.walk(os.path.join(OUT, "Data")):
    for fn in files:
        src = os.path.join(root, fn)
        rel = os.path.relpath(src, os.path.join(OUT, "Data"))
        if rel.lower().startswith("character"):
            continue
        n += apply_file(src, os.path.join(CLIENT, "Data", rel))
log("[apply] файлов применено: %d (бэкапы *.translate_bak)" % n)

# --- 3. пост-проверка на живом клиенте ---
z = zipfile.ZipFile(ORIG_BBZ)
d = z.read("skilltooltip.bin")
cnt = struct.unpack_from('<i', d, 4)[0]
s = d[20+4:20+36].split(b'\0')[0].decode('cp1251', 'replace')
log("[check] клиент Bin.bbz: skilltooltip count=%d rec0.Name='%s'" % (cnt, s))
z.close()
d = open(os.path.join(CLIENT, "system", "error list.bin"), "rb").read()
cnt = struct.unpack_from('<I', d, 0)[0]
assert len(d) == 4 + cnt*256 + cnt*4
log("[check] клиент error list.bin: count=%d размер OK" % cnt)
d = open(os.path.join(CLIENT, "Data", "NPC", "npc_list.BIN"), "rb").read()
s = d[20+4:20+80].split(b'\0')[0].decode('cp1251', 'replace')
log("[check] клиент npc_list rec0='%s'" % s)
log("ГОТОВО.")
