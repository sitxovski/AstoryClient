# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_npc_pick_zone.py — ЗАМЕР (только чтение): где у запечённых NPC стоит капсула пикинга относительно ВИДИМОЙ модели.
#
# ЗАЧЕМ (жалоба владельца 22.10.2026): у части NPC (доска объявлений Фламмио/Силариса, гробницы) кольцо выделения и зона клика
#   смещены от модели или маленькие, как у обычного NPC, — по ним не кликается и диалог не открывается. Кольцо стоит в
#   `GetActorLocation()`, капсула — в начале координат меша (`AsdaPick::FitToMeshBounds`: `SetRelativeLocation(0,0,HalfH)`), а
#   её размер — из `Mesh->Bounds`. Замер отвечает: (1) насколько центр ВИДИМОЙ модели уехал от начала координат актора по XY,
#   (2) совпадает ли размер капсулы с размером модели, (3) какой класс сущности (TypeFunc) у таких NPC.
#
# ЧТО ПЕЧАТАЕТ по каждому `AsdaNpcActor` уровня: метку, NpcId, TypeFunc, позицию актора; центр/полуразмер габаритов скелетного
#   меша (`get_component_bounds` = `Component->Bounds`), габариты АССЕТА меша (`SkeletalMesh.get_bounds`), физ-ассет; положение и
#   радиус/полувысоту капсулы в мире; смещение «центр модели − актор» по XY. Флаг «!» — смещение > 60 или радиус капсулы меньше
#   половины полуразмера модели.
#
# Запуск (редактор закрыт, без окон): python Tools\run_headless_safe.py Tools\_probe_npc_pick_zone.py --marker "ЗАМЕР-ЗОНЫ: ГОТОВО"
#   ASDA_MAPS=flammio,silalis (по умолчанию все карты с NPC). Ничего не сохраняет.
# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
import math
import os

MAPS = [m for m in os.environ.get("ASDA_MAPS", "alpen,silalis,rein,conquester,sunnycoast,flammio,castle_of_chess,quizstage,"
                                   "nightvalley,firewayforest,Mountain_Flamion,StagnantDesert,palace_of_queen,castle_of_chaos,"
                                   "dragonlair,battlefield,t_palace_of_queen").split(",") if m]


def log(m):
    import unreal
    unreal.log_warning("[замер-зоны] %s" % m)


def main():
    import unreal
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    total = flagged = 0
    for mp in MAPS:
        level = "/Game/Map/%s/%s" % (mp, mp)
        if not unreal.EditorAssetLibrary.does_asset_exist(level):
            log("%s: уровня нет" % mp)
            continue
        unreal.EditorLoadingAndSavingUtils.load_map(level)
        npcs = [a for a in eas.get_all_level_actors() if a.get_class().get_name() == "AsdaNpcActor"]
        log("════ %s: NPC %d ════" % (mp, len(npcs)))
        for a in npcs:
            total += 1
            loc = a.get_actor_location()
            try:
                nid = a.get_editor_property("npc_id")
                tf = a.get_editor_property("type_func")
            except Exception:
                nid, tf = -1, -1
            mesh = None
            cap = None
            for c in a.get_components_by_class(unreal.SkeletalMeshComponent):
                if c.get_name() == "NpcMesh":
                    mesh = c
            for c in a.get_components_by_class(unreal.CapsuleComponent):
                if c.get_name() == "PickHitbox":
                    cap = c
            if not mesh:
                log("%s: нет NpcMesh" % a.get_actor_label())
                continue
            o, e, _r = unreal.SystemLibrary.get_component_bounds(mesh)
            sk = mesh.get_editor_property("skeletal_mesh_asset") if hasattr(mesh, "get_editor_property") else None
            pa = None
            ab = None
            try:
                if sk:
                    pa = sk.get_editor_property("physics_asset")
                    ab = sk.get_bounds()
            except Exception:
                pass
            co = cap.get_world_location() if cap else None
            cr = abs(cap.get_scaled_capsule_radius()) if cap else 0.0
            ch = abs(cap.get_scaled_capsule_half_height()) if cap else 0.0
            dx, dy = o.x - loc.x, o.y - loc.y
            off = math.hypot(dx, dy)
            big = max(e.x, e.y)
            flag = "!" if (off > 60.0 or cr < 0.5 * big) else " "
            flagged += flag == "!"
            bones = ""
            if tf in (1, 3, 4, 5):
                pts = []
                for bn in ("b_bone01", "b_bone02"):
                    try:
                        if mesh.does_socket_exist(bn):
                            v = mesh.get_socket_location(bn)
                            pts.append(v)
                            bones += " %s=(%.0f,%.0f,%.0f)" % (bn, v.x, v.y, v.z)
                        else:
                            bones += " %s=НЕТ" % bn
                    except Exception as ex:
                        bones += " %s=ошибка:%s" % (bn, ex)
                if len(pts) == 2:
                    bc = ((pts[0].x + pts[1].x) / 2, (pts[0].y + pts[1].y) / 2)
                    bones += " центр-костей XY=(%.0f,%.0f) полу=(%.0f,%.0f) от модели %.0f" % (
                        bc[0], bc[1], abs(pts[0].x - pts[1].x) / 2, abs(pts[0].y - pts[1].y) / 2, math.hypot(bc[0] - o.x, bc[1] - o.y))
            # ★ 22.10 после правки: короба оригинала (+189 пик, +183 разговор) и короб выделения (пик ∪ модель).
            for nm in ("get_pick_box_world", "get_talk_box_world", "get_select_box_world"):
                fn = getattr(a, nm, None)
                if not fn:
                    continue
                r = fn()
                bx = r[1] if isinstance(r, (tuple, list)) else r
                if bx is None or (isinstance(r, (tuple, list)) and not r[0]):   # bool=false → UE-Python отдаёт None
                    bones += " %s=—" % nm[4:8]
                    continue
                c = (bx.min + bx.max) * 0.5
                h = (bx.max - bx.min) * 0.5
                bones += " %s ц=(%.0f,%.0f,%.0f) п=(%.0f,%.0f,%.0f)" % (nm[4:8], c.x, c.y, c.z, h.x, h.y, h.z)
            rot = a.get_actor_rotation()
            mrl = mesh.get_editor_property("relative_location")
            mrr = mesh.get_editor_property("relative_rotation")
            bones += " | поворот актора (P%.1f Y%.1f R%.1f) меш отн. (%.0f,%.0f,%.0f) (P%.1f Y%.1f R%.1f)" % (
                rot.pitch, rot.yaw, rot.roll, mrl.x, mrl.y, mrl.z, mrr.pitch, mrr.yaw, mrr.roll)
            log("%s %-34s id=%-4s tf=%s акт=(%.0f,%.0f,%.0f) модель центр=(%.0f,%.0f,%.0f) полу=(%.0f,%.0f,%.0f) "
                "сдвиг XY=%.0f | капсула центр=(%.0f,%.0f) R=%.0f H/2=%.0f | физ=%s ассет-полу=%s"
                % (flag, a.get_actor_label()[:34], nid, tf, loc.x, loc.y, loc.z, o.x, o.y, o.z, e.x, e.y, e.z, off,
                   co.x if co else 0, co.y if co else 0, cr, ch, pa.get_name() if pa else "—",
                   ("(%.0f,%.0f,%.0f)" % (ab.box_extent.x, ab.box_extent.y, ab.box_extent.z)) if ab else "—") + bones)
    log("ЗАМЕР-ЗОНЫ: ГОТОВО — NPC %d, с флагом %d" % (total, flagged))


try:
    import unreal as _u  # noqa: F401
    _DIRECT = (__name__ == "__main__" or __name__ == "builtins")
except Exception:
    _DIRECT = False

if _DIRECT:
    main()
