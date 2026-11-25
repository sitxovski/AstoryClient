# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_npc_level_audit.py — ★ 09.11.2026 АУДИТ NPC-АКТОРОВ УРОВНЕЙ ПРОТИВ СЕРВЕРНОЙ РАССТАНОВКИ (пачка 20, баг 7; сессия flammio-npc).
#   ЗАЧЕМ. Владелец: «make sure you are not duplicating them». В уровнях порта NPC-акторы — шаблоны, которых «удочеряет» пакет 4067
#     (`FindFreeNpc`: ближайший незанятый актор того же id), новых акторов по пакету клиент не создаёт. Поэтому «дубль» = в уровне
#     карты актор с id больше раз, чем сервер ставит этот id на эту карту; «лишний» = актора id нет в серверном файле карты вовсе.
#   ЭТАЛОН: серверные .npc карты (файл — по NpcServer\ini\MapInfo.ini), конечное состояние после P119r — JSON `--ref` (id → число по
#     карте), его строит `python Tools\asda_npc_level_audit.py --build-ref <папка .npc> [<папка выходов P119r>] > ref.json`.
#   РЕЖИМЫ (в редакторе, ASDA_NPC_REF=<ref.json>):
#     аудит (по умолчанию) — по всем уровням карт реестра: строки «NPC-AUDIT <карта>: акторов N, дублей D, лишних E, нет M …», итог
#       «NPC-AUDIT ИТОГ: карт K, дублей D, лишних E»;
#     удаление (ASDA_NPC_REMOVE="карта:id[,карта:id…]") — снять ВСЕ акторы id на карте (точечно, с бэкапом .umap), затем аудит.
#   Запуск: python Tools\run_headless_safe.py Tools\asda_npc_level_audit.py --marker "NPC-AUDIT ИТОГ"
# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
import json
import os
import shutil
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

CONTENT = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Content"                 # для бэкапа .umap
BACKUP = r"@@ASTORY_DEV_ROOT@@\_backup\2026-11-08_flammio_npc"            # бэкап до записи
# Номер карты → файл NPC сервера (NpcServer\ini\MapInfo.ini, NonPlayerInfo_NNN) и папка уровня порта.
MAPINFO = ["Sillaris", "Rein", "Conquester", "Alpen", "Night_Valley", "LostValley", "SunnyCoast", "Flammio", "palace_of_queen",
           "Castle_of_Chess", "t_Palace_of_Queen", "t_Castle_of_chess", "MountainFlamion", "Flabis", "StagnantDesert", "DragonLaiR",
           "firewayforest", "Castle_of_Chaos", "Inferion", "battlefield", "eventfield", "fieldofhonor", "quizstage", "^MineWar^"]
LEVEL = {0: "silalis", 1: "rein", 2: "conquester", 3: "alpen", 4: "alpen_dungeon", 5: "nightvalley", 6: "sunnycoast", 7: "flammio",
         8: "palace_of_queen", 9: "castle_of_chess", 10: "t_palace_of_queen", 11: "t_castle_of_chess", 12: "Mountain_Flamion",
         13: "flabis", 14: "StagnantDesert", 15: "dragonlair", 16: "firewayforest", 17: "castle_of_chaos", 18: "inferion",
         19: "battlefield", 22: "quizstage", 23: "mine"}


def build_ref(npc_dir, override_dir=None):
    """{карта: {id: число}} из .npc (override_dir — выходы P119r поверх)."""
    out = {}
    for mid, name in enumerate(MAPINFO):
        p = None
        for d in ([override_dir] if override_dir else []) + [npc_dir]:
            for f in os.listdir(d):
                if f.lower() == name.lower() + ".npc":
                    p = os.path.join(d, f)
                    break
            if p:
                break
        cnt = {}
        if p:
            b = open(p, "rb").read()
            for i in range(len(b) // 0xDD):
                tid = struct.unpack_from("<I", b, i * 0xDD + 4)[0]
                cnt[tid] = cnt.get(tid, 0) + 1
        out[mid] = cnt
    return out


def main_unreal():
    """Удаление (если задано) и аудит всех уровней."""
    import unreal
    import asda_ue_paths as U
    ref = {int(k): {int(i): n for i, n in v.items()} for k, v in json.load(open(os.environ["ASDA_NPC_REF"], encoding="utf-8")).items()}
    rem = {}
    for tok in [t for t in os.environ.get("ASDA_NPC_REMOVE", "").split(",") if t.strip()]:
        m, i = tok.split(":")
        rem.setdefault(m.strip(), set()).add(int(i))
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    EAL = unreal.EditorAssetLibrary
    tot_d = tot_e = maps = 0
    for mid, lvl in sorted(LEVEL.items()):
        level = U.level_path(lvl)
        if not EAL.does_asset_exist(level):
            unreal.log_warning("NPC-AUDIT %s: нет уровня %s" % (lvl, level))
            continue
        les.load_level(level)
        acts = [a for a in eas.get_all_level_actors() if isinstance(a, unreal.AsdaNpcActor)]
        if lvl in rem:
            kill = [a for a in acts if int(a.get_editor_property("npc_id")) in rem[lvl]]
            if kill:
                rel = level.replace("/Game/", "") + ".umap"
                src = os.path.join(CONTENT, rel.replace("/", os.sep))
                dst = os.path.join(BACKUP, "remove_0911", "AstoryClient", "Content", rel.replace("/", os.sep))
                if not os.path.exists(dst):
                    os.makedirs(os.path.dirname(dst), exist_ok=True)
                    shutil.copy2(src, dst)
                for a in kill:
                    unreal.log_warning("NPC-AUDIT %s: снят актор %s (NPC %d)" % (lvl, a.get_actor_label(), int(a.get_editor_property("npc_id"))))
                    eas.destroy_actor(a)
                les.save_current_level()
                acts = [a for a in eas.get_all_level_actors() if isinstance(a, unreal.AsdaNpcActor)]
        cnt = {}
        for a in acts:
            if a.get_editor_property("disabled"):
                continue
            i = int(a.get_editor_property("npc_id"))
            cnt[i] = cnt.get(i, 0) + 1
        want = ref.get(mid, {})
        dup = {i: (n, want.get(i, 0)) for i, n in cnt.items() if i in want and n > want[i]}
        extra = {i: n for i, n in cnt.items() if i not in want}
        miss = {i: n for i, n in want.items() if cnt.get(i, 0) < n}
        tot_d += len(dup)
        tot_e += len(extra)
        maps += 1
        unreal.log_warning("NPC-AUDIT %s (карта %d, %s.npc): акторов %d, id %d; дублей %d %s; лишних %d %s; нет %d %s" % (
            lvl, mid, MAPINFO[mid], sum(cnt.values()), len(cnt), len(dup), sorted(dup.items()), len(extra), sorted(extra.items()),
            len(miss), sorted(miss.items())))
    unreal.log_warning("NPC-AUDIT ИТОГ: карт %d, дублей %d, лишних %d" % (maps, tot_d, tot_e))


def main():
    """--build-ref <папка> [<override>] — эталон в stdout; иначе (в редакторе) удаление/аудит."""
    if "--build-ref" in sys.argv:
        a = sys.argv[sys.argv.index("--build-ref") + 1:]
        print(json.dumps(build_ref(a[0], a[1] if len(a) > 1 else None), ensure_ascii=False))
        return
    main_unreal()


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
