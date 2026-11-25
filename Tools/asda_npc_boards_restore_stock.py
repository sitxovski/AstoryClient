# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_npc_boards_restore_stock.py — ★ 22.10.2026 ОТКАТ РУЧНЫХ ПРАВОК ДОСОК (слово владельца: «можешь обратить ручные правки и исправить
#   размещение полноценно»). Доски Силариса (`mdel_raidboard`, NPC 9) и Рейна (`mdel_noticeboard`, NPC 148) владелец 18.10 поднимал
#   вручную креном 88–90° — модель лежала на боку из-за дефекта импорта (служебная кость `b_bone01` в идл-клипе, исправлено шагом
#   `asda_fix_npc_helper_root_anim.py`). Теперь доски стоят сами, и ручной крен клал бы их обратно на бок.
# ЧТО ДЕЛАЕТ: актору доски возвращает размещение СТОКА оригинала (`Donors\GameServer_stock\NpcData_Info\<Карта>.npc`): клетку →
#   мировые X/Y (как place-скрипты: X = −(tz·100+50), Y = tx·100+50), поворот −face−90 без крена и
#   тангажа, высоту — из DA_WalkGrid; поля TileX/TileZ/FacingDeg актора — тоже сток. Меняются ТОЛЬКО эти акторы; бэкап .umap до
#   записи (`backup_file`, набор `npc_boards_stock`, md5). Идемпотентно (уже на месте — 0 правок).
# Сухой прогон: ASDA_BOARDS_DRYRUN=1. Маркер: «ДОСКИ: ГОТОВО».
# Запуск: python Tools\run_headless_safe.py Tools\asda_npc_boards_restore_stock.py --marker "ДОСКИ: ГОТОВО"
# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
import os
import struct
import sys

STOCK = r"@@ASTORY_DEV_ROOT@@\Donors\GameServer_stock\NpcData_Info"
CONTENT = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Content"
# (карта UE, файл .npc, NpcId, подстрока метки актора)
BOARDS = [("silalis", "Sillaris.npc", 9, "raidboard"), ("rein", "Rein.npc", 148, "noticeboard")]


def log(m):
    import unreal
    unreal.log_warning("[доски] %s" % m)


def stock_rec(fname, npc_id):
    """(tileX, tileZ, face) записи стока с этим NpcId; None — нет."""
    b = open(os.path.join(STOCK, fname), "rb").read()
    for i in range(len(b) // 0xDD):
        o = i * 0xDD
        if struct.unpack_from("<I", b, o + 4)[0] == npc_id:
            tx, tz, face = struct.unpack_from("<hhh", b, o + 8)
            return tx, tz, face
    return None


def main():
    import unreal
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import asda_headless_tool as HT
    dry = bool(os.environ.get("ASDA_BOARDS_DRYRUN"))
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    changed = 0
    for mp, fname, nid, label in BOARDS:
        level = "/Game/Map/%s/%s" % (mp, mp)
        unreal.EditorLoadingAndSavingUtils.load_map(level)
        npcs = [a for a in eas.get_all_level_actors() if a.get_class().get_name() == "AsdaNpcActor"]
        rec = stock_rec(fname, nid)
        board = [a for a in npcs if label in a.get_actor_label() and a.get_editor_property("npc_id") == nid]
        if not rec or len(board) != 1:
            raise RuntimeError("%s: сток %s, досок с id %d — %d; не трогаю" % (mp, rec, nid, len(board)))
        tx, tz, face = rec
        a = board[0]
        # Клетка→мир — как у place-скриптов и asda_rebake_entity_heights: игра (tx·100+50, tz·100+50) → UE X = −gz, Y = gx;
        #   высота — боевой ассет DA_WalkGrid (то же, что видит игра).
        gx, gz = tx * 100 + 50, tz * 100 + 50
        x, y = -float(gz), float(gx)
        cur = a.get_actor_location()
        da = unreal.load_asset("/Game/Map/%s/DA_WalkGrid" % mp)
        z = cur.z
        if da:
            r = da.get_ground_y(float(gx), float(gz))
            z = float(r[1]) if isinstance(r, (tuple, list)) else float(r)
        else:
            log("%s: нет DA_WalkGrid — высота прежняя" % mp)
        # Сверка формулы поворота по соседям без крена: (yaw + face + 90) mod 360 должно быть ≈ 0.
        dev = []
        for n in npcs:
            r0 = n.get_actor_rotation()
            if n is a or abs(r0.roll) > 0.5 or abs(r0.pitch) > 0.5:
                continue
            dev.append(((r0.yaw + n.get_editor_property("facing_deg") + 90.0) + 180.0) % 360.0 - 180.0)
        dev.sort()
        log("%s: сверка поворота по %d соседям — медиана отклонения %.2f°, max |%.2f|°" % (
            mp, len(dev), dev[len(dev) // 2] if dev else 0.0, max([abs(d) for d in dev] or [0.0])))
        want_rot = unreal.Rotator(roll=0.0, pitch=0.0, yaw=-face - 90.0)   # yaw=−face−90 (формула 18.10), без крена/тангажа
        rot = a.get_actor_rotation()
        log("%s %s: сток клетка (%d,%d) face %d → UE (%.0f,%.0f,%.0f) поворот Y%.0f; было (%.0f,%.0f,%.0f) P%.1f Y%.1f R%.1f; "
            "" % (mp, a.get_actor_label(), tx, tz, face, x, y, z, want_rot.yaw,
                                                    cur.x, cur.y, cur.z, rot.pitch, rot.yaw, rot.roll))
        same = (abs(cur.x - x) < 0.5 and abs(cur.y - y) < 0.5 and abs(rot.roll) < 0.01 and abs(rot.pitch) < 0.01
                and abs(((rot.yaw - want_rot.yaw) + 180.0) % 360.0 - 180.0) < 0.01)
        if same:
            log("%s: уже на месте — 0 правок" % mp)
            continue
        if dry:
            continue
        f = os.path.join(CONTENT, "Map", mp, mp + ".umap")
        bak = HT.backup_file(f, "npc_boards_stock", verify="md5", log=log)
        if bak is None:
            raise RuntimeError("нет %s — бэкап невозможен" % f)
        a.set_actor_location_and_rotation(unreal.Vector(x, y, z), want_rot, False, True)
        a.set_editor_property("tile_x", tx)
        a.set_editor_property("tile_z", tz)
        a.set_editor_property("facing_deg", face)
        unreal.log("ASDA_WRITES_ASSET: %s" % level)
        if not les.save_current_level():
            raise RuntimeError("не сохранён %s" % level)
        changed += 1
        log("%s: сохранено; бэкап %s" % (mp, bak))
    log("ДОСКИ: ГОТОВО%s — изменено карт %d" % ("  [СУХОЙ ПРОГОН]" if dry else "", changed))


if __name__ == "__main__" or globals().get("__spec__") is None:
    try:
        main()
    except Exception:
        import traceback
        import unreal
        unreal.log_error("[доски] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
