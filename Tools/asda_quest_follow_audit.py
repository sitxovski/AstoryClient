# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""Аудит кнопки «Следовать» журнала заданий: точка маршрута (гид QDT) <-> фактическое место цели (31.10.2026, баг 6).

Зачем. Кнопки 705…708 окна 700 ведут персонажа в клетку (X, Y) строки гида `QDT.BIN`
(эталон `QuestGuide_BuildObjectiveText@0xC68E00` -> `miniMapClick_TravelTo@0xC692D0`: игровая точка 100·X+50, 100·Y+50;
своя карта — путь напрямую, чужая — межкартовый автопуть). Никакого другого источника точки у эталона нет: ни спавна,
ни позиции NPC, ни ближайшей точки. Поэтому «ведёт не туда» = (а) клетка гида далеко от места цели, (б) не та карта,
(в) у строки нет координат (кнопка не ведёт никуда), (г) порт читает гид не так, как эталон.

Что делает скрипт (ВСЕ строки гида порта и эталона):
  * точка порта  — `RawAssets/Data/Quest/QuestGuide.json` (MINE + ведомость `data/quest_guide_fixes.json`);
  * точка эталона — сырой `QDT.BIN` эталонного клиента (`Donors/AsdaChroniclesInterlude/Data`) по той же паре (квест, шаг, цель);
  * место цели: NPC — `npc_list.BIN` (+0x6C карта, +0x70/+0x74 мировые X/Y, клетка = /100); моб — спавн сервера
    (`<карта>.svr` по `MapInfo.ini`) типа цели (строка + 1) и всех типов, которые сервер засчитывает за этот шаг (`Monster.ai`);
  * координаты из текста шага (RU/EN), если есть, — третий независимый ориентир;
  * группировка расхождений по причине -> `Reports/bug_quest_follow/mismatch.csv` и сводка в консоль.

Источники — только чтение. Сервер — копия по scp (`--fetch`) или готовая папка (`--server-dir`), шару \\\\@@ASTORY_SERVER_HOSTNAME@@ не трогаем.
Запуск:  python asda_quest_follow_audit.py --server-dir <копия NpcServer> [--out <папка>]
Код возврата: 0 — расхождений нет (кроме принятых решений), 1 — есть, 2 — нет данных.
"""
import argparse
import collections
import csv
import json
import math
import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_paths as _P  # noqa: E402
import asda_quest_mob_audit as A  # noqa: E402  (общие разборщики QDT/Monster.ai/.svr — не дублируем)

RAW = os.path.join(_P.PORT_DIR, "AstoryClient", "RawAssets")
NPC_HDR, NPC_STRIDE = 0x14, 0x88           # npc_list.BIN (как `asda_import_npc_quest_data.read_npcs`)
NPC_NEAR = 10                              # клеток: точка гида у NPC (NPC стоит, разброс — подход к нему)
MOB_NEAR = A.NEAR_CELLS                    # клеток: точка гида у спавна моба (как аудит мобов 25.10)
ZONE_R = 40                                # радиус группировки спавна в зоны (как `asda_quest_target_areas.RADIUS`)


def read_npcs(path):
    """npc_list.BIN -> {NpcId: [(карта, клетка X, клетка Y)]}."""
    b = open(path, "rb").read()
    cnt = struct.unpack_from("<i", b, 4)[0]
    out = collections.defaultdict(list)
    for i in range(cnt):
        r = NPC_HDR + NPC_STRIDE * i
        nid = struct.unpack_from("<i", b, r)[0]
        mp = struct.unpack_from("<i", b, r + 0x6C)[0]
        wx, wy = struct.unpack_from("<ff", b, r + 0x70)
        out[nid].append((mp, int(math.floor(wx / 100.0)), int(math.floor(wy / 100.0))))
    return out


def read_server_npcs(server_dir):
    """Сервер `NpcInfo/<карта>.npc` (по `MapInfo.ini` NonPlayerInfo_NNN): записи 0xDD б, +4 u16 NpcId, +8/+A i16 клетка X/Y
    (GameServer `LoadNpcInfo`, GameServer.exe.c:26489: клетка < 0 — NPC выключен). Это МЕСТО NPC в игре: клиент
    усыновляет акторов по серверному спавну. -> {NpcId: [(карта, X, Y)]}."""
    import re
    ini = open(os.path.join(server_dir, "ini", "MapInfo.ini"), "rb").read().decode("cp1251", "replace")
    out = collections.defaultdict(list)
    files = {f.lower(): f for f in os.listdir(os.path.join(server_dir, "NpcInfo"))}
    for m in re.finditer(r'NonPlayerInfo_(\d+)\s*=\s*"([^"]+)"', ini):
        mp, fn = int(m.group(1)), files.get(m.group(2).lower())
        if not fn:
            continue
        b = open(os.path.join(server_dir, "NpcInfo", fn), "rb").read()
        for i in range(len(b) // 0xDD):
            nid, tx, ty = struct.unpack_from("<H", b, i * 0xDD + 4)[0], *struct.unpack_from("<hh", b, i * 0xDD + 8)
            if tx >= 0 and ty >= 0:
                out[nid].append((mp, tx, ty))
    return out


def zones(points):
    """Жадная группировка точек спавна в зоны (как `asda_quest_target_areas.clusters`): [(cx, cy, n)]."""
    zs = []
    for x, y in points:
        for z in zs:
            if (z[0] / z[2] - x) ** 2 + (z[1] / z[2] - y) ** 2 <= ZONE_R * ZONE_R:
                z[0] += x; z[1] += y; z[2] += 1
                break
        else:
            zs.append([x, y, 1])
    return [(int(round(z[0] / z[2])), int(round(z[1] / z[2])), z[2]) for z in zs]


def dist(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def build(server_dir):
    srv = A.load_server(server_dir)
    guide = json.load(open(os.path.join(RAW, "Data", "Quest", "QuestGuide.json"), encoding="utf-8"))
    qdt_orig = A.parse_qdt(os.path.join(_P.DATA_ORIG, "QDT.BIN"))
    npc_list = read_npcs(os.path.join(_P.DATA, "NPC", "npc_list.BIN"))
    npc_srv = read_server_npcs(server_dir)
    npc_orig = read_npcs(os.path.join(_P.DATA_ORIG, "NPC", "npc_list.BIN"))
    book = {r["Name"]: r for r in json.load(open(os.path.join(RAW, "Data", "Quest", "QuestBook.json"), encoding="utf-8"))}
    ep = {r["QuestKey"]: r for r in json.load(open(os.path.join(RAW, "Data", "NPC", "EpisodeTable.json"), encoding="utf-8"))}
    book_en = A.load_csv_map(os.path.join(A.L10N_EN, "DT_Loc_QuestBook_en.csv"))
    mob_ru = A.parse_moblist(os.path.join(_P.DATA, "Mob", "Mob list.BIN"), "cp1251")

    def step_xy(q, st):
        e = ep.get(q)
        if not e:
            return []
        b = book.get(e["QstFile"])
        t = (b["Steps"][st] if b and st < len(b["Steps"]) else "") + " " + book_en.get("%s.Steps.%d" % (e["QstFile"], st), "")
        return [(int(x), int(y)) for x, y in A.TXT_XY.findall(t)]

    # Эталон: строки гида по ключу (квест, шаг) В ПОРЯДКЕ ФАЙЛА — так их перебирает `QuestGuide_BuildObjectiveText`.
    orig_by_key = collections.defaultdict(list)
    for r in qdt_orig:
        orig_by_key[(r["quest"], r["stage"])].append(r)
    port_by_key = collections.defaultdict(list)
    for g in guide:
        port_by_key[(g["QuestNo"], g["Stage"])].append(g)

    def route_lines(rows, get):
        """Какие строки эталонный цикл покажет на кнопках: перебор по порядку, стоп на ContinueFlag==0,
        пятая и далее строки ПЕРЕЗАПИСЫВАЮТ четвёртую кнопку (`if (lineIdx + 1 < 4) ++lineIdx`)."""
        out = [None] * 4
        li = 0
        for r in rows:
            out[li] = r
            if get(r, "cont") == 0:
                break
            if li + 1 < 4:
                li += 1
        return [r for r in out if r is not None]

    res = []
    for key in sorted(set(port_by_key) | set(orig_by_key)):
        q, st = key
        p_lines = route_lines(sorted(port_by_key.get(key, []), key=lambda g: g["Index"]),
                              lambda r, k: r["ContinueFlag"] if k == "cont" else None)
        o_lines = route_lines(orig_by_key.get(key, []), lambda r, k: r[k])
        n = max(len(p_lines), len(o_lines))
        txy = step_xy(q, st)
        kills = srv["kills"].get((q, st), [])
        srv_types = sorted({t for t, _, _ in kills})
        for i in range(n):
            g = p_lines[i] if i < len(p_lines) else None
            o = o_lines[i] if i < len(o_lines) else None
            kind = g["TargetKind"] if g else o["kind"]
            tid = g["TargetId"] if g else o["tid"]
            mp = g["MapId"] if g else o["map"]
            px, py = (g["X"], g["Y"]) if g else (None, None)
            ox, oy = (o["x"], o["y"]) if o else (None, None)
            row = dict(quest=q, stage=st, line=i, index=g["Index"] if g else "", kind="NPC" if kind == 0 else "моб",
                       tid=tid, map=mp, port_x=px, port_y=py, orig_x=ox, orig_y=oy,
                       orig_kind=("" if not o else ("NPC" if o["kind"] == 0 else "моб")),
                       orig_tid=("" if not o else o["tid"]), orig_map=("" if not o else o["map"]),
                       title=(book.get(ep[q]["QstFile"], {}).get("Title", "") if q in ep else "").strip('"'),
                       target="", target_map="", target_x="", target_y="", dist="", cause="", fix_x="", fix_y="",
                       fix_map="", note="", text_xy=";".join("%d,%d" % t for t in txy))
            have_xy = g is not None and px != -1 and py != -1
            if kind == 0:
                places = npc_srv.get(tid, []) or npc_list.get(tid, [])
                row["target"] = "NPC %d" % tid
                same = [p for p in places if p[0] == mp]
                if not places:
                    row["cause"] = "NPC нет в npc_list"
                elif not same:
                    p = places[0]
                    row.update(target_map=p[0], target_x=p[1], target_y=p[2], cause="NPC на другой карте",
                               fix_map=p[0], fix_x=p[1], fix_y=p[2])
                else:
                    best = min(same, key=lambda p: dist((p[1], p[2]), (px, py)) if have_xy else 0)
                    row.update(target_map=best[0], target_x=best[1], target_y=best[2])
                    if have_xy:
                        d = dist((best[1], best[2]), (px, py))
                        row["dist"] = "%.0f" % d
                        if d > NPC_NEAR:
                            row.update(cause="точка гида далеко от NPC", fix_map=best[0], fix_x=best[1], fix_y=best[2])
                    else:
                        row.update(cause="нет координат (NPC)", fix_map=best[0], fix_x=best[1], fix_y=best[2])
                if not npc_srv.get(tid):
                    row["note"] += "NPC нет в спавне сервера (место из npc_list); "
                lp = [p for p in npc_list.get(tid, []) if p[0] == mp]
                if npc_srv.get(tid) and lp and row["target_x"] != "" and dist((lp[0][1], lp[0][2]), (row["target_x"], row["target_y"])) > NPC_NEAR:
                    row["note"] += "npc_list клиента ставит NPC в %s (сервер %s,%s); " % (lp[0][1:], row["target_x"], row["target_y"])
                if tid in npc_orig and npc_orig[tid] != npc_list.get(tid):
                    row["note"] += "позиция NPC в npc_list эталона %s; " % npc_orig[tid]
            else:
                typ = tid + 1
                row["target"] = "моб %d (%s)" % (typ, mob_ru[tid]["name"] if 0 <= tid < len(mob_ru) else "?")
                cnt_types = [typ] + [t for t in srv_types if t != typ and t in srv_types]
                sp_map = srv["spawns"].get(mp, {})
                # Парный вариант того же моба (соседний тип с тем же именем, сервер засчитывает его за этот шаг) — тоже цель.
                nm = mob_ru[tid]["name"] if 0 <= tid < len(mob_ru) else None
                pair = [t for t in srv_types if t != typ and 0 < t <= len(mob_ru) and mob_ru[t - 1]["name"] == nm]
                pts_named = sp_map.get(typ, []) + [p for t in pair for p in sp_map.get(t, [])]
                pts_cnt = [p for t in srv_types for p in sp_map.get(t, [])]
                pts = pts_named or pts_cnt
                if not pts:
                    # Моба нет на карте гида — где он вообще стоит?
                    else_maps = sorted({m for m, per in srv["spawns"].items() for t in cnt_types if per.get(t)})
                    row["cause"] = "моба нет на карте гида"
                    row["note"] += "есть на картах %s; " % else_maps
                    if else_maps:
                        m2 = else_maps[0]
                        per = srv["spawns"][m2]
                        zs = zones(per.get(typ) or [p for t in srv_types for p in per.get(t, [])])
                        z = max(zs, key=lambda z: z[2])
                        row.update(target_map=m2, target_x=z[0], target_y=z[1], fix_map=m2, fix_x=z[0], fix_y=z[1])
                else:
                    zs = zones(pts)
                    if have_xy:
                        dn = min(dist(p, (px, py)) for p in pts)
                        z = min(zs, key=lambda z: dist((z[0], z[1]), (px, py)))
                        row.update(target_map=mp, target_x=z[0], target_y=z[1], dist="%.0f" % dn)
                        if not pts_named:
                            row["note"] += "тип %d на карте не стоит, стоит засчитываемый %s; " % (typ, srv_types)
                        if dn > MOB_NEAR:
                            # Предлагаемая точка — зона спавна цели, БЛИЖАЙШАЯ к прежней метке (район, который имел в виду гид).
                            zb = min(zs, key=lambda z: dist((z[0], z[1]), (px, py)))
                            row.update(cause="точка гида далеко от спавна", fix_map=mp, fix_x=zb[0], fix_y=zb[1])
                    else:
                        zb = max(zs, key=lambda z: z[2])
                        row.update(target_map=mp, target_x=zb[0], target_y=zb[1], cause="нет координат (моб)",
                                   fix_map=mp, fix_x=zb[0], fix_y=zb[1])
                if typ not in srv_types and srv_types:
                    row["note"] += "сервер засчитывает за шаг типы %s, не %d; " % (srv_types, typ)
            # Порт vs эталон: та же ли строка гида.
            if g and o and (g["TargetKind"], g["TargetId"], g["MapId"], g["X"], g["Y"]) != (o["kind"], o["tid"], o["map"], o["x"], o["y"]):
                row["note"] += "порт!=эталон (ведомость/MINE); "
            if g is None:
                row["note"] += "строки нет в порту; "
            if o is None:
                row["note"] += "строки нет в эталоне (MINE/Флэммио); "
            if txy and have_xy:
                dt = min(dist(t, (px, py)) for t in txy)
                if dt > NPC_NEAR:
                    row["note"] += "текст шага называет %s (%.0f кл. от гида); " % (txy, dt)
            res.append(row)
    return res, srv


COLS = ["quest", "stage", "line", "index", "title", "kind", "tid", "target", "map", "port_x", "port_y",
        "orig_x", "orig_y", "orig_kind", "orig_tid", "orig_map", "target_map", "target_x", "target_y", "dist",
        "cause", "fix_map", "fix_x", "fix_y", "text_xy", "note"]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--server-dir")
    ap.add_argument("--fetch", action="store_true")
    ap.add_argument("--out", default=os.path.join(_P.PORT_DIR, "Reports", "bug_quest_follow"))
    a = ap.parse_args(argv)
    sd = a.server_dir
    if a.fetch:
        sd = tempfile.mkdtemp(prefix="asda_qfollow_")
        A.fetch_server(sd)
    if not sd or not os.path.isdir(sd):
        print("нет копии сервера: --fetch или --server-dir")
        return 2
    rows, _srv = build(sd)
    os.makedirs(a.out, exist_ok=True)
    with open(os.path.join(a.out, "all_routes.csv"), "w", encoding="utf-8-sig", newline="") as f:
        w = csv.DictWriter(f, COLS, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)
    bad = [r for r in rows if r["cause"]]
    with open(os.path.join(a.out, "mismatch.csv"), "w", encoding="utf-8-sig", newline="") as f:
        w = csv.DictWriter(f, COLS, extrasaction="ignore")
        w.writeheader()
        w.writerows(bad)
    c = collections.Counter(r["cause"] for r in bad)
    print("строк маршрута: %d (NPC %d, моб %d); расхождений %d" % (
        len(rows), sum(r["kind"] == "NPC" for r in rows), sum(r["kind"] == "моб" for r in rows), len(bad)))
    for k, v in c.most_common():
        print("  %-32s %d (заданий %d)" % (k, v, len({r["quest"] for r in bad if r["cause"] == k})))
    print("порт!=эталон: %d строк" % sum("порт!=эталон" in r["note"] for r in rows))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
