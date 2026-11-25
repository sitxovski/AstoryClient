# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""
server_sowel_tail_restore.py — восстанавливает в серверном `ItemTable_Info\\SowelCombine.txt` данные заточки
для экипировки «хвоста» (коды выше стокового 76314), которую 13.08.2026 `server_item_tail_extend.py` дописал
НЕЙТРАЛЬНЫМ образцом: предел попыток col45 = 0 (предмет не точится, окно кузнеца — 50011 и «0/0»),
грейд свитка col47 = −1, цена/очки сброса 0, группы опций col49/col50 = 0 (реальная группа №0).

Баг игроков 29.10.2026 «Military Set armor cannot be upgraded … 0/0» (комплект «Военный костюм» 78928…78987).

ЧТО ДЕЛАЕТ (только хвостовая экипировка, ItemType 0–4/8/9, у которой на сервере col45 == 0):
  класс A — в стоковом сервере есть вещь с ТЕМ ЖЕ русским именем и слотом (тот же набор другого яруса/класса):
            значения берутся у неё (оригинал известен; в т. ч. 0 — если набор в оригинале не точится);
  класс B — то же по имени без «(стильный)»/«Особый»;
  класс C — стокового аналога нет: правило, господствующее у стоковых «ярусных» костюмов (полосы 1–39/40–59/60–79):
            предел 5+грейд (грейд ≥ 5 → 5), грейд-класс = грейд, очки 4; группы опций −1 (нет), сет-поля −1;
  удочки (класс R) — стоковых нет; по умолчанию НЕ трогаются (рыбалка), включаются ключом --rods.
  Колонки уровня (цена сброса col43, грейд свитка col47, полоса col56/57) — у аналога того же уровня, иначе
  по ярусу стоковых костюмов: L<40 → 1800/0/1–39, 40–59 → 72000/2/40–59, ≥60 → 300000/3/60–79
  (яруса L70 в стоке нет — полоса 60–79 его покрывает).
  Колонка 51 (+170, ID титула) и комбинации col23/col24 не меняются.

Сервер сам поднимает предел экземпляра (+26) до col45 при перемещении/вставке вещи (`CharItemMove`,
`sub_4C97C0`, лог «Upgrade_Max», GameServer.c:80078/82391) — уже выданные вещи догоняются без правки БД.
Таблица читается при старте GameServer: после записи нужен рестарт стойки (с объявлением).

ЗАПУСК:
    python server_sowel_tail_restore.py --report                 # таблица предметов, ничего не пишет
    python server_sowel_tail_restore.py --build --classes A,B,C  # собрать файл в RE/dumps/server_sowel_restore/out
    python server_sowel_tail_restore.py --verify                 # инварианты собранного файла
    python server_sowel_tail_restore.py --write                  # бэкап + запись во все GameServer (stack_layout)
    python server_sowel_tail_restore.py --check-live             # перечитать с сервера и сверить md5
"""

import argparse
import collections
import csv
import hashlib
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))   # <ROOT> (ItemDB → Tools → проект → ROOT)
sys.path.insert(0, os.path.join(PORT, "AstoryClient", "Tools"))
sys.path.insert(0, os.path.join(PORT, "AstoryServer", "Patches", "AsdaPatch", "tools"))
import asda_paths     # noqa: E402  единые пути проекта
import stack_layout   # noqa: E402  состав стойки (оба полевых канала + данж)
import srv_io         # noqa: E402  чтение/запись стойки только по SSH

STOCK_SC = os.path.join(asda_paths.GAMESERVER, "ItemTable_Info", "SowelCombine.txt")   # стоковый сервер (оригинал)
CLIENT_CSV = os.path.join(PORT, "AstoryClient", "RawAssets", "Data", "ItemDB", "item_list.csv")
EN_LOC = os.path.join(PORT, "AstoryClient", "RawAssets", "Text", "L10N", "en", "DT_Loc_item_list_en.csv")
WORK = os.path.join(PORT, "RE", "dumps", "server_sowel_restore")
DIR_ORIG = os.path.join(WORK, "live_orig")          # снимок живого файла (GameServer)
DIR_OUT = os.path.join(WORK, "out")
TABLE_CSV = os.path.join(PORT, "Reports", "bugs_2026-10-29", "military_upgrade", "tail_equipment_upgrade.csv")
NONGEAR_CSV = os.path.join(PORT, "Reports", "bugs_2026-10-29", "military_upgrade", "nongear_upgradable_by_server.csv")
REVERSE_CSV = os.path.join(PORT, "Reports", "bugs_2026-10-29", "gear_reverse_123", "restored_18_upgrade.csv")
REL = "ItemTable_Info/SowelCombine.txt"             # путь файла от папки GameServer

FIRST_CODE = 20551          # база индексации таблиц предметов
SC_NCOL = 57                # токенов в строке SowelCombine (запись 172 б, sub_4A0DB0)
EQUIP = {0, 1, 2, 3, 4, 8, 9}
SLOT = {0: "шлем", 1: "доспех", 2: "штаны", 3: "ботинки", 4: "перчатки", 8: "щит", 9: "оружие"}

# 0-базные индексы колонок SowelCombine (1-базная колонка = индекс + 1; смещения записи — GameServer.c:65212-65249)
I_PRICE, I_PTS, I_LIM, I_GC, I_SG = 42, 43, 44, 45, 46      # +140 цена сброса, +144 очки, +146 предел, +148 грейд-класс, +150 грейд свитка
I_OPT0, I_OPT1, I_OPT2 = 47, 48, 49                         # +152, +154 (опция ступени 6), +156 (высшая переработка)
I_TITLE = 50                                                # +170 ID титула — не трогаем
I_SET = (51, 52, 53, 54)                                    # +158/+162/+160/+164
I_BAND = (55, 56)                                           # +166 (пишется дважды)
COPY_IDX = [I_PRICE, I_PTS, I_LIM, I_GC, I_SG, I_OPT0, I_OPT1, I_OPT2, *I_SET, *I_BAND]   # что берём у аналога того же уровня
FAMILY_IDX = [I_PTS, I_LIM, I_GC, I_OPT0, I_OPT1, I_OPT2, *I_SET]                         # что берём у аналога другого уровня


def _atol(tok):
    """atol() бинаря: ведущее целое, мусор → 0 (в файле встречается «(1)»)."""
    m = re.match(rb"\s*[-+]?\d+", tok)
    return int(m.group()) if m else 0


def read_sc(raw):
    """(заголовок, [(сырая строка, [int×57])]) — сырьё нужно, чтобы нетронутые строки остались байт в байт."""
    parts = raw.split(b"\r\n")
    out = [(ln, [_atol(t) for t in ln.split()]) for ln in parts[1:] if ln.strip()]
    return parts[0], out


def fmt(row):
    """Строка в формате хвоста (табы, CRLF)."""
    return "\t".join(str(v) for v in row).encode("ascii") + b"\r\n"


def md5b(data):
    return hashlib.md5(data).hexdigest()


def load_client():
    """code → строка item_list.csv (ItemType, ItemGrade, RequiredLevel, …)."""
    csv.field_size_limit(10 ** 9)
    with open(CLIENT_CSV, newline="", encoding="utf-8") as f:
        return {int(r["ItemCode"]): r for r in csv.DictReader(f)}


def load_en():
    """code → английское имя (ключ ведомости = номер строки, код = строка + 20551)."""
    out = {}
    with open(EN_LOC, encoding="utf-8") as f:
        for line in f:
            k, _, v = line.rstrip("\n").partition(",")
            if k.endswith(".ItemName") and k[:-9].isdigit():
                out[int(k[:-9]) + FIRST_CODE] = v.strip('"')
    return out


def norm_name(n):
    """Имя без «(стильный)» и ведущего «Особый» — ключ класса B."""
    n = re.sub(r"\(стильн\w*\)", "", n.lower())
    n = re.sub(r"^особ\w+ ", "", n)
    return re.sub(r"\s+", " ", n).strip()


def tier(level):
    """Колонки уровня у стоковых «ярусных» костюмов: (цена сброса, грейд свитка, полоса от, полоса до)."""
    if level < 40:
        return 1800, 0, 1, 39
    if level < 60:
        return 72000, 2, 40, 59
    return 300000, 3, 60, 79


def plan(live_rows, stock_rows, client, en, classes, rods):
    """Список решений по хвостовой экипировке с пределом 0: dict(code, класс, новая строка, …)."""
    stock_last = max(stock_rows)
    by_name, by_norm = collections.defaultdict(list), collections.defaultdict(list)
    for code, row in stock_rows.items():
        r = client.get(code)
        if r and int(r["ItemType"]) in EQUIP:
            by_name[(r["ItemName"], r["ItemType"])].append(code)
            by_norm[(norm_name(r["ItemName"]), r["ItemType"])].append(code)
    out = []
    for code in sorted(live_rows):
        r, row = client.get(code), live_rows[code]
        if code <= stock_last or not r or int(r["ItemType"]) not in EQUIP or row[I_LIM] != 0:
            continue
        t, g, lvl = int(r["ItemType"]), int(r["ItemGrade"]), int(r["RequiredLevel"])
        new = list(row)
        sib, cls = by_name.get((r["ItemName"], r["ItemType"])), "A"
        if not sib:
            sib, cls = by_norm.get((norm_name(r["ItemName"]), r["ItemType"])), "B"
        rod = "удочк" in r["ItemName"].lower()
        if sib:
            same = [c for c in sib if client[c]["RequiredLevel"] == r["RequiredLevel"]]
            pick = [c for c in same if client[c]["UsageCategory"] == r["UsageCategory"]] or same
            if pick:
                src = stock_rows[pick[0]]
                for i in COPY_IDX:
                    new[i] = src[i]
                how = "аналог %d того же уровня" % pick[0]
            else:
                fam = collections.Counter(tuple(stock_rows[c][i] for i in FAMILY_IDX) for c in sib).most_common(1)[0][0]
                for i, v in zip(FAMILY_IDX, fam):
                    new[i] = v
                new[I_PRICE], new[I_SG], new[I_BAND[0]], new[I_BAND[1]] = tier(lvl)
                how = "семейство %d стоковых, уровень по ярусу" % len(sib)
            limits = sorted({stock_rows[c][I_LIM] for c in sib})
            how += "; пределы у стоковых: %s" % "/".join(map(str, limits))
        else:
            cls = "R" if rod else "C"
            new[I_PTS], new[I_LIM], new[I_GC] = 4, (5 + g if g <= 4 else 5), g
            new[I_OPT0], new[I_OPT1], new[I_OPT2] = 0, -1, -1
            for i in I_SET:
                new[i] = -1
            new[I_PRICE], new[I_SG], new[I_BAND[0]], new[I_BAND[1]] = tier(lvl)
            how = "аналога нет: правило грейда" + (" (удочка)" if rod else "")
        apply = cls in classes and (cls != "R" or rods)
        group = "удочка" if rod else ("оружие" if t == 9 else "щит" if t == 8 else "костюм/броня")
        out.append(dict(code=code, cls=cls, apply=apply, row=new if apply else row, how=how, group=group,
                        slot=SLOT[t], grade=g, level=lvl, usage=r["UsageCategory"],
                        sockets=0 if r["EnchantOrSocket"] == "4" else int(r["EnchantOrSocket"]) + 1,
                        name_ru=r["ItemName"], name_en=en.get(code, ""), lim_new=new[I_LIM], sg_new=new[I_SG]))
    return out


def plan_nongear(live_rows, server_types, client, en):
    """Режим --nongear (баг 30.10, Caroling Gift Basket): коды, которые КЛИЕНТ показывает не-снаряжением (item_list ItemType
    не 0…4/8/9), а серверная item_table (колонка 17, +26 записи 71 б) считает снаряжением, — сервер пускает их в заточку
    (маска `Recv_User_ShellItemUpgrade(22)` смотрит только свой тип). Предел col45 → 0: тогда P33 N-14 (ZeroLimitGuard)
    отвечает кодом 7 «Предмет нельзя улучшить» до броска. Остальные колонки не трогаются."""
    out = []
    for code in sorted(live_rows):
        r, row, st = client.get(code), live_rows[code], server_types.get(code)
        if not r or st is None or row[I_LIM] <= 0:
            continue
        if int(r["ItemType"]) in EQUIP or st[0] not in EQUIP:
            continue
        new = list(row)
        new[I_LIM] = 0
        out.append(dict(code=code, cls="N", apply=True, row=new, how="клиент: тип %s/%s, сервер: тип %d/%d" % (
                        r["ItemType"], r["ItemSubType"], st[0], st[1]), group="не-снаряжение", slot=SLOT.get(st[0], "?"),
                        grade=int(r["ItemGrade"]), level=int(r["RequiredLevel"]), usage=r["UsageCategory"], sockets=0,
                        name_ru=r["ItemName"], name_en=en.get(code, ""), lim_new=0, sg_new=row[I_SG], lim_old=row[I_LIM]))
    return out


# Режим --reverse (30.10, решение владельца): 18 настоящих вещей снаряжения, которые клиент описывает снаряжением, а
#   стоковая item_table — чужим предметом (тип 11/5, без характеристик): сервер их не надевает и не точит. Серверная
#   ItemName_Table называет эти коды так же, как клиент (Pan's_Bow, Veteran_Reindeer_Hat…) — устарела именно item_table.
#   Разбор: Reports/bugs_2026-10-29/gear_reverse_123/CONCLUSION.md. У игроков (Joy1.Item/Exitem/ItemPostBox) на 30.10 — 0.
REVERSE_CODES = [23317, 23363, 23364, 75667, 75668, 75669, 75797, 76071, 76072] + list(range(76181, 76190))
IT_REL = {"GameServer": "item_table.txt", "Indun_GameServer": "Item_Table.txt"}   # имя файла по площадке (копия Ch2 — как поле)


def item_rel(gs):
    """Путь item_table площадки от v2."""
    return gs + "/ItemTable_Info/" + IT_REL.get(stack_layout.TWINS.get(gs, gs), "item_table.txt")


def tail_module():
    """server_item_tail_extend.py как модуль: build_item_row (строка item_table по 248-байтовой клиентской записи;
    соответствие колонок проверено на 44 492 вещах снаряжения — ≥ 94,8 %, кроме серверных ручных правок колонки 40)."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("server_item_tail_extend", os.path.join(HERE, "server_item_tail_extend.py"))
    t = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(t)
    for c in range(1, 18):
        t.COLMAP[c] = ("i32", 68 + 4 * (c - 1))
    t.COLMAP[18] = ("u16", 136)   # dword @136 сервер делит на два слова
    t.COLMAP[19] = ("u16", 138)
    for c in range(20, 42):
        t.COLMAP[c] = ("i32", 68 + 4 * (c - 2))
    return t


def plan_reverse(live_rows, stock_rows, server_types, client, en):
    """SowelCombine для REVERSE_CODES: аналог — стоковая вещь с тем же именем и слотом, у которой клиент и сервер согласны
    по типу (уровень — у аналога того же уровня, иначе ярус); аналога нет — правило грейда (как класс C)."""
    agree = {c: row for c, row in stock_rows.items()
             if c in client and c not in REVERSE_CODES and server_types.get(c, (-1,))[0] == int(client[c]["ItemType"])}
    out = []
    for code in REVERSE_CODES:
        r = client[code]
        g, lvl = int(r["ItemGrade"]), int(r["RequiredLevel"])
        named = r["ItemName"].strip() not in ("", "-", "_")   # пустые заготовки клиента — не аналоги
        sib = [c for c in agree if named and client[c]["ItemName"] == r["ItemName"] and client[c]["ItemType"] == r["ItemType"]]
        new = list(live_rows[code])
        if sib:
            same = [c for c in sib if client[c]["RequiredLevel"] == r["RequiredLevel"]]
            pick = [c for c in same if client[c]["UsageCategory"] == r["UsageCategory"]] or same
            if pick:
                for i in COPY_IDX:
                    new[i] = stock_rows[pick[0]][i]
                how = "аналог %d того же уровня" % pick[0]
            else:
                fam = collections.Counter(tuple(stock_rows[c][i] for i in FAMILY_IDX) for c in sib).most_common(1)[0][0]
                for i, v in zip(FAMILY_IDX, fam):
                    new[i] = v
                new[I_PRICE], new[I_SG], new[I_BAND[0]], new[I_BAND[1]] = tier(lvl)
                how = "семейство %d стоковых (%s), уровень по ярусу" % (len(sib), "/".join(map(str, sorted({stock_rows[c][I_LIM] for c in sib}))))
            cls = "A"
        else:
            new[I_PTS], new[I_LIM], new[I_GC] = 4, (5 + g if g <= 4 else 5), g
            new[I_OPT0], new[I_OPT1], new[I_OPT2] = 0, -1, -1
            for i in I_SET:
                new[i] = -1
            new[I_PRICE], new[I_SG], new[I_BAND[0]], new[I_BAND[1]] = tier(lvl)
            how, cls = "аналога нет: правило грейда", "C"
        t = int(r["ItemType"])
        out.append(dict(code=code, cls=cls, apply=True, row=new, how=how,
                        group="оружие" if t == 9 else "щит" if t == 8 else "костюм/броня", slot=SLOT[t], grade=g, level=lvl,
                        usage=r["UsageCategory"], sockets=0 if r["EnchantOrSocket"] == "4" else int(r["EnchantOrSocket"]) + 1,
                        name_ru=r["ItemName"], name_en=en.get(code, ""), lim_new=new[I_LIM], sg_new=new[I_SG],
                        lim_old=live_rows[code][I_LIM]))
    return out


def load_client_raw():
    """code → строка item_list АРХИВА клиента (декод `decode_item_db.py --only item_list.bin --no-ledgers` во WORK/client_raw).
    ★ Не боевой item_list.csv: ведомость `_value_overlay` «привела клиента к серверу» (optgroup-sync, цены, уровни) — у этих
    кодов там серверные ошибочные значения (группы опций 0, уровень 1, цена 1); строить по ним серверную строку = по кругу."""
    raw_dir = os.path.join(WORK, "client_raw")
    path = os.path.join(raw_dir, "item_list.csv")
    if not os.path.isfile(path):
        os.makedirs(raw_dir, exist_ok=True)
        rc = subprocess.run([sys.executable, os.path.join(HERE, "decode_item_db.py"), "--out", raw_dir, "--only", "item_list.bin",
                             "--no-ledgers"]).returncode
        if rc != 0 or not os.path.isfile(path):
            sys.exit("ОТКАЗ: декод архива item_list.bin не удался (код %d)" % rc)
    csv.field_size_limit(10 ** 9)
    with open(path, newline="", encoding="utf-8") as f:
        return {int(r["ItemCode"]): r for r in csv.DictReader(f)}


def build_reverse():
    """Собрать out/item_table.txt и out/SowelCombine.txt для REVERSE_CODES из свежих снимков живых файлов."""
    snapshot()
    os.makedirs(DIR_ORIG, exist_ok=True)
    it_raw = srv_io.read_live(item_rel(stack_layout.GS_FOLDERS[0]))
    sums = {gs: srv_io.live_md5(item_rel(gs)) for gs in stack_layout.GS_FOLDERS}
    if len(set(sums.values())) != 1:
        sys.exit("ОТКАЗ: item_table на GameServer различаются: %s" % sums)
    with open(os.path.join(DIR_ORIG, "item_table.txt"), "wb") as f:
        f.write(it_raw)
    client, en = load_client(), load_en()
    raw = load_client_raw()
    for code in REVERSE_CODES:   # числа и запись этих кодов — из архива (см. load_client_raw); имя — портовое (по нему ищется аналог)
        client[code] = dict(raw[code], ItemName=client[code]["ItemName"])
    tail = tail_module()
    it_parts = it_raw.split(b"\r\n")
    it_body = []
    changed_it = 0
    for ln in it_parts[1:]:
        t = ln.split()
        code = _atol(t[0]) if t else None
        if code in REVERSE_CODES:
            row = tail.build_item_row(code, bytes.fromhex(client[code]["DataHex"]))
            it_body.append(tail.fmt(row).rstrip(b"\r\n"))
            changed_it += 1
        else:
            it_body.append(ln)
    it_out = b"\r\n".join([it_parts[0]] + it_body)
    sc_raw = open(os.path.join(DIR_ORIG, "SowelCombine.txt"), "rb").read()
    header, rows = read_sc(sc_raw)
    live = {row[0]: row for _ln, row in rows}
    stypes = {}
    for ln in it_parts[1:]:
        t = ln.split()
        if len(t) > 17:
            stypes[_atol(t[0])] = (_atol(t[16]), _atol(t[17]))
    decisions = plan_reverse(live, stock_table(), stypes, client, en)
    summary(decisions)
    write_table(decisions, REVERSE_CSV)
    change = {d["code"]: d["row"] for d in decisions}
    sc_out = header + b"\r\n" + b"".join(fmt(change[row[0]]) if row[0] in change else ln + b"\r\n" for ln, row in rows)
    os.makedirs(DIR_OUT, exist_ok=True)
    with open(os.path.join(DIR_OUT, "item_table.txt"), "wb") as f:
        f.write(it_out)
    with open(os.path.join(DIR_OUT, "SowelCombine.txt"), "wb") as f:
        f.write(sc_out)
    with open(os.path.join(DIR_OUT, "mode.txt"), "w", encoding="ascii") as f:
        f.write("reverse")
    print("собрано: item_table (%d строк) md5 %s; SowelCombine (%d строк) md5 %s" % (changed_it, md5b(it_out), len(change), md5b(sc_out)))


def verify_reverse():
    """item_table: число строк и порядок прежние, изменены только REVERSE_CODES, тип теперь снаряжение, значения влезают
    в поля записи; SowelCombine — только REVERSE_CODES и только колонки заточки."""
    tail = tail_module()
    ok = True

    def chk(cond, msg):
        nonlocal ok
        print(("OK   " if cond else "FAIL ") + msg)
        ok = ok and cond
    a = open(os.path.join(DIR_ORIG, "item_table.txt"), "rb").read().split(b"\r\n")
    b = open(os.path.join(DIR_OUT, "item_table.txt"), "rb").read().split(b"\r\n")
    chk(len(a) == len(b) and a[0] == b[0], "item_table: заголовок %s, строк %d" % (b[0].decode(), len(b) - 1))
    diff = [(x, y) for x, y in zip(a[1:], b[1:]) if x != y]
    codes = sorted(_atol(y.split()[0]) for _x, y in diff)
    chk(codes == sorted(REVERSE_CODES), "item_table: изменены ровно %d кодов из списка" % len(codes))
    chk([_atol(x.split()[0]) for x in a[1:] if x.split()] == [_atol(y.split()[0]) for y in b[1:] if y.split()], "item_table: коды и порядок прежние")
    bad = 0
    for _x, y in diff:
        v = [_atol(t) for t in y.split()]
        if len(v) != tail.IT_NCOL or v[16] not in EQUIP:
            bad += 1
        for c, w in tail.IT_WIDTH.items():
            lo, hi = tail.LIMITS[w]
            if not lo <= v[c] <= hi:
                bad += 1
    chk(bad == 0, "item_table: 42 колонки, тип — снаряжение, значения влезают в поля")
    ho, ro = read_sc(open(os.path.join(DIR_ORIG, "SowelCombine.txt"), "rb").read())
    hn, rn = read_sc(open(os.path.join(DIR_OUT, "SowelCombine.txt"), "rb").read())
    sdiff = [(p, q) for (lo, p), (ln, q) in zip(ro, rn) if lo != ln]
    chk(ho == hn and len(ro) == len(rn), "SowelCombine: заголовок и число строк прежние")
    chk(all(p[0] in REVERSE_CODES and {i for i in range(SC_NCOL) if p[i] != q[i]} <= set(COPY_IDX) for p, q in sdiff),
        "SowelCombine: изменено %d строк, только коды списка и колонки заточки" % len(sdiff))
    chk(all(0 <= q[I_LIM] <= 9 for _p, q in sdiff), "SowelCombine: предел 0…9")
    print("ИТОГ:", "ВСЁ ОК" if ok else "ЕСТЬ ОШИБКИ")
    return ok


def server_item_types():
    """code → (ItemType, ItemSubType) по живой item_table GameServer (колонки 17/18)."""
    raw = srv_io.read_live(stack_layout.GS_FOLDERS[0] + "/ItemTable_Info/item_table.txt")
    out = {}
    for ln in raw.split(b"\r\n")[1:]:
        t = ln.split()
        if len(t) > 17:
            out[_atol(t[0])] = (_atol(t[16]), _atol(t[17]))
    return out


def stock_table():
    with open(STOCK_SC, "rb") as f:
        return {row[0]: row for _ln, row in read_sc(f.read())[1]}


def write_table(decisions, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="", encoding="utf-8-sig") as f:
        w = csv.writer(f)
        w.writerow(["code", "name_en", "name_ru", "group", "slot", "grade", "level", "class_usage", "gem_sockets",
                    "limit_now", "limit_proposed", "scroll_grade", "class", "applied", "source"])
        for d in decisions:
            w.writerow([d["code"], d["name_en"], d["name_ru"], d["group"], d["slot"], d["grade"], d["level"], d["usage"],
                        d["sockets"], d.get("lim_old", 0), d["lim_new"], d["sg_new"], d["cls"], int(d["apply"]), d["how"]])
    print("таблица:", path, len(decisions), "строк")


def summary(decisions):
    c = collections.Counter((d["cls"], d["group"], d["lim_new"]) for d in decisions)
    for k, v in sorted(c.items()):
        print("  класс %s  %-13s предел %2d : %d" % (k[0], k[1], k[2], v))
    print("  всего:", len(decisions), " к записи:", sum(d["apply"] for d in decisions))


def snapshot():
    """Снять живой файл со всех GameServer и убедиться, что он одинаковый."""
    os.makedirs(DIR_ORIG, exist_ok=True)
    data = {gs: srv_io.read_live(gs + "/" + REL) for gs in stack_layout.GS_FOLDERS}
    sums = {gs: md5b(b) for gs, b in data.items()}
    for gs, s in sums.items():
        print("  снято %s/%s md5 %s" % (gs, REL, s))
    if len(set(sums.values())) != 1:
        sys.exit("ОТКАЗ: SowelCombine.txt на GameServer различаются — сначала выровнять")
    with open(os.path.join(DIR_ORIG, "SowelCombine.txt"), "wb") as f:
        f.write(data[stack_layout.GS_FOLDERS[0]])


def build(classes, rods, report_only, nongear=False):
    path = os.path.join(DIR_ORIG, "SowelCombine.txt")
    if not os.path.isfile(path):
        snapshot()
    raw = open(path, "rb").read()
    header, rows = read_sc(raw)
    live = {row[0]: row for _ln, row in rows}
    if nongear:
        decisions = plan_nongear(live, server_item_types(), load_client(), load_en())
    else:
        decisions = plan(live, stock_table(), load_client(), load_en(), classes, rods)
    summary(decisions)
    write_table(decisions, NONGEAR_CSV if nongear else TABLE_CSV)
    os.makedirs(DIR_OUT, exist_ok=True)
    with open(os.path.join(DIR_OUT, "mode.txt"), "w", encoding="ascii") as f:   # режим сборки — для --verify
        f.write("nongear" if nongear else "tail")
    if report_only:
        return
    change = {d["code"]: d["row"] for d in decisions if d["apply"]}
    body = b"".join(fmt(change[row[0]]) if row[0] in change else ln + b"\r\n" for ln, row in rows)
    out = header + b"\r\n" + body
    os.makedirs(DIR_OUT, exist_ok=True)
    with open(os.path.join(DIR_OUT, "SowelCombine.txt"), "wb") as f:
        f.write(out)
    print("собрано: %s (%d строк изменено) md5 %s" % (os.path.join(DIR_OUT, "SowelCombine.txt"), len(change), md5b(out)))


def verify():
    """Инварианты: число строк и порядок кодов прежние, у всех 57 колонок, нетронутые строки байт в байт,
    у изменённых — только колонки заточки, значения влезают в поля записи."""
    orig = open(os.path.join(DIR_ORIG, "SowelCombine.txt"), "rb").read()
    new = open(os.path.join(DIR_OUT, "SowelCombine.txt"), "rb").read()
    ho, ro = read_sc(orig)
    hn, rn = read_sc(new)
    ok = True

    def chk(cond, msg):
        nonlocal ok
        print(("OK   " if cond else "FAIL ") + msg)
        ok = ok and cond
    chk(ho == hn and int(hn) == len(rn), "заголовок %s = числу строк %d" % (hn.decode(), len(rn)))
    chk([r[0] for _l, r in ro] == [r[0] for _l, r in rn], "коды и порядок строк прежние")
    chk(all(len(r) == SC_NCOL for _l, r in rn), "во всех строках %d колонок" % SC_NCOL)
    mode_path = os.path.join(DIR_OUT, "mode.txt")
    nongear = os.path.isfile(mode_path) and open(mode_path, encoding="ascii").read().strip() == "nongear"
    allowed = {I_LIM} if nongear else set(COPY_IDX)
    changed = bad = 0
    for (lo, a), (ln, b) in zip(ro, rn):
        if lo == ln:
            continue
        changed += 1
        diff = {i for i in range(SC_NCOL) if a[i] != b[i]}
        ok_row = (b[I_LIM] == 0 and a[I_LIM] > 0) if nongear else a[I_LIM] == 0
        if not diff <= allowed or not ok_row:
            bad += 1
    chk(bad == 0, ("изменено %d строк, у всех только предел col45 → 0" if nongear else
                   "изменено %d строк, все — бывший предел 0 и только колонки заточки") % changed)
    words = [I_PTS, I_LIM, I_SG, I_OPT0, I_OPT1, I_OPT2, *I_SET, *I_BAND]
    chk(all(-32768 <= r[i] <= 32767 for _l, r in rn for i in words) and all(-128 <= r[I_GC] <= 255 for _l, r in rn),
        "значения влезают в поля записи (word/byte)")
    chk(all(0 <= r[I_LIM] <= 9 for _l, r in rn), "предел в диапазоне таблицы шансов 0…9 (UpgradeSlotRate, 10 строк)")
    print("ИТОГ:", "ВСЁ ОК" if ok else "ЕСТЬ ОШИБКИ")
    return ok


def out_mode():
    """Режим последней сборки: tail / nongear / reverse."""
    p = os.path.join(DIR_OUT, "mode.txt")
    return open(p, encoding="ascii").read().strip() if os.path.isfile(p) else "tail"


def write_live():
    data = open(os.path.join(DIR_OUT, "SowelCombine.txt"), "rb").read()
    it_data = open(os.path.join(DIR_OUT, "item_table.txt"), "rb").read() if out_mode() == "reverse" else None
    stamp = time.strftime("%Y%m%d_%H%M")   # ★ свой суффикс на каждую выкладку: бэкапы прежних выкладок не перекрываются
    suffix = ".bak_sowelrestore_" + stamp
    srv_bak = "@@ASTORY_SERVER_ROOT_ESC@@\\Working_ASDA\\v2\\_backup\\sowelrestore_" + stamp
    for gs in stack_layout.GS_FOLDERS:
        print("площадка %s:" % gs)
        srv_io.put_live(gs + "/" + REL, data, suffix, srv_bak, os.path.join(WORK, "live_backup_" + stamp))
        if it_data is not None:
            srv_io.put_live(item_rel(gs), it_data, suffix, srv_bak, os.path.join(WORK, "live_backup_" + stamp))


def check_live():
    files = [("SowelCombine.txt", lambda gs: gs + "/" + REL)]
    if out_mode() == "reverse":
        files.append(("item_table.txt", item_rel))
    ok = True
    for name, rel in files:
        want = md5b(open(os.path.join(DIR_OUT, name), "rb").read())
        for gs in stack_layout.GS_FOLDERS:
            got = srv_io.live_md5(rel(gs))
            print(("OK   " if got == want else "FAIL ") + "%s md5 %s" % (rel(gs), got))
            ok = ok and got == want
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--snapshot", action="store_true", help="заново снять живой файл")
    ap.add_argument("--report", action="store_true", help="только таблица предметов")
    ap.add_argument("--build", action="store_true")
    ap.add_argument("--classes", default="", help="какие классы восстановить: A,B,C (R — удочки, только с --rods)")
    ap.add_argument("--rods", action="store_true", help="включить удочки (класс R)")
    ap.add_argument("--nongear", action="store_true", help="режим «не-снаряжение»: предел 0 кодам, которые клиент "
                                                          "показывает не снаряжением, а сервер считает снаряжением")
    ap.add_argument("--reverse", action="store_true", help="режим «18 вещей»: строки item_table по клиенту + SowelCombine")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--check-live", action="store_true")
    a = ap.parse_args()
    classes = {c.strip().upper() for c in a.classes.split(",") if c.strip()} | ({"R"} if a.rods else set())
    if a.snapshot:
        snapshot()
    if a.reverse and a.build:
        build_reverse()
    elif a.report or a.build:
        build(classes, a.rods, report_only=a.report and not a.build, nongear=a.nongear)
    if a.verify and not (verify_reverse() if out_mode() == "reverse" else verify()):
        sys.exit(1)
    if a.write:
        write_live()
    if a.check_live:
        sys.exit(0 if check_live() else 1)


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
