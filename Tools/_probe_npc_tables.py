# Copyright (c) 2026 @sitxovski (Discord)
# _probe_npc_tables.py — НЕЗАВИСИМАЯ сверка ЗАЛИТЫХ таблиц против сырых `.BIN`/`.NSR`.
#   Таблица читается ИЗ АССЕТА (`export_data_table_to_json_string`), источник разбирается ЗАНОВО —
#   то есть сравниваются два независимых прохода, а не JSON сам с собой.
#   Проверяются ИМЕННО те поля, которые добавил заход Э3…Э5:
#     · DT_NpcNsrPage  — новая пара «уровень» (cond+6 / cond+8) и квестовая часть под-записи шага
#                        (AcceptQuestId +1028, ObjActive +164+4j, Objs +188+140j);
#     · DT_SnqIct      — ключ, talkId, флаги категорий и данные условия;
#     · DT_NpcSnq      — questId и пять ключей у обеих таблиц NPC;
#     · DT_WarpList / DT_EpisodeTable / DT_EventItem — ключевые поля.
#   Запуск: Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\_probe_npc_tables.py
import json
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_import_nsr_dialog as NSR
import asda_import_npc_tables as TBL

BAD = [0]


def L(m):
    unreal.log_warning("[probe-npc] %s" % m)


def fail(m):
    BAD[0] += 1
    if BAD[0] <= 15:
        L("!! %s" % m)


def table_json(path):
    dt = unreal.EditorAssetLibrary.load_asset(path)
    if not dt:
        fail("нет ассета %s" % path)
        return {}
    txt = unreal.DataTableFunctionLibrary.export_data_table_to_json_string(dt)
    rows = json.loads(txt)
    return {str(r.get("Name")): r for r in rows}


def eq(tag, a, b):
    if a != b:
        fail("%s: %r != %r" % (tag, a, b))
        return False
    return True


def main():
    # ── 1. DT_NpcNsrPage ──
    _, _, _, pages = NSR.read_all()
    src = {"%s_%d" % (p["NsrName"], p["TalkId"]): p for p in pages}
    got = table_json("/Game/AsdaAssets/Data/NPC/DT_NpcNsrPage")
    L("DT_NpcNsrPage: строк в ассете %d, в источнике %d" % (len(got), len(src)))
    n_lvl = n_obj = n_acc = 0
    for key, want in src.items():
        have = got.get(key)
        if have is None:
            fail("DT_NpcNsrPage: нет строки %s" % key)
            continue
        hs = have.get("Steps", [])
        if len(hs) != len(want["Steps"]):
            fail("%s: шагов %d != %d" % (key, len(hs), len(want["Steps"])))
            continue
        for si, ws in enumerate(want["Steps"]):
            hsubs = hs[si].get("Subs", [])
            if len(hsubs) != len(ws["Subs"]):
                fail("%s шаг %d: под-записей %d != %d" % (key, si, len(hsubs), len(ws["Subs"])))
                continue
            for mi, wsub in enumerate(ws["Subs"]):
                h = hsubs[mi]
                hc = h.get("Cond", {})
                wc = wsub["Cond"]
                if wc["LevelMin"] or wc["LevelMax"]:
                    if eq("%s ш%d п%d уровень" % (key, si, mi),
                          (hc.get("LevelMin"), hc.get("LevelMax")), (wc["LevelMin"], wc["LevelMax"])):
                        n_lvl += 1
                eq("%s ш%d п%d AcceptQuestId" % (key, si, mi), h.get("AcceptQuestId"), wsub["AcceptQuestId"])
                if wsub["AcceptQuestId"] > 0:
                    n_acc += 1
                eq("%s ш%d п%d ObjActive" % (key, si, mi), list(h.get("ObjActive", [])), wsub["ObjActive"])
                ho = h.get("Objs", [])
                if not eq("%s ш%d п%d Objs" % (key, si, mi), len(ho), len(wsub["Objs"])):
                    continue
                if wsub["Objs"]:
                    n_obj += 1
                    for oi, wo in enumerate(wsub["Objs"]):
                        eq("%s ш%d п%d цель%d" % (key, si, mi, oi),
                           (ho[oi].get("QuestId"), ho[oi].get("Kind24"), ho[oi].get("Need136"), ho[oi].get("A52")),
                           (wo["QuestId"], wo["Kind24"], wo["Need136"], wo["A52"]))
    L("DT_NpcNsrPage сверено: под-записей с уровнем %d, с квестом на приём %d, с блоками цели %d"
      % (n_lvl, n_acc, n_obj))

    # ── 2. остальные таблицы ──
    jobs = [
        ("/Game/AsdaAssets/Data/NPC/DT_WarpList", TBL.read_warp, lambda r: str(r["ListIndex"]),
         ("WarpIndex", "MapId", "CellX", "CellY")),
        ("/Game/AsdaAssets/Data/NPC/DT_SnqIct", TBL.read_ict, lambda r: str(r["Key"]),
         ("TalkId", "HasLevel", "LevelMin", "LevelMax", "HasClass", "ClassId", "ClassMode",
          "HasQuest", "QuestDone", "QuestFreeA", "QuestActive", "QuestFreeB", "QuestStage",
          "HasReqItems", "HasEquipped", "HaveItemId", "HasBagTab", "BagTabA", "BagTabB", "BagSlot", "Misc")),
        ("/Game/AsdaAssets/Data/NPC/DT_EpisodeTable", TBL.read_episode, lambda r: str(r["EpisodeId"]),
         ("QuestKey", "Title", "Index")),
        ("/Game/AsdaAssets/Data/NPC/DT_EventItem", TBL.read_event_items, lambda r: str(r["Index"]), ()),
    ]
    for path, reader, key_fn, fields in jobs:
        srows = {key_fn(r): r for r in reader()}
        grows = table_json(path)
        name = path.rsplit("/", 1)[-1]
        L("%s: строк в ассете %d, в источнике %d" % (name, len(grows), len(srows)))
        for k, want in srows.items():
            have = grows.get(k)
            if have is None:
                fail("%s: нет строки %s" % (name, k))
                continue
            for f in fields:
                eq("%s[%s].%s" % (name, k, f), have.get(f), want[f])
            for arr in ("ObjIds", "ObjCounts", "ReqItemIds", "ReqItemCounts", "ItemIds", "ItemCounts"):
                if arr in want:
                    eq("%s[%s].%s" % (name, k, arr), list(have.get(arr, [])), want[arr])

    # ── 3. DT_NpcSnq ──
    srows = {str(r["NpcId"]): r for r in TBL.read_snq()}
    grows = table_json("/Game/AsdaAssets/Data/NPC/DT_NpcSnq")
    L("DT_NpcSnq: строк в ассете %d, в источнике (после отбрасывания дублей) %d" % (len(grows), len(srows)))
    for k, want in srows.items():
        have = grows.get(k)
        if have is None:
            fail("DT_NpcSnq: нет строки %s" % k)
            continue
        for tag in ("Give", "Take"):
            hl = have.get(tag, [])
            if not eq("DT_NpcSnq[%s].%s размер" % (k, tag), len(hl), len(want[tag])):
                continue
            for i, we in enumerate(want[tag]):
                eq("DT_NpcSnq[%s].%s[%d]" % (k, tag, i),
                   (hl[i].get("QuestId"), list(hl[i].get("Keys", []))), (we["QuestId"], we["Keys"]))

    L("СВЕРКА ЗАЛИТЫХ ТАБЛИЦ: расхождений %d" % BAD[0])


# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
