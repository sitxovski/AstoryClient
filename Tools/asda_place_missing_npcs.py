# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_place_missing_npcs.py — ★ 09.11.2026 ДОБАВЛЕНИЕ «ПРОПАВШИХ» NPC В УРОВНИ КАРТ (пачка 20, баг 7; сессия flammio-npc).
#   ЗАЧЕМ. Клиент оригинала перечисляет в map\<карта>\<карта>.NPC NPC, которых не было в серверной расстановке
#     (NpcData_Info\<Карта>.npc), — построитель уровней (`asda_build_npc_manifest.py` → `asda_place_npcs.py`) их не запёк, и
#     пакет 4067 не находит шаблон (`FindFreeNpc`). Сервер получает их правкой P119 (шахту — P120 сессии шахты); здесь — акторы.
#   ЧТО ДЕЛАЕТ: для каждого NPC из списка MISSING, у которого в уровне ЕЩЁ НЕТ актора AAsdaNpcActor с таким npc_id, спавнит актор
#     ТЕМ ЖЕ способом, что `asda_place_npcs.py` (place_transform, масштаб 0.01, доворот −90, идл anim_00/_Anim, поля npc_id,
#     tile_x/z, facing_deg, npc_name, type_func). Позиция — правилом сервера/эталона: классы 0/2/4 — тайл·100+50, тайл = npc_list
#     X/Z // 100 (как записи P119), классы 1/3/5 — X/Z из npc_list (+0x70/+0x74, `npcMgr_spawnWorldEntityFromTable`); высота — .TER
#     (`asda_ter_height`, единственный источник, как у манифеста). Поворот — npc_list +0x78.
#   ★ НИЧЕГО НЕ УДАЛЯЕТ и не двигает: существующие NPC, свет и AsdaMapObjects_ не трогаются (разрешение владельца 09.11 — только
#     новые акторы). Идемпотентно: уже есть актор с этим npc_id — пропуск. Бэкап .umap до записи (_backup/2026-11-08_flammio_npc).
#   Видимость «только для GM_» и префикс [MISSING] — данные клиента (Config/DefaultGame.ini), не этот скрипт.
#   Запуск (на карту): ASDA_MAP=<карта> python Tools\run_headless_safe.py Tools\asda_place_missing_npcs.py --marker "MISSING-NPC: ГОТОВО"
#   Без unreal (обычный python): `python Tools\asda_place_missing_npcs.py --plan` — печать плана (карта, id, позиция, модель).
# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
import os
import shutil
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

NPC_LIST = r"@@ASTORY_DEV_ROOT@@\Donors\AsdaChroniclesInterlude\Data\NPC\npc_list.BIN"   # таблица NPC клиента-эталона
CONTENT = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Content"                               # для бэкапа .umap
BACKUP = r"@@ASTORY_DEV_ROOT@@\_backup\2026-11-08_flammio_npc"                          # бэкап до записи
# Карта UE (имя .mapraw/.TER и уровня) → NPC к добавлению.
MISSING = {
    "flammio": [7],
    "alpen": [2, 37, 98, 99],   # ★ 09.11: 37 Arshes — сток в Рейне, в Альпии по слову владельца остаётся (только для GM_)
    "firewayforest": [201],
    "inferion": [200],
    "nightvalley": [21, 108, 109, 110, 111, 112, 113, 114, 115, 177],
    "mine": [202, 203, 204],
    "alpen_dungeon": [22],
}
MAP_ID = {"flammio": 7, "alpen": 3, "firewayforest": 16, "inferion": 18, "nightvalley": 5, "mine": 23, "alpen_dungeon": 4}
# ★ 09.11.2026 (P119r): серверный файл NPC карты — ТОЛЬКО по NpcServer/ini/MapInfo.ini (NonPlayerInfo_NNN), не по имени папки:
#   карта 4 (папка alpen_dungeon, Night Valley) = Night_Valley.npc, карта 5 (папка nightvalley, Forgotten Canyon) = LostValley.npc.
#   Если NPC есть в стоковом файле карты, его тайл и поворот берутся оттуда (их сервер шлёт в 4067), иначе — из npc_list.
STOCK_NPC = os.path.join(r"@@ASTORY_DEV_ROOT@@", "Donors", "GameServer_stock", "NpcData_Info")
SERVER_NPC_FILE = {3: "Alpen.npc", 4: "Night_Valley.npc", 5: "LostValley.npc", 7: "Flammio.npc", 16: "firewayforest.npc",
                   18: "Inferion.npc", 23: "^MineWar^.npc"}


def stock_recs(map_id):
    """id → (тайл X, тайл Z, поворот) из стокового .npc карты (пусто — файла нет)."""
    f = os.path.join(STOCK_NPC, SERVER_NPC_FILE.get(map_id, "-"))
    if not os.path.isfile(f):
        return {}
    b = open(f, "rb").read()
    out = {}
    for i in range(len(b) // 0xDD):
        tid = struct.unpack_from("<I", b, i * 0xDD + 4)[0]
        tx, tz, face = struct.unpack_from("<hhH", b, i * 0xDD + 8)
        out[tid] = (tx, tz, face)
    return out


def npc_list():
    """id → dict(name, model, map, x, z, face, cls) из npc_list.BIN (запись 0x88 с +0x14)."""
    b = open(NPC_LIST, "rb").read()
    out = {}
    for k in range((len(b) - 0x14) // 0x88):
        r = 0x14 + k * 0x88
        rid = struct.unpack_from("<H", b, r)[0]
        name = b[r + 4:r + 0x46].split(b"\0")[0].decode("cp949", "replace")
        model = b[r + 0x46:r + 0x66].split(b"\0")[0].decode("latin1")
        mp = struct.unpack_from("<i", b, r + 0x6C)[0]
        x, z = struct.unpack_from("<ff", b, r + 0x70)
        face, cls = struct.unpack_from("<II", b, r + 0x78)
        out[rid] = {"name": name, "model": model, "map": mp, "x": x, "z": z, "face": face, "cls": cls}
    return out


def plan(map_name):
    """Записи к добавлению: idx, type, model, gx/gy/gz, face, tileX/Z, typefunc, name (как в npc_manifest.json)."""
    import asda_ter_height as TERH
    rows = npc_list()
    th = TERH.get(map_name)
    if th is None:
        raise SystemExit("нет .TER у карты %s — высоту не угадываем" % map_name)
    out = []
    stock = stock_recs(MAP_ID[map_name])
    for nid in MISSING[map_name]:
        r = dict(rows[nid])
        if nid in stock:
            stx, stz, sface = stock[nid]
            r["face"] = sface
            if r["cls"] not in (1, 3, 5):
                r["x"], r["z"] = stx * 100 + 50, stz * 100 + 50
        if r["map"] != MAP_ID[map_name]:
            raise SystemExit("NPC %d в npc_list на карте %d, ждали %d" % (nid, r["map"], MAP_ID[map_name]))
        tx, tz = int(r["x"] // 100), int(r["z"] // 100)
        wx, wz = (r["x"], r["z"]) if r["cls"] in (1, 3, 5) else (tx * 100 + 50, tz * 100 + 50)
        wx = min(max(wx, 100.0), 51100.0)
        wz = min(max(wz, 100.0), 51100.0)
        out.append({"idx": 900 + nid, "type": nid, "model": r["model"], "gx": wx, "gy": round(th.GroundY(wx, wz), 3), "gz": wz,
                    "face": r["face"], "tileX": tx, "tileZ": tz, "typefunc": r["cls"], "name": r["name"]})
    return out


def main_unreal():
    """Спавн недостающих акторов в уровень ASDA_MAP (только добавление)."""
    import unreal
    import asda_place_npcs as PN            # тот же трансформ и пути (модуль без побочных эффектов)
    import asda_ue_paths as U
    map_name = os.environ.get("ASDA_MAP", "")
    if map_name not in MISSING:
        unreal.log_error("MISSING-NPC: ОТКАЗ: ASDA_MAP=%r не из списка %s" % (map_name, sorted(MISSING)))
        return
    level = U.level_path(map_name)
    EAL = unreal.EditorAssetLibrary
    if not EAL.does_asset_exist(level):
        unreal.log_error("MISSING-NPC: ОТКАЗ: нет уровня %s" % level)
        return
    items = plan(map_name)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level(level)
    acts = [a for a in eas.get_all_level_actors() if isinstance(a, unreal.AsdaNpcActor)]
    have = {int(a.get_editor_property("npc_id")) for a in acts}
    # ★ 09.11.2026 (P119r): уже запечённые ЭТИМ инструментом акторы (метка NPC_M…) — поправить трансформ и поворот по текущему плану
    #   (поворот NPC 114 каньона: сервер 270, npc_list 0). Чужие акторы не трогаются.
    fixed = 0
    for a in acts:
        if not a.get_actor_label().startswith("NPC_M"):
            continue
        n = next((x for x in items if x["type"] == int(a.get_editor_property("npc_id"))), None)
        if n is None:
            continue
        xf = PN.place_transform((n["gx"], n["gy"], n["gz"]), PN.roty_matrix(n["face"]))
        if int(a.get_editor_property("facing_deg")) != n["face"] or (a.get_actor_location() - xf.translation).length() > 0.5:
            a.set_actor_transform(xf, False, False)
            a.set_editor_property("facing_deg", n["face"])
            a.set_editor_property("tile_x", n["tileX"])
            a.set_editor_property("tile_z", n["tileZ"])
            fixed += 1
            unreal.log_warning("MISSING-NPC %s: поправлен NPC %d — поворот %d, тайл (%d,%d)" % (map_name, n["type"], n["face"], n["tileX"], n["tileZ"]))
    if fixed:
        les.save_current_level()
    todo = [n for n in items if n["type"] not in have]
    unreal.log_warning("MISSING-NPC %s: в уровне NPC-акторов %d, к добавлению %d из %d (%s)" % (
        map_name, len(have), len(todo), len(items), ", ".join(str(n["type"]) for n in todo)))
    if not todo:
        unreal.log_warning("MISSING-NPC: ГОТОВО (нечего добавлять, поправлено %d)" % fixed)
        return
    # Бэкап .umap до записи.
    rel = level.replace("/Game/", "") + ".umap"
    src = os.path.join(CONTENT, rel.replace("/", os.sep))
    dst = os.path.join(BACKUP, "AstoryClient", "Content", rel.replace("/", os.sep))
    if os.path.exists(src) and not os.path.exists(dst):
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)
        if os.path.getsize(src) != os.path.getsize(dst):
            unreal.log_error("MISSING-NPC: ОТКАЗ: бэкап %s не сошёлся по размеру" % dst)
            return
    placed = 0
    for n in todo:
        mesh_path = "%s/%s/SkeletalMeshes/%s" % (PN.NPC_ROOT, n["model"], n["model"])
        anim_path = None
        for cand in (mesh_path + "anim_00", mesh_path + "_Anim"):
            if EAL.does_asset_exist(cand):
                anim_path = cand
                break
        xf = PN.place_transform((n["gx"], n["gy"], n["gz"]), PN.roty_matrix(n["face"]))
        act = eas.spawn_actor_from_class(unreal.AsdaNpcActor, xf.translation)
        act.set_actor_transform(xf, False, False)
        act.set_editor_property("npc_id", n["type"])
        act.set_editor_property("tile_x", n["tileX"])
        act.set_editor_property("tile_z", n["tileZ"])
        act.set_editor_property("facing_deg", n["face"])
        act.set_editor_property("npc_name", n["name"])
        act.set_editor_property("type_func", n["typefunc"])
        if EAL.does_asset_exist(mesh_path):
            act.mesh.set_skeletal_mesh(unreal.load_asset(mesh_path))
        else:
            unreal.log_warning("MISSING-NPC: нет меша %s — в игре модель-замена (asda.Npc.ModelFallback)" % mesh_path)
        if anim_path:
            anim = unreal.load_asset(anim_path)
            act.set_editor_property("idle_anim", anim)
            act.mesh.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_SINGLE_NODE)
            ad = act.mesh.get_editor_property("animation_data")
            ad.set_editor_property("anim_to_play", anim)
            for prop, val in (("saved_looping", True), ("saved_playing", True)):
                try:
                    ad.set_editor_property(prop, val)
                except Exception:
                    pass
            act.mesh.set_editor_property("animation_data", ad)
        act.set_editor_property("disabled", False)
        act.set_actor_label("NPC_M%03d_%s" % (n["type"], n["model"].replace("-a_00", "")))
        act.set_folder_path("NPC/Missing")
        placed += 1
        unreal.log_warning("MISSING-NPC %s: NPC %d «%s» %s в (%.0f, %.0f, %.0f) тайл (%d,%d) поворот %d класс %d" % (
            map_name, n["type"], n["name"], n["model"], n["gx"], n["gy"], n["gz"], n["tileX"], n["tileZ"], n["face"], n["typefunc"]))
    les.save_current_level()
    unreal.log_warning("MISSING-NPC: ГОТОВО %s: добавлено %d" % (map_name, placed))


def main():
    """--plan — печать плана без unreal; иначе (в редакторе) спавн."""
    if "--plan" in sys.argv:
        try:
            sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass
        for m in MISSING:
            for n in plan(m):
                print("%-14s %4d %-28s %-28s (%.0f, %.1f, %.0f) тайл (%d,%d) поворот %d класс %d" % (
                    m, n["type"], n["name"], n["model"], n["gx"], n["gy"], n["gz"], n["tileX"], n["tileZ"], n["face"], n["typefunc"]))
        return
    main_unreal()


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
