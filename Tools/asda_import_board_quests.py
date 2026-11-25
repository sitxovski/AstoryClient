# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_board_quests.py — ★ ДОСКА ОБЪЯВЛЕНИЙ (окно 75000 `Subjugate_WIN`): таблицы «ограниченных заданий» 20000…20056.
#   28.10.2026. Спецификация — `Docs/Port/systems/BOARD_QUEST_FLOW.md` §4; отчёт — `Reports/board_quest/CLIENT.md`.
#
#  1) `Data\NPC\Raid_quest_data.BIN` (тег «RQD», шапка 0x14, `+4` число записей, запись 388 б; загрузчик `questLoadQuestTable@0xEEA960`)
#     → **DT_RaidQuestData** (`FAsdaRaidQuestRow`, ВСЕ поля записи):
#       +0 questId · +4 тип периода · +8 max · +12 имя (64 б) · +76 зона · +80/+84 lvMin/lvMax · +88 режим · +92+4i / +112+4i
#       5 требуемых предметов/количеств · +132 описание (200 б) · +332+4i / +348+4i 4 награды на выбор · +364 опыт · +368 золото ·
#       +372+4i / +380+4i 2 предмета-награды. Имя/описание в DT не кладутся: их источник — текстовая таблица `Raid_quest_data`
#       (`FAsdaText::Get`, RU + EN-оверлей). Поле `Name` строки = номер задания (так было и у прежнего импорта — UE берёт `Name` ключом).
#  2) `Data\NPC\Raid_npc_data.BIN` (тег «RQN», запись 428 б; `dataLoadRecordArray428@0xEEA6E0`) → **DT_BoardNpc** (`FAsdaBoardNpcRow`):
#       +0 номер записи · +4 npcId · +8+4j зоны 5 вкладок · +28+4k 100 слотов заданий (значение = номер задания, −1 — нет).
#
#  ★ ИСТОЧНИК ЧИСЕЛ — клиентская таблица Interlude (`asda_paths.DATA_ORIG`, решение владельца 28.10.2026; сервер правится под неё,
#    включая 20055/20056). Числа Raid_quest_data в рабочей сборке MINE те же (сверка ниже), а в Raid_npc_data у MINE у доски 10
#    вычеркнуто 20056 — поэтому NPC-доски берутся из Interlude.
#  ★ КОНТРОЛЬ (§2.4): доска адресует задание ИНДЕКСОМ k (`questTable[k]`), поэтому для каждого слота `questTable[k].id == 20000 + k ==
#    npcRec[+28+4k]` — иначе стоп.
#
# Запуск:  python Tools/run_headless_safe.py Tools/asda_import_board_quests.py --marker "ЗАВЕРШЕНО: доска объявлений"
#            --expect-asset /Game/AsdaAssets/Data/Quest/DT_RaidQuestData --expect-asset /Game/AsdaAssets/Data/Quest/DT_BoardNpc
#          без UE (только JSON и сверка): python Tools/asda_import_board_quests.py --json-only
#   Соседнего `-game` быть не должно (сохранение ассета при нём молча не проходит). Бэкап ассетов — `asda_headless_tool.backup_file`.
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_paths as _P  # noqa: E402

PKG = "/Game/AsdaAssets/Data/Quest"
RS_RAID = "/Script/AsdaReborn.AsdaRaidQuestRow"
RS_NPC = "/Script/AsdaReborn.AsdaBoardNpcRow"
OUT_DIR = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Data", "Quest")
CONTENT_DIR = os.path.join(_P.ROOT, "AstoryClient", "Content")
HDR = 0x14
QUEST_REC = 388
NPC_REC = 428
QUEST_BASE = 20000
MARKER = "ЗАВЕРШЕНО: доска объявлений"


def log(msg):
    """Строка журнала шага (в UE — предупреждение, чтобы было видно в логе headless)."""
    try:
        import unreal
        unreal.log_warning("[доска] %s" % msg)
    except Exception:
        print("[доска] %s" % msg)


def I(b, o):
    """i32 little-endian."""
    return struct.unpack_from("<i", b, o)[0]


def check(name, blob, tag, stride):
    """Тег + count + шаг — арифметикой размера файла."""
    got = blob[0x0C:0x0F].decode("ascii", "replace")
    count = I(blob, 4)
    need = HDR + stride * count
    if got != tag or need != len(blob):
        raise ValueError("%s: тег «%s» (ждали «%s»), count %d, шаг %d ⇒ %d б, в файле %d" % (name, got, tag, count, stride, need, len(blob)))
    return count


HIDDEN_LEDGER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "board_quest_hidden.json")
LEVEL_CAP_OLD = 99    # lvMax клиентской таблицы
LEVEL_CAP_NEW = 100   # ★ решение владельца 22.10.2026: максимальный уровень 100, на сервере у заданий lvMax 99 → 100


def load_hidden():
    """Номера заданий, скрытых с досок (ведомость `Tools/data/board_quest_hidden.json`, поле "hidden"; нет файла — пусто)."""
    if not os.path.isfile(HIDDEN_LEDGER):
        return set()
    return set(int(x) for x in json.load(open(HIDDEN_LEDGER, encoding="utf-8")).get("hidden", []))


# ★ 20.11.2026 (админ-центр А4.5 «Задания», AstoryAdminCenter/domains/cs_kinds/questtables.py): ведомость ЧИСЕЛ поверх таблицы
#   эталона — правки панели для пары с серверным LimitationQuest.txt (эталон Interlude не пишется, правило: эталон только читаем).
#   Формат: {"rows": [{"quest": 20000…, "field": "LevelMin" | "LevelMax" | "MaxCount" | "Exp" | "Gold", "was": число после правок выше
#   (lvMax 99 → 100), "now": число, "source": "admin_center", "reason": "…"}]}; строки накладываются по порядку. was ≠ текущему
#   значению — ОСТАНОВ импорта (ValueError): эталон или правки выше поменялись, правку надо разобрать, а не молча потерять или
#   наложить. Нет файла — прежнее поведение.
VALUES_LEDGER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "board_quest_values.json")
VALUE_FIELDS = ("LevelMin", "LevelMax", "MaxCount", "Exp", "Gold")


def load_values(path=None):
    """Строки ведомости чисел (нет файла — пусто)."""
    p = path or VALUES_LEDGER
    if not os.path.isfile(p):
        return []
    return list(json.load(open(p, encoding="utf-8")).get("rows", []))


def apply_values(rows, path=None):
    """Наложить ведомость чисел (см. VALUES_LEDGER) → число наложенных строк; расхождение was — ValueError."""
    by_id = {r["QuestId"]: r for r in rows}
    n = 0
    for e in load_values(path):
        q, f = int(e["quest"]), e["field"]
        if f not in VALUE_FIELDS or q not in by_id:
            raise ValueError("board_quest_values.json: строка %s.%s — нет такого задания или поля" % (q, f))
        if by_id[q][f] != int(e["was"]):
            raise ValueError("board_quest_values.json: %d.%s сейчас %d, а в ведомости was %s — разберите ведомость (импорт остановлен)"
                             % (q, f, by_id[q][f], e["was"]))
        by_id[q][f] = int(e["now"])
        n += 1
    return n


def apply_overrides(rows, values_path=None):
    """Правки поверх клиентской таблицы: lvMax 99 → 100 (как на сервере с 22.10, иначе на 100-м уровне клиент скрывал бы
    задания, которые сервер выдаёт), `bHidden` по ведомости и ★ 20.11 ведомость чисел админ-центра. Возврат — (поднято lvMax, скрыто)."""
    hidden = load_hidden()
    up = 0
    for r in rows:
        if r["LevelMax"] == LEVEL_CAP_OLD:
            r["LevelMax"] = LEVEL_CAP_NEW
            up += 1
        r["bHidden"] = r["QuestId"] in hidden
    nv = apply_values(rows, values_path)
    if nv:
        log("ведомость чисел админ-центра: строк %d" % nv)
    return up, sorted(hidden)


def read_quests(data_dir=None, overrides=True, values_path=None):
    """Записи Raid_quest_data.BIN (все числовые поля) в порядке файла; `RecIndex` = индекс k. `overrides` — правки
    `apply_overrides` (lvMax 100, скрытие, ведомость чисел; `values_path` — другая ведомость, для проверки плана панели);
    сырые числа (для сверки сборок) — `overrides=False`."""
    p = os.path.join(data_dir or _P.DATA_ORIG, "NPC", "Raid_quest_data.BIN")
    b = open(p, "rb").read()
    n = check("Raid_quest_data.BIN", b, "RQD", QUEST_REC)
    rows = []
    for k in range(n):
        o = HDR + QUEST_REC * k
        rows.append(dict(
            QuestId=I(b, o), Name=str(I(b, o)), RecIndex=k,
            PeriodType=I(b, o + 4), MaxCount=I(b, o + 8), Zone=I(b, o + 76),
            LevelMin=I(b, o + 80), LevelMax=I(b, o + 84), Mode=I(b, o + 88),
            ReqItemIds=[I(b, o + 92 + 4 * i) for i in range(5)],
            ReqItemCounts=[I(b, o + 112 + 4 * i) for i in range(5)],
            ChoiceItemIds=[I(b, o + 332 + 4 * i) for i in range(4)],
            ChoiceItemCounts=[I(b, o + 348 + 4 * i) for i in range(4)],
            Exp=I(b, o + 364), Gold=I(b, o + 368),
            RewardItemIds=[I(b, o + 372 + 4 * i) for i in range(2)],
            RewardItemCounts=[I(b, o + 380 + 4 * i) for i in range(2)]))
    if overrides:
        up, hidden = apply_overrides(rows, values_path)
        log("правки: lvMax %d → %d у %d заданий; скрыто с досок %s" % (LEVEL_CAP_OLD, LEVEL_CAP_NEW, up, hidden or "нет"))
    return rows


def read_npcs(data_dir=None):
    """Записи Raid_npc_data.BIN."""
    p = os.path.join(data_dir or _P.DATA_ORIG, "NPC", "Raid_npc_data.BIN")
    b = open(p, "rb").read()
    n = check("Raid_npc_data.BIN", b, "RQN", NPC_REC)
    rows = []
    for r in range(n):
        o = HDR + NPC_REC * r
        rows.append(dict(RecIndex=I(b, o), NpcId=I(b, o + 4),
                         Zones=[I(b, o + 8 + 4 * j) for j in range(5)],
                         QuestSlots=[I(b, o + 28 + 4 * k) for k in range(100)]))
    return rows


def verify(quests, npcs):
    """Сверка инвариантов доски. Возврат — число расхождений (0 — всё сошлось)."""
    bad = 0
    for k, q in enumerate(quests):
        if q["QuestId"] != QUEST_BASE + k:
            log("!! запись %d: номер %d ≠ %d" % (k, q["QuestId"], QUEST_BASE + k)); bad += 1
    for r in npcs:
        for k, v in enumerate(r["QuestSlots"]):
            if v < 0:
                continue
            if k >= len(quests) or quests[k]["QuestId"] != v or v != QUEST_BASE + k:
                log("!! NPC %d слот %d = %d: не совпадает с questTable[%d]" % (r["NpcId"], k, v, k)); bad += 1
    boards = [r for r in npcs if r["NpcId"] >= 0]
    for r in boards:
        zones = [z for z in r["Zones"] if z >= 0]
        slots = [v for v in r["QuestSlots"] if v >= 0]
        log("доска npcId %d: вкладок %d (зоны %s), заданий %d" % (r["NpcId"], len(zones), zones, len(slots)))
    if [r["NpcId"] for r in boards] != [9, 10]:
        log("!! NPC-досок ждали ровно две (9, 10): %s" % [r["NpcId"] for r in boards]); bad += 1
    # Статистика данных (для отчёта): типы, режимы, награды на выбор, зона каждого задания среди вкладок доски.
    types = {}
    modes = {}
    for q in quests:
        types[q["PeriodType"]] = types.get(q["PeriodType"], 0) + 1
        modes[q["Mode"]] = modes.get(q["Mode"], 0) + 1
    log("заданий %d; типы %s; режимы %s; с наградой на выбор %d; с предметом-наградой %d; с >3 требуемыми %d"
        % (len(quests), types, modes, sum(1 for q in quests if any(c >= 0 for c in q["ChoiceItemIds"])),
           sum(1 for q in quests if any(c >= 0 for c in q["RewardItemIds"])),
           sum(1 for q in quests if sum(1 for c in q["ReqItemIds"] if c >= 0) > 3)))
    for r in boards:
        zones = set(z for z in r["Zones"] if z >= 0)
        lost = [v for v in r["QuestSlots"] if v >= 0 and quests[v - QUEST_BASE]["Zone"] not in zones]
        if lost:
            log("примечание: у доски %d задания вне её вкладок (эталон их не показывает): %s" % (r["NpcId"], lost))
    unbound = [q["QuestId"] for q in quests if not any(q["QuestId"] in r["QuestSlots"] for r in boards)]
    if unbound:
        log("примечание: задания ни на одной доске: %s" % unbound)
    return bad


def compare_mine(quests):
    """Сверка чисел Interlude с рабочей сборкой MINE (имена у MINE переведены — их не сравниваем)."""
    try:
        mine = read_quests(_P.DATA, overrides=False)
    except Exception as e:  # noqa: BLE001
        log("сверка с MINE пропущена: %s" % e)
        return
    keys = [k for k in quests[0].keys() if k not in ("Name",)]
    diff = [q["QuestId"] for q, m in zip(quests, mine) if any(q[k] != m[k] for k in keys)]
    log("сверка чисел Interlude <-> MINE: записей %d/%d, расхождений %d %s" % (len(quests), len(mine), len(diff), diff[:10]))


def json_rows(rows, key):
    """JSON для `fill_data_table_from_json_string` (ключ строки — поле "Name")."""
    out = []
    for r in rows:
        d = dict(r)
        d["Name"] = key(r)
        out.append(d)
    return json.dumps(out, ensure_ascii=False, indent=0)


def main(json_only=False):
    quests, npcs = read_quests(), read_npcs()
    bad = verify(quests, npcs)
    compare_mine(read_quests(overrides=False))
    os.makedirs(OUT_DIR, exist_ok=True)
    tq = json_rows(quests, lambda r: str(r["QuestId"]))
    tn = json_rows(npcs, lambda r: str(r["RecIndex"]))
    open(os.path.join(OUT_DIR, "RaidQuestData.json"), "w", encoding="utf-8", newline="").write(tq)
    open(os.path.join(OUT_DIR, "BoardNpc.json"), "w", encoding="utf-8", newline="").write(tn)
    log("JSON: RaidQuestData.json (%d), BoardNpc.json (%d); расхождений %d" % (len(quests), len(npcs), bad))
    if bad:
        log("!! сверка не сошлась — DT НЕ пишутся")
        return
    if json_only:
        return
    try:
        import unreal
    except Exception:
        log("UE недоступен — записаны только JSON")
        return
    import asda_headless_tool as HT
    from asda_dt_common import fill_dt
    ok = True
    for name, rs, text in (("DT_RaidQuestData", RS_RAID, tq), ("DT_BoardNpc", RS_NPC, tn)):
        asset = os.path.join(CONTENT_DIR, "AsdaAssets", "Data", "Quest", name + ".uasset")
        if os.path.exists(asset):
            HT.backup_file(asset, set_name="board_client", log=log)
        dt = fill_dt(unreal, PKG, name, rs, text, log, fmt="json", missing_hint="Public/Quest/AsdaQuestTypes.h")
        if dt is None:
            ok = False
            continue
        unreal.log("ASDA_WRITES_ASSET: %s/%s" % (PKG, name))
        log("%s: строк %d" % (name, len(dt.get_row_names())))
    log("%s (%s)" % (MARKER, "успех" if ok else "ОШИБКА"))


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main(json_only=("--json-only" in sys.argv))
