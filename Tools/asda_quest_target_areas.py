# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""Места мобов целей заданий для значков карты мира → `RawAssets/Data/Quest/QuestTargetArea.json` → `DT_QuestTargetArea`.

Зачем (31.10.2026, решение владельца «добавить значки целей на карту мира», пометка [ПОРТ]). Значок цели шага берётся из гида
`QDT.bin` (`DT_QuestGuide`, X/Y цели), но у 63 шагов обычных заданий и 53 заданий доски объявлений координат в гиде нет: цель —
«добыть предмет с мобов». Где эти мобы, знает только СЕРВЕР:
  * `NpcServer\\Monster\\Monster.ai` — квест-блок типа моба с +402: (задание, шаг, условие, предмет, шанс) — какой тип моба на
    каком шаге задания даёт предмет (разбор — `asda_quest_mob_audit.load_server`, общая точка);
  * `NpcServer\\Monster\\<карта>.svr` — точки спавна по типу (`ini\\MapInfo.ini`: MonsterInfo_<номер карты>).
Точки одного типа на карте группируются в зоны (жадно, радиус `RADIUS` клеток), центр зоны — среднее точек. Строка таблицы —
(задание, шаг, тип моба, предмет, карта, центр зоны, число точек). Клиент берёт её ТОЛЬКО для шагов без координат гида.

Запуск:
  python asda_quest_target_areas.py --fetch          — снять сервер (scp, только чтение), записать JSON, отчёт
  python asda_quest_target_areas.py --server-dir D   — то же из готовой копии
  Tools\\run_headless_safe.py Tools\\asda_quest_target_areas.py — под UE: залить JSON в DT_QuestTargetArea (сети нет)
"""
import collections
import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_paths as _P
import asda_item_dedup as _DD   # ★ 01.11.2026 item_dedup: ссылки на предметы-дубли -> основной код (карта Tools/data/item_dedup_map.json)

OUT_JSON = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Data", "Quest", "QuestTargetArea.json")
PKG = "/Game/AsdaAssets/Data/Quest"
DT_NAME = "DT_QuestTargetArea"
RS = "/Script/AsdaReborn.AsdaQuestTargetAreaRow"
RADIUS = 40   # клеток: точки одного типа ближе этого к центру зоны — та же зона


def log(msg):
    print("[quest-target-areas] %s" % msg)


def clusters(points):
    """Жадная группировка точек (x, y) в зоны: [(cx, cy, n)]. Порядок точек файла сохраняется — результат детерминирован."""
    zones = []   # [sumx, sumy, n]
    for x, y in points:
        for z in zones:
            cx, cy = z[0] / z[2], z[1] / z[2]
            if (cx - x) ** 2 + (cy - y) ** 2 <= RADIUS * RADIUS:
                z[0] += x; z[1] += y; z[2] += 1
                break
        else:
            zones.append([x, y, 1])
    return [(int(round(z[0] / z[2])), int(round(z[1] / z[2])), z[2]) for z in zones]


def build(server_dir):
    import asda_quest_mob_audit as A
    srv = A.load_server(server_dir)
    dmap = _DD.active_map()   # ★ 01.11.2026 item_dedup: квестовый предмет-дубль -> основной код (ASDA_NO_DEDUP_MAP=1 — как сервер)
    rows = []
    for (quest, step), lst in sorted(srv["kills"].items()):
        for mob, item, _rate in lst:
            for map_id, per in sorted(srv["spawns"].items()):
                pts = per.get(mob)
                if not pts:
                    continue
                for cx, cy, n in clusters(pts):
                    rows.append(dict(QuestNo=quest, Stage=step, MobType=mob, ItemId=_DD.remap(item, dmap), MapId=map_id,
                                     TileX=cx, TileY=cy, SpawnCount=n))
    return rows


def main():
    try:
        import unreal  # noqa: F401
        in_ue = True
    except Exception:
        in_ue = False
    if in_ue:
        from asda_dt_common import fill_dt
        text = open(OUT_JSON, encoding="utf-8").read()
        import unreal
        dt = fill_dt(unreal, PKG, DT_NAME, RS, text, log, fmt="json", missing_hint="Public/Quest/AsdaQuestTypes.h")
        log("ASDA_WRITES_ASSET: %s/%s" % (PKG, DT_NAME))
        log("ЗАВЕРШЕНО: %s %s" % (DT_NAME, "залит" if dt is not None else "НЕ залит"))
        return
    import argparse
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--fetch", action="store_true")
    ap.add_argument("--server-dir")
    a = ap.parse_args()
    sdir = a.server_dir
    if a.fetch:
        import asda_quest_mob_audit as A
        sdir = tempfile.mkdtemp(prefix="asda_qta_")
        A.fetch_server(sdir)
    if not sdir:
        log("нет сервера (--fetch или --server-dir)")
        return 2
    rows = build(sdir)
    # Ключ строки DataTable — сквозной номер в поле "Name" (как у `asda_dt_common.json_rows`).
    out = []
    for i, r in enumerate(rows):
        d = dict(r)
        d["Name"] = str(i)
        out.append(d)
    os.makedirs(os.path.dirname(OUT_JSON), exist_ok=True)
    with open(OUT_JSON, "w", encoding="utf-8", newline="\n") as f:
        json.dump(out, f, ensure_ascii=False, indent=0)
    by_map = collections.Counter(r["MapId"] for r in rows)
    steps = {(r["QuestNo"], r["Stage"]) for r in rows}
    log("строк %d (шагов заданий %d), по картам: %s" % (len(rows), len(steps), dict(sorted(by_map.items()))))
    log("записано: %s" % OUT_JSON)
    return 0


if __name__ == "__main__":
    sys.exit(main())
