# Copyright (c) 2026 @sitxovski (Discord)
# _probe_qmark_over_plate.py — контроль: квестовая метка ОСТАЛАСЬ над плашкой имени после смены
#   высоты плашки на эталонную (npc_list +0x68). Печатает Z плашки, Z метки и разницу.
import unreal
def log(m): unreal.log_warning("[qmark] %s" % m)
if __name__ == "__main__":   # ★ без побочных эффектов при импорте (правило проекта)
    unreal.EditorLoadingAndSavingUtils.load_map("/Game/Map/alpen/alpen")
    w = unreal.EditorLevelLibrary.get_editor_world()
    acts = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.load_class(None, "/Script/AsdaReborn.AsdaNpcActor"))
    log("%-5s %-26s %9s %9s %7s" % ("id", "имя", "плашкаZ", "меткаZ", "дельта"))
    seen = set()
    for a in acts:
        nid = a.get_editor_property("NpcId")
        if nid in seen: continue
        seen.add(nid)
        a.call_method("SetQuestMark", (1,))
        sc = a.get_actor_scale3d().z
        np_ = a.get_editor_property("Nameplate")
        qm = a.get_editor_property("QuestMark")
        pz = np_.get_editor_property("relative_location").z * sc
        qz = qm.get_editor_property("relative_location").z * sc
        log("%-5d %-26s %9.1f %9.1f %7.1f" % (nid, a.get_editor_property("NpcName")[:26], pz, qz, qz - pz))
    log("ГОТОВО")
