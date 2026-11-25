# Copyright (c) 2026 @sitxovski (Discord)
# _probe_qmark_height.py — исходные данные для высоты квестовой метки:
#   1) габариты мешей меток (asset bounds) — где у модели ноль по Z;
#   2) масштаб NPC/меша и Z плашки имени по каждому NPC карты;
#   3) позиция контрола имени внутри канваса плашки (WBP) — верх строки имени.
import unreal

def log(m): unreal.log_warning("[qmark-h] %s" % m)

MESHES = [
    "/Game/AsdaAssets/Models/NPC/eff_0_npc_99_exclam-a_00/SkeletalMeshes/eff_0_npc_99_exclam-a_00",
    "/Game/AsdaAssets/Models/NPC/eff_0_npc_99_exclam-a_01/SkeletalMeshes/eff_0_npc_99_exclam-a_01",
    "/Game/AsdaAssets/Models/NPC/eff_0_npc_99_exclam-a_02/SkeletalMeshes/eff_0_npc_99_exclam-a_02",
]

def dump_meshes():
    for p in MESHES:
        sm = unreal.load_asset(p)
        if not sm:
            log("НЕТ меша %s" % p); continue
        b = sm.get_bounds()
        o = b.origin; e = b.box_extent
        log("меш %-34s origin=(%.2f,%.2f,%.2f) extent=(%.2f,%.2f,%.2f) низ_z=%.2f верх_z=%.2f"
            % (sm.get_name(), o.x, o.y, o.z, e.x, e.y, e.z, o.z - e.z, o.z + e.z))

def dump_widget():
    for asset in ("WBP_NAMEPLATE_MAN1", "WBP_NAMEPLATE_MON"):
        path = "/Game/AsdaAssets/UI/GameMain/%s" % asset
        bp = unreal.EditorAssetLibrary.load_asset(path)
        if not bp:
            log("НЕТ виджета %s" % asset); continue
        try:
            widgets = unreal.AsdaUmgBuilder.list_widgets(bp) or []
        except Exception as ex:
            log("%s: list_widgets недоступен (%s)" % (asset, ex)); continue
        log("%s: виджетов=%d" % (asset, len(widgets)))
        for w in widgets:
            nm = str(w.get_name())
            sl = w.slot
            if sl is None: continue
            try:
                lay = sl.get_editor_property("layout_data")
                off = lay.offsets
                log("  %-30s pos=(%.1f,%.1f) size=(%.1f,%.1f)" % (nm, off.left, off.top, off.right, off.bottom))
            except Exception:
                pass

def dump_npcs():
    unreal.EditorLoadingAndSavingUtils.load_map("/Game/Map/alpen/alpen")
    w = unreal.EditorLevelLibrary.get_editor_world()
    acts = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.load_class(None, "/Script/AsdaReborn.AsdaNpcActor"))
    log("%-5s %-24s %6s %8s %9s %9s %7s" % ("id", "имя", "масшт", "верхмеш", "плашкаZ", "меткаZ", "дельта"))
    seen = set()
    for a in acts:
        nid = a.get_editor_property("NpcId")
        if nid in seen: continue
        seen.add(nid)
        a.call_method("SetQuestMark", (1,))
        msh = a.get_editor_property("Mesh")
        sc = abs(msh.get_world_scale().z) if hasattr(msh, "get_world_scale") else abs(a.get_actor_scale3d().z)
        base = a.get_actor_location().z
        mb = msh.get_editor_property("bounds") if False else None
        try:
            org, ext = a.get_actor_bounds(False)
            top = (org.z + ext.z) - base
        except Exception:
            top = -1.0
        np_ = a.get_editor_property("Nameplate")
        qm = a.get_editor_property("QuestMark")
        pz = np_.get_editor_property("relative_location").z * sc
        qz = qm.get_editor_property("relative_location").z * sc
        log("%-5d %-24s %6.3f %8.1f %9.1f %9.1f %7.1f"
            % (nid, str(a.get_editor_property("NpcName"))[:24], sc, top, pz, qz, qz - pz))

if __name__ == "__main__":
    dump_meshes()
    dump_widget()
    dump_npcs()
    log("ГОТОВО")
