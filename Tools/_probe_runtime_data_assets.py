# Copyright (c) 2026 @sitxovski (Discord)
# _probe_runtime_data_assets.py — регресс-проверка перевода рантайма на ассеты:
#   DT_RideEffect / DT_PlusPackage / DT_ServerList сверяются С ИСХОДНЫМИ .bin БАЙТ-В-БАЙТ,
#   плюс проверяется, что имена эффектов/звуков резолвятся в реальные ассеты проекта.
# Только ЧТЕНИЕ. Отчёт → Reports/runtime_data_assets_probe.txt
import csv as _csv
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import os
import struct

import unreal

OUT = r"@@ASTORY_DEV_ROOT@@\Reports\runtime_data_assets_probe.txt"
RIDE_BIN = r"@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Data\Character\Ride_effect_table.bin"
PLUS_BIN = r"@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Data\Character\PlusPackageTable.BIN"
OPSET = [_P.OPSET_BIN,
         _P.OPSET_BIN_ORIG]


def dt_rows(path):
    dt = unreal.EditorAssetLibrary.load_asset(path)
    if not dt:
        return None, None
    lines = [l for l in unreal.DataTableFunctionLibrary.export_data_table_to_csv_string(dt).splitlines() if l.strip()]
    recs = list(_csv.reader(lines))
    return recs[0], {r[0]: r for r in recs[1:] if r}


def col(hdr, rec, name):
    for i, h in enumerate(hdr):
        if h.strip().lower() == name.lower() and i < len(rec):
            return rec[i]
    return ""


def strip_ext(s, exts):
    s = s.lower()
    for e in exts:
        if s.endswith(e):
            return s[:-len(e)]
    return s


def main():
    L = []

    # ── 1. DT_RideEffect vs Ride_effect_table.bin ──
    b = open(RIDE_BIN, "rb").read()
    cnt = struct.unpack_from("<i", b, 4)[0]
    ref = {}
    for i in range(cnt):
        r = 20 + 260 * i
        code = struct.unpack_from("<i", b, r)[0]

        def s(o):
            return strip_ext(b[o:o + 64].split(b"\0")[0].decode("latin1"), (".jeb", ".wav"))
        ref[str(code)] = [s(r + 4), s(r + 68), s(r + 132), s(r + 196)]
    hdr, rows = dt_rows("/Game/AsdaAssets/Data/DT_RideEffect")
    if rows is None:
        L.append("DT_RideEffect: НЕ НАЙДЕН !!")
    else:
        bad = [k for k in ref if k not in rows or
               [col(hdr, rows[k], c) for c in ("EffectName", "SoundName", "SoundName2", "SoundName3")] != ref[k]]
        L.append("DT_RideEffect: строк DT=%d, записей в .bin=%d, РАСХОЖДЕНИЙ=%d %s"
                 % (len(rows), len(ref), len(bad), bad[:10]))
        # ассеты эффектов/звуков
        miss_fx, miss_snd = [], []
        for k, v in ref.items():
            if v[0] and not unreal.EditorAssetLibrary.does_asset_exist("/Game/AsdaAssets/Effects/BP_Fx_%s" % v[0]):
                miss_fx.append(v[0])
            for sn in v[1:]:
                if sn and not unreal.EditorAssetLibrary.does_asset_exist("/Game/AsdaAssets/Sound/%s" % sn):
                    miss_snd.append(sn)
        L.append("  эффект-BP не найдено: %d уникальных %s" % (len(set(miss_fx)), sorted(set(miss_fx))[:8]))
        L.append("  звук-ассетов не найдено: %d уникальных %s" % (len(set(miss_snd)), sorted(set(miss_snd))[:8]))

    # ── 2. DT_PlusPackage vs PlusPackageTable.BIN ──
    b = open(PLUS_BIN, "rb").read()
    cnt = struct.unpack_from("<i", b, 4)[0]
    ref = {}
    for i in range(cnt):
        r = 20 + 140 * i
        wid, nm, nf = struct.unpack_from("<iii", b, r)
        ref[str(wid)] = [str(nm), str(nf),
                         strip_ext(b[r + 12:r + 76].split(b"\0")[0].decode("latin1"), (".jeb",)),
                         strip_ext(b[r + 76:r + 140].split(b"\0")[0].decode("latin1"), (".jeb",))]
    hdr, rows = dt_rows("/Game/AsdaAssets/Data/DT_PlusPackage")
    if rows is None:
        L.append("DT_PlusPackage: НЕ НАЙДЕН !!")
    else:
        bad = [k for k in ref if k not in rows or
               [col(hdr, rows[k], c) for c in ("NoticeIdMale", "NoticeIdFemale", "JebMale", "JebFemale")] != ref[k]]
        L.append("DT_PlusPackage: строк DT=%d, записей в .BIN=%d, РАСХОЖДЕНИЙ=%d %s"
                 % (len(rows), len(ref), len(bad), bad[:10]))

    # ── 3. DT_ServerList vs OPset.bin ──
    src = next((p for p in OPSET if os.path.isfile(p)), None)
    hdr, rows = dt_rows("/Game/AsdaAssets/Data/DT_ServerList")
    if rows is None:
        L.append("DT_ServerList: НЕ НАЙДЕН !!")
    elif not src:
        L.append("DT_ServerList: строк=%d (OPset.bin для сверки не найден)" % len(rows))
    else:
        d = bytes(((x - 0x64) & 0xFF) for x in open(src, "rb").read())

        def s(o, m=64):
            e = d.find(b"\0", o, o + m)
            return d[o:e if e >= 0 else o + m].decode("latin1")
        want = [s(0x004), s(0x104), s(0x204), s(0x304)]
        got = [col(hdr, r, "Group") for r in rows.values()][:1] + \
              [col(hdr, r, "IP") for r in rows.values()][:1] + \
              [col(hdr, r, "Port") for r in rows.values()][:1] + \
              [col(hdr, r, "Name") for r in rows.values()][:1]
        L.append("DT_ServerList: .bin=%s  DT=%s  СОВПАДАЕТ=%s" % (want, got, want == got))

    txt = "\n".join(L)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(txt)
    unreal.log_warning("[data-probe]\n%s" % txt)


if __name__ == "__main__":
    main()
