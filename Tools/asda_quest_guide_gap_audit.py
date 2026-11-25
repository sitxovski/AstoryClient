# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""Аудит «цель без указателя» в журнале заданий (08.11.2026, пачка 15, баг 4 «Golden Piggy Bank»).

Кнопки маршрута журнала (705…708) и указатель «Click» берутся только из строк гида `QDT.BIN` (`DT_QuestGuide`) для пары
(задание, стадия): нет строк — у шага только текст описания. Аудит перебирает КАЖДУЮ стадию, у которой есть зачётная цель,
и сверяет три источника:
  * сервер: какие мобы засчитывают стадию (`NpcServer\\Monster\\Monster.ai` квест-блоки + `MonsterConditionTable`, общий
    разборщик `asda_quest_mob_audit.load_server`) и где они стоят (`<карта>.svr`);
  * эталон: строки гида в `QDT.BIN` эталонного клиента (`_P.DATA_ORIG`) — «как в оригинале»;
  * порт: строки гида в `RawAssets/Data/Quest/QuestGuide.json` (= `DT_QuestGuide` с ведомостью `data/quest_guide_fixes.json`).
Классы строк: «есть строки» / «в оригинале нет, у нас добавлены (ведомость)» / «в оригинале нет и у нас нет» / «в оригинале
есть, у нас пропали» (наш дефект). Вывод — CSV и сводка в stdout.
Запуск: python asda_quest_guide_gap_audit.py --server-dir <копия NpcServer (Monster\\, Quest\\, ini\\)> [--out <csv>]
"""
import argparse
import collections
import csv
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def main():
    import asda_paths as _P
    import asda_quest_mob_audit as A
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--server-dir", required=True)
    ap.add_argument("--out", default=os.path.join(_P.PORT_DIR, "Reports", "batch15", "quest_guide_gaps.csv"))
    a = ap.parse_args()
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    srv = A.load_server(a.server_dir)
    orig = collections.defaultdict(list)
    for r in A.parse_qdt(os.path.join(_P.DATA_ORIG, "QDT.BIN")):
        orig[(r["quest"], r["stage"])].append(r)
    raw = os.path.join(_P.PORT_DIR, "AstoryClient", "RawAssets", "Data")
    port = collections.defaultdict(list)
    for r in json.load(open(os.path.join(raw, "Quest", "QuestGuide.json"), encoding="utf-8")):
        port[(r["QuestNo"], r["Stage"])].append(r)
    titles = {}
    for e in json.load(open(os.path.join(raw, "NPC", "EpisodeTable.json"), encoding="utf-8")):
        titles.setdefault(e["QuestKey"], e["EpisodeId"])
    rows = []
    for (q, st), lst in sorted(srv["kills"].items()):
        mobs = sorted({m for m, _item, _rate in lst})
        where = {}
        for m in mobs:
            for mi, per in srv["spawns"].items():
                if m in per:
                    where.setdefault(srv["map_name"][mi], 0)
                    where[srv["map_name"][mi]] += len(per[m])
        o, p = len(orig.get((q, st), [])), len(port.get((q, st), []))
        if o and p:
            cls = "есть строки"
        elif not o and p:
            cls = "в оригинале нет, у нас добавлены (ведомость)"
        elif o and not p:
            cls = "в оригинале есть, у нас ПРОПАЛИ (наш дефект)"
        else:
            cls = "в оригинале нет и у нас нет" if where else "нет строк; мобы цели не спавнятся на картах"
        rows.append(dict(quest=q, stage=st, episode=titles.get(q, ""), mobs=" ".join(map(str, mobs)),
                         spawns="; ".join("%s:%d" % kv for kv in sorted(where.items())) or "НЕТ", orig_rows=o, port_rows=p,
                         status=cls))
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, "w", encoding="utf-8-sig", newline="") as f:
        w = csv.DictWriter(f, list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    cnt = collections.Counter(r["status"] for r in rows)
    print("стадий с зачётом за мобов: %d" % len(rows))
    for k, v in cnt.most_common():
        print("  %-50s %d" % (k, v))
    for r in rows:
        if r["status"] not in ("есть строки", "в оригинале нет, у нас добавлены (ведомость)"):
            print("  %s: задание %d стадия %d (эпизод %s), мобы %s, спавн %s" % (r["status"], r["quest"], r["stage"], r["episode"],
                                                                            r["mobs"], r["spawns"]))
    print("→ %s" % a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
