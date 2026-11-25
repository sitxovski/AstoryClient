# Copyright (c) 2026 @sitxovski (Discord)
# _probe_await_maps.py — СВЕРКА уже залитых DT_TimList / DT_NmsList с байтами оригинальных .BIN.
#   Читает АССЕТЫ (а не JSON, из которого их делали) и сравнивает с независимым разбором файла.
#   Запуск: Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\_probe_await_maps.py
import json as _json
import os as _os
import struct
import sys as _sys

_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import unreal

TIM_BIN = _os.path.join(_P.DATA, "NPC", "TIM_List.BIN")
NMS_BIN = _os.path.join(_P.DATA, "NPC", "NMS_list.BIN")


def log(m):
    unreal.log_warning("[probe-await] %s" % m)


def dump(dt):
    try:
        txt = unreal.DataTableFunctionLibrary.export_data_table_to_json_string(dt)
    except AttributeError:
        txt = unreal.DataTableFunctionLibrary.get_data_table_as_json_string(dt)
    return {r.get("Name"): r for r in _json.loads(txt)}


tim = unreal.EditorAssetLibrary.load_asset("/Game/AsdaAssets/Data/NPC/DT_TimList")
nms = unreal.EditorAssetLibrary.load_asset("/Game/AsdaAssets/Data/NPC/DT_NmsList")
if not tim or not nms:
    log("!! ассеты не найдены — прогнать asda_import_await_maps.py")
else:
    log("DT_TimList строк=%d, DT_NmsList строк=%d" % (len(tim.get_row_names()), len(nms.get_row_names())))

    A = dump(tim)
    b = open(TIM_BIN, "rb").read()
    n = struct.unpack_from("<i", b, 4)[0]
    bad = 0
    for i in range(n):
        o = 0x14 + 204 * i
        mid = struct.unpack_from("<i", b, o)[0]
        end = b.find(b"\x00", o + 4, o + 204)
        nm = b[o + 4:end].decode("cp1251", "replace")
        r = A.get(str(mid))
        if not r:
            log("!! строки %d нет в DT_TimList" % mid)
            bad += 1
            continue
        if r.get("VillageName") != nm or r.get("MapId") != mid:
            log("!! %d: в ассете %s, в .BIN «%s»" % (mid, r, nm))
            bad += 1
    log("TIM: расхождений с .BIN — %d (проверено %d записей)" % (bad, n))

    B = dump(nms)
    b = open(NMS_BIN, "rb").read()
    n = struct.unpack_from("<i", b, 4)[0]
    bad = 0
    for i in range(n):
        o = 0x14 + 44 * i
        mid = struct.unpack_from("<i", b, o)[0]
        rel = [struct.unpack_from("<i", b, o + 4 + 4 * k)[0] for k in range(10)]
        r = B.get(str(mid))
        if not r:
            log("!! строки %d нет в DT_NmsList" % mid)
            bad += 1
            continue
        got = [int(x) for x in r.get("Related", [])]
        if got != rel or r.get("MapId") != mid:
            log("!! %d: в ассете %s, в .BIN %s" % (mid, got, rel))
            bad += 1
    log("NMS: расхождений с .BIN — %d (проверено %d записей)" % (bad, n))

    # ── Необходимое условие: на карте вообще должны быть NPC типа 0/4 (сборщик берёт только их) ──
    npc = unreal.EditorAssetLibrary.load_asset("/Game/AsdaAssets/Data/NPC/DT_NpcQuestNpc")
    if npc:
        C = dump(npc)
        per = {}
        for r in C.values():
            t = r.get("NpcType")
            if t not in (0, 4):
                continue
            per[r.get("MapId")] = per.get(r.get("MapId"), 0) + 1
        for mid in sorted(A):
            log("карта %s «%s»: NPC типа 0/4 — %d%s"
                % (mid, A[mid].get("VillageName"), per.get(int(mid), 0),
                   "" if per.get(int(mid), 0) else "  ← назвать её нельзя НИКОГДА"))
        # Кто вообще достижим: объединение строк NMS_list.
        named = set()
        for r in B.values():
            named.update(x for x in r.get("Related", []) if x >= 0)
        log("карты TIM_List, отсутствующие во всех строках NMS_list: %s"
            % sorted(set(int(k) for k in A) - named))
