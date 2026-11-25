#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""
extract_sound_table.py

Reverse-engineers and extracts the original AsdaStory sound table CSoundFileTable
(Data\\Sound\\SFT.BIN) into a CSV mapping: numeric id -> wav file name.

Ground truth (etalon decompile @@ASTORY_DEV_ROOT_ESC@@\\RE\\etalon\\audio.c):
  CSoundFileTable_Load @ 0xFA5FE0:
    - path = <cwd>\\Data\\Sound\\SFT.BIN
    - fread(header, 0x14 /*20*/, 1)      -> 20-byte header
    - magic "SFT" compared via strncmp(...,4)
    - entryCount field in header
    - operator new[](204 * entryCount)   -> record stride = 0xCC = 204 bytes
    - fread(records, 0xCC, entryCount)
    - for entryIdx in [0..count): name = record + 204*entryIdx + 4
      map[entryIdx] = std::string(name)   -> KEY = sequential index

  SFT.BIN observed header (20 bytes):
    +0  u32  = 0            (unknown / reserved, always 0)
    +4  u32  = entryCount   (216 in this build)
    +8  u32  = 2            (version?)
    +12 char[4] = "SFT\0"   (magic, strncmp 4)
    +16 u32  = 0            (reserved)

  Record (204 bytes):
    +0   i32   id           (== sequential index in all observed data)
    +4   char[200] name     (null-terminated, INCLUDES ".wav"/".mp3" extension)

  GetNameById(id) @ 0xFA6390 -> map lookup by id -> name.
  CSound_BuildSamplePath @ 0xFA4DA0 -> <cwd>\\Data\\Sound\\ + name  (name already has ext)
"""
import os, struct, csv, sys
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P

SFT = _P.SFT_BIN
OUT = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Sound", "sound_table.csv")

HEADER_SIZE = 20
REC_STRIDE  = 204
NAME_OFF    = 4
NAME_MAX    = REC_STRIDE - NAME_OFF  # 200

def decode_name(raw: bytes) -> str:
    raw = raw.split(b"\x00", 1)[0]
    # Names are ASCII in this build; fall back to cp949 (Korean client encoding)
    try:
        return raw.decode("ascii")
    except UnicodeDecodeError:
        return raw.decode("cp949", errors="replace")

def main():
    with open(SFT, "rb") as f:
        data = f.read()

    if len(data) < HEADER_SIZE:
        sys.exit("file too small")

    reserved0 = struct.unpack_from("<I", data, 0)[0]
    count     = struct.unpack_from("<I", data, 4)[0]
    version   = struct.unpack_from("<I", data, 8)[0]
    magic     = data[12:16]

    if magic[:3] != b"SFT":
        sys.exit(f"bad magic: {magic!r}")

    expected = HEADER_SIZE + REC_STRIDE * count
    print(f"[SFT.BIN] size={len(data)} magic={magic!r} count={count} "
          f"version={version} reserved0={reserved0}")
    if expected != len(data):
        print(f"WARNING: expected size {expected} != actual {len(data)}")

    rows = []
    for i in range(count):
        off = HEADER_SIZE + REC_STRIDE * i
        rec = data[off:off + REC_STRIDE]
        rid = struct.unpack_from("<i", rec, 0)[0]
        name = decode_name(rec[NAME_OFF:NAME_OFF + NAME_MAX])
        if rid != i:
            print(f"NOTE: record {i} has field0={rid} (!= index)")
        rows.append((i, rid, name))

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", newline="", encoding="utf-8") as fo:
        w = csv.writer(fo)
        # Name column = RowName (== id) for UE DataTable import; id numeric; wav name
        w.writerow(["Name", "id", "wav"])
        for i, rid, name in rows:
            w.writerow([i, rid, name])

    print(f"[OUT] {OUT}  ({len(rows)} rows)")
    print("---- first 15 ----")
    for i, rid, name in rows[:15]:
        print(f"{i:4d} -> {name}")
    print(f"---- total {len(rows)} ----")

if __name__ == "__main__":
    main()
