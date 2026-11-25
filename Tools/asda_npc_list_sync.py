# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""Места NPC клиента (`npc_list.BIN` -> `DT_NpcQuestNpc`) по серверному спавну: генератор ведомости `data/npc_list_fixes.json`
(31.10.2026, решение владельца «у сервера верные значения — синхронизируй позиции, где сервер = правда»).

Зачем. Место NPC в игре ставит сервер: пакет 4067 шлёт GameServer по своему спавну `GameServer/NpcData_Info/<имя>.npc`
(`LoadNpcInfo`, GameServer.c:22578: имя — столбец 1 строки карты `MapData_Info/MapInfo.txt`, столбец 0 — номер карты,
чтение — GameServer.c:71968); у NpcServer та же раскладка — `NpcServer/NpcInfo/<файл>` по `ini/MapInfo.ini` NonPlayerInfo_NNN.
Запись 0xDD б: +4 u16 NpcId, +8/+A i16 клетка, клетка < 0 — NPC выключен (GameServer.exe.c:26489). Читаются ВСЕ копии стойки
(`stack_layout.py`: GS_FOLDERS и NPC_FOLDERS — поле 1, поле 2, Indun); любое расхождение копий — код 1 (какая из них правда,
решает человек). Правда для клиента — копия `GameServer` (поле 1, она шлёт 4067).
Эталон `npcMgr_spawnWorldEntityFromTable@0xF90600` (etalon/npc_mob_pet.c:9411) ставит сущность так:
  * класс 0/4 и 2 (`npc_list +0x7C`) — в клетку СЕРВЕРА: игровая точка `клетка·100 + 50`;
  * класс 1/3/5 — в точные float `npc_list +0x70/+0x74` (клетку сервера не берёт).
А метки «!»/«?» мини-карты и карты мира, значок NPC сдачи, подсказка «где выдающий» берут место из `npc_list`
(`npcUi_BuildNpcActionList@0xD97650`). У части NPC класса 0/2/4 `npc_list` расходится с сервером (эталонный файл такой же —
дефект оригинала), поэтому метка стоит не там, где NPC.

Что делает (только чтение источников):
  * класс 0/2/4 со спавном на сервере -> строка ведомости, если `(карта, X, Y)` npc_list ≠ серверной точке
    `(карта, клетка·100+50)`: `set` — главное место (на карте клиента — ближайшее к клетке клиента; иначе — на карте, уровень
    которой есть в порте, `asda_map_registry.json` has_level; иначе первое место сервера), `extra` — остальные места сервера
    (вторая и далее строки таблицы `<NpcId>_<n>`; NPC стоит в нескольких местах);
  * класс 1/3/5 — НЕ правится: игра ставит их в float npc_list; проверяется, что расхождение с сервером — только округление
    (|float − (клетка·100+50)| ≤ 100 по обеим осям, т. е. сервер держит соседнюю клетку той же точки; 100 — точка ровно на
    границе клеток, как у NPC 89: Y 5750, сервер — клетка 58). Иначе — строка «!!» и код 1;
  * NPC без спавна на сервере — не трогаем (сервер их не ставит, сверять не с чем);
  * `Reports/npc_list_sync/mismatch.csv` — все NPC, у которых клиент ≠ сервер, с причиной и решением;
  * `data/npc_server_spawn.json` — слепок серверного спавна (места всех NPC + md5 копий): по нему импорт
    (`asda_import_npc_quest_data.verify`) без сервера проверяет, что строки таблицы класса 0/2/4 стоят на местах сервера.

Запуск:  python asda_npc_list_sync.py --server-dir <папка с копиями: <Папка NpcServer>/NpcInfo + ini/MapInfo.ini,
                                       <Папка GameServer>/NpcData_Info + MapData_Info/MapInfo.txt> | --fetch (scp всех копий)
         [--check]  — только сверить ведомость и слепок с источниками (код 1 — устарели или копии расходятся)
Код возврата: 0 — ок, 1 — копии сервера расходятся / расхождение не объяснено / ведомость устарела, 2 — нет данных.
"""
import argparse
import collections
import csv
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_paths as _P  # noqa: E402
import asda_quest_follow_audit as F  # noqa: E402  (read_server_npcs — разбор NpcInfo/*.npc, не дублируем)
sys.path.insert(0, os.path.join(_P.PORT_DIR, "AstoryServer", "Patches", "AsdaPatch", "tools"))
import stack_layout as SL  # noqa: E402  (единственный список папок стойки)

DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
FIXES = os.path.join(DATA, "npc_list_fixes.json")          # ведомость мест (применяет импорт)
SPAWN = os.path.join(DATA, "npc_server_spawn.json")        # слепок серверного спавна (сверка импорта)
REGISTRY = os.path.join(DATA, "asda_map_registry.json")    # реестр карт: есть ли уровень в порте
OUT = os.path.join(_P.PORT_DIR, "Reports", "npc_list_sync")
SSH_KEY = r"C:\Users\@@ASTORY_DEV_USER@@\.ssh\@@ASTORY_SSH_KEY_NAME@@"            # только чтение (scp)
SSH_HOST = "Administrator@@@ASTORY_SERVER_VPN_IP@@"
SRV_ROOT = "@@ASTORY_SERVER_ROOT_FWD@@/Working_ASDA/v2"
TRUTH = "GameServer"              # копия, которая шлёт клиенту 4067
SERVER_CLS = (0, 2, 4)            # классы, которые эталон ставит в клетку сервера
TABLE_CLS = (1, 3, 5)             # классы, которые эталон ставит в float npc_list


def fetch_server(dst):
    """Копии спавна всех папок стойки по scp (только чтение): NpcServer-ы — NpcInfo/* + ini/MapInfo.ini,
    GameServer-ы — NpcData_Info/*.npc + MapData_Info/MapInfo.txt."""
    jobs = [(d, sub, src) for d in SL.NPC_FOLDERS for sub, src in (("NpcInfo", "NpcInfo/*"), ("ini", "ini/MapInfo.ini"))]
    jobs += [(d, sub, src) for d in SL.GS_FOLDERS
             for sub, src in (("NpcData_Info", "NpcData_Info/*.npc"), ("MapData_Info", "MapData_Info/MapInfo.txt"))]
    for d, sub, src in jobs:
        os.makedirs(os.path.join(dst, d, sub), exist_ok=True)
        r = subprocess.run(["scp", "-q", "-i", SSH_KEY, "-o", "BatchMode=yes", "-o", "ConnectTimeout=15",
                            "%s:%s/%s/%s" % (SSH_HOST, SRV_ROOT, d, src), os.path.join(dst, d, sub) + os.sep],
                           capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit("scp %s/%s: %s" % (d, src, r.stderr.strip()))


def read_gs_npcs(gs_dir):
    """GameServer `NpcData_Info/<имя>.npc` по `MapData_Info/MapInfo.txt` (первая строка — число карт, далее «номер имя …»).
    Формат записи — как у NpcServer. -> {NpcId: [(карта, X, Y)]}."""
    out = collections.defaultdict(list)
    files = {f.lower(): f for f in os.listdir(os.path.join(gs_dir, "NpcData_Info"))}
    lines = open(os.path.join(gs_dir, "MapData_Info", "MapInfo.txt"), "rb").read().decode("cp1251", "replace").splitlines()
    for ln in lines[1:]:
        p = ln.split()
        if len(p) < 2:
            continue
        mp, fn = int(p[0]), files.get(p[1].lower() + ".npc")
        if not fn:
            continue                                   # у карты нет файла спавна (^MineWar^) — NPC нет
        b = open(os.path.join(gs_dir, "NpcData_Info", fn), "rb").read()
        for i in range(len(b) // 0xDD):
            nid, tx, ty = struct.unpack_from("<H", b, i * 0xDD + 4)[0], *struct.unpack_from("<hh", b, i * 0xDD + 8)
            if tx >= 0 and ty >= 0:
                out[nid].append((mp, tx, ty))
    return out


def read_all(root):
    """Все копии стойки -> (спавн копии TRUTH, {папка: md5 спавна}, [расхождения копий с TRUTH])."""
    copies = {d: F.read_server_npcs(os.path.join(root, d)) for d in SL.NPC_FOLDERS}
    copies.update({d: read_gs_npcs(os.path.join(root, d)) for d in SL.GS_FOLDERS})
    norm = {d: {k: sorted(v) for k, v in c.items()} for d, c in copies.items()}
    md5 = {d: hashlib.md5(json.dumps(sorted(n.items())).encode()).hexdigest() for d, n in norm.items()}
    diffs = ["%s ≠ %s: NPC %d %s / %s" % (d, TRUTH, k, n.get(k), norm[TRUTH].get(k))
             for d, n in norm.items() for k in sorted(set(n) | set(norm[TRUTH])) if n.get(k) != norm[TRUTH].get(k)]
    return norm[TRUTH], md5, diffs


def level_maps():
    """Номера карт, уровень которых есть в порте (реестр карт — единый источник)."""
    d = json.load(open(REGISTRY, encoding="utf-8"))
    return {m["id"] for m in (d if isinstance(d, list) else d["maps"]) if m.get("has_level")}


def pt(place):
    """Серверная клетка (карта, X, Y) -> место строки таблицы (карта, игровые X/Y центра клетки)."""
    return {"MapId": place[0], "WorldX": place[1] * 100.0 + 50.0, "WorldY": place[2] * 100.0 + 50.0}


def build(srv):
    """Спавн сервера -> (строки ведомости, строки отчёта, число необъяснённых)."""
    import asda_import_npc_quest_data as I        # разбор npc_list БЕЗ ведомости (сырой источник)
    lv = level_maps()
    fixes, report, bad = [], [], 0
    for r in I.read_npcs(apply_fixes=False):
        nid, cls = r["NpcId"], r["NpcType"]
        was = {"MapId": r["MapId"], "WorldX": r["WorldX"], "WorldY": r["WorldY"]}
        cell = (r["MapId"], int(r["WorldX"] // 100), int(r["WorldY"] // 100))
        places = [tuple(p) for p in srv.get(nid, [])]
        rep = dict(npc=nid, nsr=r["NsrName"], cls=cls, cli_map=cell[0], cli_x=cell[1], cli_y=cell[2],
                   cli_wx=r["WorldX"], cli_wy=r["WorldY"], srv=";".join("%d:%d,%d" % p for p in places), kind="", action="")
        if not places:
            rep.update(kind="нет спавна на сервере", action="не трогаем: сервер NPC не ставит, сверять не с чем")
            report.append(rep)
            continue
        if cls in TABLE_CLS:
            near = [p for p in places if p[0] == r["MapId"]
                    and abs(r["WorldX"] - (p[1] * 100 + 50)) <= 100 and abs(r["WorldY"] - (p[2] * 100 + 50)) <= 100]
            if near and all((p[1], p[2]) == cell[1:] for p in near):
                continue                                   # та же клетка
            if near:
                rep.update(kind="другая клетка (округление)", action=(
                    "не трогаем: класс %d эталон ставит в float npc_list (npcMgr_spawnWorldEntityFromTable@0xF90600), "
                    "сервер держит соседнюю клетку той же точки (≤100 ед.)" % cls))
            else:
                bad += 1
                rep.update(kind="!! класс %d: реальная разница с сервером" % cls, action="разобрать вручную")
            report.append(rep)
            continue
        if cls not in SERVER_CLS:
            bad += 1
            rep.update(kind="!! неизвестный класс %d" % cls, action="разобрать вручную")
            report.append(rep)
            continue
        # Класс 0/2/4: место = сервер. Главная строка — на карте клиента (ближайшее), иначе на карте с уровнем в порте.
        same = sorted((p for p in places if p[0] == r["MapId"]), key=lambda p: abs(p[1] - cell[1]) + abs(p[2] - cell[2]))
        on_level = [p for p in places if p[0] in lv]
        main = same[0] if same else (on_level[0] if on_level else places[0])
        rest = [p for p in places if p is not main]
        want = pt(main)
        if want == was and not rest:
            continue
        fixes.append({"npc": nid, "nsr": r["NsrName"], "cls": cls, "was": was, "set": want,
                      "extra": [pt(p) for p in rest], "src": ";".join("%d:%d,%d" % p for p in places)})
        if (main[1], main[2]) == cell[1:] and main[0] == cell[0] and not rest:
            continue                                       # клетка та же — только точка в центр клетки, в отчёт не идёт
        if r["MapId"] < 0:
            kind = "в клиенте нет места"
        elif not same:
            kind = "другая карта"
        else:
            kind = "другая клетка"
        if rest:
            kind += "; на сервере мест %d" % len(places)
        rep.update(kind=kind, action="по серверу [ИСПРАВЛЕНИЕ]" + (" + строки %s" % ", ".join(
            "%d_%d" % (nid, k + 2) for k in range(len(rest))) if rest else ""))
        report.append(rep)
    return fixes, report, bad


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--server-dir")
    ap.add_argument("--fetch", action="store_true")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args(argv)
    sd = a.server_dir
    if a.fetch:
        sd = tempfile.mkdtemp(prefix="asda_npcsync_")
        fetch_server(sd)
    if not sd or not os.path.isdir(sd):
        print("нет копии сервера: --fetch или --server-dir")
        return 2
    srv, md5, diffs = read_all(sd)
    print("копии спавна: %s" % ", ".join("%s %s" % (d, h[:8]) for d, h in md5.items()))
    for d in diffs[:40]:
        print("  !! " + d)
    if diffs:
        print("!! копии сервера расходятся (%d NPC) — правда не определена, ведомость не строится" % len(diffs))
        return 1
    fixes, report, bad = build(srv)
    doc = {"note": "Места NPC класса 0/2/4 по серверному спавну (копия %s; 31.10.2026, [ИСПРАВЛЕНИЕ]); строит "
                   "Tools/asda_npc_list_sync.py, применяет Tools/asda_import_npc_quest_data.py (откат ASDA_NPCLIST_FIXES=0)." % TRUTH,
           "rows": fixes}
    spawn = {"note": "Слепок серверного спавна NPC (все копии стойки совпадают; правда — %s). Строит Tools/asda_npc_list_sync.py, "
                     "читает verify() Tools/asda_import_npc_quest_data.py." % TRUTH,
             "copies": md5, "spawn": {str(k): [list(p) for p in v] for k, v in sorted(srv.items())}}
    if a.check:
        old = json.load(open(FIXES, encoding="utf-8")) if os.path.isfile(FIXES) else {"rows": []}
        olds = json.load(open(SPAWN, encoding="utf-8")) if os.path.isfile(SPAWN) else {"spawn": {}}
        same, same_s = old["rows"] == fixes, olds["spawn"] == spawn["spawn"]
        print("ведомость %s: %d строк, по источникам %d — %s; слепок спавна — %s" % (
            FIXES, len(old["rows"]), len(fixes), "совпадает" if same else "УСТАРЕЛА", "совпадает" if same_s else "УСТАРЕЛ"))
        return 0 if same and same_s and not bad else 1
    with open(FIXES, "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, ensure_ascii=False, indent=1)
    with open(SPAWN, "w", encoding="utf-8", newline="\n") as f:
        json.dump(spawn, f, ensure_ascii=False, indent=0)
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "mismatch.csv"), "w", encoding="utf-8-sig", newline="") as f:
        w = csv.DictWriter(f, list(report[0]))
        w.writeheader()
        w.writerows(report)
    print("ведомость: %d строк (из них с другой клеткой/картой/местами — %d), отчёт: %d NPC, необъяснённых %d" % (
        len(fixes), sum(1 for r in report if r["action"].startswith("по серверу")), len(report), bad))
    for r in report:
        print("  NPC %3d %-11s кл.%d  клиент %d (%d,%d)  сервер %-24s %s -> %s" % (
            r["npc"], r["nsr"], r["cls"], r["cli_map"], r["cli_x"], r["cli_y"], r["srv"], r["kind"], r["action"][:40]))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
