# Copyright (c) 2026 @sitxovski (Discord)
# _probe_npc_plate_height.py — ЗАМЕР высоты плашки имени NPC (до фикса) против эталона.
#   Печатает по каждому NPC карты: габариты меша, капсулу пикинга, локальный Z плашки/метки,
#   и эталонное значение из npc_list.BIN (+0x68; -1 -> 150; тип-функции +0x7C == 2 -> 1500).
#   Запуск: Tools\run_headless.bat Tools\_probe_npc_plate_height.py
import unreal, struct, os
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P

NPC_LIST = _P.NPC_LIST_BIN
MAPS = ["/Game/Map/alpen/alpen"]

def log(m): unreal.log_warning("[plate-h] %s" % m)

def load_list():
    b = open(NPC_LIST, "rb").read()
    hdr, stride = 0x14, 0x88
    out = {}
    for i in range((len(b) - hdr) // stride):
        r = hdr + i * stride
        rid = struct.unpack_from("<i", b, r)[0]
        e = b.find(b"\0", r + 4)
        nm = b[r + 4:e if 0 <= e - r - 4 <= 40 else r + 36].decode("cp1251", "ignore")
        out[rid] = (nm, struct.unpack_from("<i", b, r + 0x68)[0], struct.unpack_from("<i", b, r + 0x7C)[0])
    return out

if __name__ == "__main__":   # ★ без побочных эффектов при импорте (правило проекта)
    TBL = load_list()
    log("npc_list: %d записей" % len(TBL))

    for mp in MAPS:
        unreal.EditorLoadingAndSavingUtils.load_map(mp)
        w = unreal.EditorLevelLibrary.get_editor_world()
        acts = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.load_class(None, "/Script/AsdaReborn.AsdaNpcActor"))
        log("=== %s: акторов NPC = %d ===" % (mp, len(acts)))
        seen = set()
        log("%-5s %-28s %8s %8s %8s %8s %8s %8s %6s %6s" % (
            "id", "имя", "origZ", "extZ", "extXY", "capHalf", "верхКапс", "плашкаZ", "этал", "fn"))
        for a in acts:
            nid = a.get_editor_property("NpcId")
            if nid in seen: continue
            seen.add(nid)
            m = a.get_editor_property("Mesh")
            cap = a.get_editor_property("PickHitbox")
            np_ = a.get_editor_property("Nameplate")
            sc = a.get_actor_scale3d().z
            sm = m.get_editor_property("skeletal_mesh_asset") if m else None
            msz = m.get_editor_property("relative_scale3d").z if m else 1.0
            b = sm.get_bounds() if sm else None
            extz = b.box_extent.z * sc * msz if b else -1.0
            extxy = max(b.box_extent.x, b.box_extent.y) * sc * msz if b else -1.0
            orgz = b.origin.z * sc * msz if b else 0.0
            ch = cap.get_scaled_capsule_half_height() if cap else -1.0
            cz = cap.get_editor_property("relative_location").z * sc if cap else 0.0
            top = cz + ch
            pz = np_.get_editor_property("relative_location").z * sc if np_ else -1.0
            nm, h, fn = TBL.get(nid, ("?", -999, -999))
            eh = 1500 if fn == 2 else (150 if h == -1 else h)
            log("%-5d %-28s %8.1f %8.1f %8.1f %8.1f %8.1f %8.1f %6d %6d" % (
                nid, nm[:28], orgz, extz, extxy, ch, top, pz, eh, fn))
    log("ГОТОВО")
