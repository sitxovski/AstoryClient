# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
asda_wod_records.py — записи предметов донора WorldOfDreams в клиент и на сервер порта (волны 4–5, 29.10.2026).
Решения владельца и разбор — `Reports/donor_wod/IMPORT.md` §3, §3а (баланс), §3б (исключения), §3в (охват «рабочее»).

ОХВАТ («рабочее», неподвижная точка). Кандидаты — реестр `data/wod/wod_item_codes.csv` (8 384 «нет у нас») минус:
  заготовки донора; ItemType 11–16, 18, 19 (аватар-вещи, аксессуары, плащи); профессия @92 = 44; ссылка любой из 6 групп
  опций @104…@124 на группу NIO с типом 115/128. «Прочее» (донор 20/x) — только если его система описана в
  `data/wod/wod_scope.json` (крафт / питомцы / рыбалка / …). Коробка (20/200|201) и бустер (20/66) входят, только если КАЖДАЯ
  позиция содержимого — предмет набора или доказанное соответствие нашему коду (`wod_scope.json` → map_item / map_func /
  map_sowel); выпал предмет — выпадают содержащие его коробки (повтор до неподвижной точки).
  Итог — `data/wod/wod_codes_final.csv`: наши коды ПОДРЯД с первого свободного (сервер: item_table/SowelCombine без дыр).

БАЛАНС (§3а). Экипировка: уровень @80 — кусочно-линейно по опорам профессий (1→1, 10→5, 30→24, 50→40, 70→60, 100→80) и не
  ниже порога ступени @92; группы опций, вес, пол, служебные @184/@188/@192/@220 и серверная строка SowelCombine (базовый
  соул) — от нашего аналога (тот же ItemType/SubType, та же ступень профессии, ближайший уровень, не костюм 10/100); опции
  донора не переносятся; цена покупки @164 — донорская, продажа @160 = покупка / 5. Коробки и бустеры — проверенный образец
  нашей коробки/бустера + уровень/профессия/грейд/привязка донора; продажа 1, покупка 10 000; пакет/бустер — новые строки.

КОМАНДЫ (без UE, кроме dt-apply):
  python asda_wod_records.py fetch-server            — снимок 3 GameServer (stack_layout) по SSH (srv_io, только чтение)
  python asda_wod_records.py scope                   — охват → data/wod/wod_codes_final.csv (+ сводка причин)
  python asda_wod_records.py build                   — ведомости клиента (хвосты ItemDB, EN) + srv_out + стенд
  python asda_wod_records.py verify                  — стенд по srv_out
Модуль без побочных эффектов при импорте.
"""
import argparse
import collections
import csv
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import asda_paths as _P                                                            # noqa: E402
import asda_wod_items as WI                                                        # noqa: E402

PORT = _P.PORT_DIR                                                                 # @@ASTORY_DEV_ROOT@@\port
UPROJ = os.path.join(PORT, "AstoryClient")                                           # проект UE
ITEMDB_LIVE = WI.ITEMDB_LIVE                                                       # боевые CSV ItemDB
TAIL_DIR = os.path.join(UPROJ, "RawAssets", "ItemDB", "_tail_append")             # ведомости хвоста decode_item_db
LOC_EN = os.path.join(HERE, "data", "loc", "en")                                  # ведомости EN
EN_LIVE = os.path.join(UPROJ, "RawAssets", "Text", "L10N", "en")                  # боевые EN-оверлеи (CSV DT)
DATA_DIR = WI.DATA_DIR                                                             # data/wod
FINAL_CSV = os.path.join(DATA_DIR, "wod_codes_final.csv")                          # итоговый реестр (охват §3в)
SCOPE_JSON = os.path.join(DATA_DIR, "wod_scope.json")                              # системы «прочего» и доказанные соответствия
MODELS_JSON = WI.MODELS_JSON                                                       # план моделей (действие wod → префикс)
DECODER = os.path.join(PORT, "AstoryClient", "Tools", "ItemDB", "decode_item_db.py")              # декодер ItemDB
SRV_TOOLS = os.path.join(PORT, "AstoryServer", "Patches", "AsdaPatch", "tools")         # серверные инструменты
SRV_DIR = os.path.join(SRV_TOOLS, "data_wod_items")                               # снимок/выход/манифест
SRV_IN = os.path.join(SRV_DIR, "srv_in")                                          # снимок стойки до правки
SRV_OUT = os.path.join(SRV_DIR, "srv_out")                                        # файлы для выкладки
MANIFEST = os.path.join(SRV_DIR, "manifest.json")                                 # путь → md5 базы / нового
SRV_TABLES = ("item_table.txt", "SowelCombine.txt", "PackageItem_Table.txt", "GachaItemRateTable.txt")
SRC_PREFIX = "wod:"                                                                # метка строк генератора в ведомостях

ITEM_FIRST = WI.ITEM_FIRST            # 20551 — база плотных таблиц
NIO_LOOSE = os.path.join(WI.WOD_ROOT, "Data", "NewItemOption_Table.BIN")          # группы опций донора (loose, XOR от начала файла)
NIO_STRIDE = 328                      # 82 i32: id, вид, 20 типов, 20 min, 20 max, 20 накопл. весов (options.md §2)
DEAD_OPTION_TYPES = (115, 128)        # «Magic Defense» / «Potentiality» — у нас не работают (balance_stats.md §4)
AVATAR_TYPES = (11, 12, 13, 14, 15, 16, 18, 19)   # аватар-части, аксессуар лица, плащ, аватар-оружие (§3б)
JOB_UNREACHABLE = 44                  # 5-я профессия — недостижима (balance_levels.md §6)
BOX_SUBS = (200, 201)                 # коробка: @216 = строка packagetable (с 0)
BOOSTER_SUB = 66                      # бустер: @216 = booster_list.Code (с 1)
MISC_TYPE_WOD, MISC_TYPE_OUR = 20, 11  # «прочее»: у донора 20, у нас 11
TPL_BOX = 76467                       # образец коробки (asda_premium_box_add / asda_fable_mounts_boxes: проверенный путь)
BOX_BUY, BOX_SELL = 10000, 1          # §3а п.3: коробки и бустеры вне витрины
LEVEL_ANCHORS = ((1, 1), (10, 5), (30, 24), (50, 40), (70, 60), (100, 80))   # донор D → наш L (balance_levels.md §5)
JOB_TIER_FLOOR = {1: 5, 12: 5, 23: 5, 40: 5, 2: 24, 13: 24, 24: 24, 41: 24,
                  3: 40, 14: 40, 25: 40, 42: 40, 4: 60, 15: 60, 26: 60, 43: 60}  # порог ступени по @92
JOB_ANALOG = {4: 3, 15: 14, 26: 25, 43: 42}   # 4-я ступень: у нас вещей нет — аналог ищется у 3-й (balance_levels.md §6)
ACCESSORY_TYPES = (5, 6, 7)            # кольца/ожерелья: профессию 41/42 сервер для них не знает → 0; основной стат @96/@100 —
                                      #   от аналога (у донора 115 «Magic Defense», у нас мёртв; sys_misc.md §4)
ACT_PREFIX = "[Act.]"                  # усиленные близнецы обычных вещей донора (sys_misc.md §4.1)
NOT_FOR_SALE = 10000000               # покупка донора ≥ 10 млн — его метка «не продаётся» (100 000 000) → продажа NPC 1
COSTUME_SPREAD = (10, 100)            # @136/@138 наших костюмов — для обычной экипировки аналогом не берутся


def out(*a):
    """Печать в UTF-8."""
    WI.out(*a)


def i32(b, o):
    """i32 записи."""
    return struct.unpack_from("<i", b, o)[0]


def u16(b, o):
    """u16 записи."""
    return struct.unpack_from("<H", b, o)[0]


# ── донор ──────────────────────────────────────────────────────────────────────────────────────────
def wod_table(name, stride):
    """Записи таблицы донора из Bin.bbz с явным шагом (packagetable 56, booster_list 284, special_itemlist 300,
    sowell_list 176 — deps.md §1)."""
    WI.STRIDE.setdefault(name, stride)
    return WI.wod_records(name)


def nio_dead_groups():
    """id групп NIO донора, в строках которых встречается тип 115/128 (loose-файл, XOR ключом от начала файла)."""
    import numpy as np
    raw = np.frombuffer(open(NIO_LOOSE, "rb").read(), np.uint8)
    key = np.load(WI.KEY_PATH).astype(np.uint8)
    d = (raw ^ np.resize(key, len(raw))).tobytes()
    n = i32(d, 9)
    if WI.WOD_HDR + n * NIO_STRIDE != len(d):
        raise SystemExit("NewItemOption_Table.BIN: размер %d ≠ 25 + %d × %d" % (len(d), n, NIO_STRIDE))
    bad = set()
    for i in range(n):
        r = struct.unpack_from("<82i", d, WI.WOD_HDR + i * NIO_STRIDE)
        if any(t in DEAD_OPTION_TYPES for t in r[2:22]):
            bad.add(r[0])
    return bad


def option_refs(rec):
    """6 ссылок записи донора на группы NIO (@104 u16, @108 i32, @112 i32, @116 u16, @120 i32, @124 i32); «нет» отброшено."""
    refs = (u16(rec, 104), i32(rec, 108), i32(rec, 112), u16(rec, 116), i32(rec, 120), i32(rec, 124))
    return [x for x in refs if x not in (0, 0xFFFF, -1)]


class Donor:
    """Таблицы донора, нужные записям."""

    def __init__(self):
        self.items = WI.wod_items()                                  # {код: (item_list 248, img 300)}
        self.pkg = wod_table("packagetable.bin", 56)                 # строка = Index (с 0)
        self.bst = wod_table("booster_list.bin", 284)                # строка = Code − 1
        self.func = {i32(r, 0): r for r in wod_table("special_itemlist.bin", 300)}   # F 1…710
        self.sowel = {i32(r, 0): r for r in wod_table("sowell_list.bin", 176)}       # S 7001+
        self.dead = nio_dead_groups()
        self.premium = self._premium()

    def _premium(self):
        """Премиум-вещи донора: премиум-витрина (premiumshop.bin +0), премиум-гача (premiumgachadata +4) и содержимое таких
        коробок/бустеров (2 уровня)."""
        prem = {i32(r, 0) for r in wod_table("premiumshop.bin", 104)} | {i32(r, 4) for r in wod_table("premiumgachadata.bin", 12)}
        front = set(prem)
        for _ in range(2):
            front = {x for c in front if c in self.items for x, _ in (self.content(c) if isinstance(self.content(c), list) else [])
                     } - prem
            prem |= front
        return prem

    def rec(self, code):
        """Запись item_list 248 б."""
        return self.items[code][0]

    def img(self, code):
        """Запись item_img_table 300 б."""
        return self.items[code][1]

    def name(self, code):
        """EN-имя донора."""
        return WI.cstr(self.rec(code)[4:68])

    def content(self, code):
        """Содержимое коробки/бустера: [(код, кол-во|вес)] или None (нет строки пакета/бустера); 'n/a' — не контейнер."""
        r = self.rec(code)
        s, idx = i32(r, 132), i32(r, 216)
        if i32(r, 128) != MISC_TYPE_WOD or s not in BOX_SUBS + (BOOSTER_SUB,):
            return "n/a"
        if s in BOX_SUBS:
            if not 0 <= idx < len(self.pkg):
                return None
            p = self.pkg[idx]
            return [(i32(p, 4 + 8 * k), i32(p, 8 + 8 * k)) for k in range(5) if i32(p, 4 + 8 * k) > 0]
        if not 1 <= idx <= len(self.bst):
            return None
        p = self.bst[idx - 1]
        return [(i32(p, 4 + 8 * k), i32(p, 8 + 8 * k)) for k in range(35) if i32(p, 4 + 8 * k) > 0]

    def exclusion(self, code, placeholder=False):
        """Причина исключения кандидата правилами §3б (или None)."""
        r = self.rec(code)
        if placeholder:
            return "заготовка"
        if i32(r, 128) in AVATAR_TYPES:
            return "аватар/аксессуар/плащ"
        if i32(r, 92) == JOB_UNREACHABLE:
            return "профессия 44"
        if any(g in self.dead for g in option_refs(r)):
            return "опции 115/128"
        return None


# ── охват ──────────────────────────────────────────────────────────────────────────────────────────
def load_scope():
    """wod_scope.json: {"misc": {wod_code: {...}}, "map_item": {wod: наш}, "map_func": {F: наш F}, "map_sowel": {S: наш S},
    "exclude": {wod_code: причина}} — ключи строками (JSON)."""
    if not os.path.isfile(SCOPE_JSON):
        return {"misc": {}, "map_item": {}, "map_func": {}, "map_sowel": {}, "exclude": {}}
    d = json.load(open(SCOPE_JSON, encoding="utf-8"))
    for k in ("misc", "map_item", "map_func", "map_sowel", "exclude"):
        d.setdefault(k, {})
    return d


def classify(D, code):
    """Категория предмета донора: gear (ItemType 0–10), box, booster, misc."""
    r = D.rec(code)
    t, s = i32(r, 128), i32(r, 132)
    if t != MISC_TYPE_WOD:
        return "gear"
    if s in BOX_SUBS:
        return "box"
    if s == BOOSTER_SUB:
        return "booster"
    return "misc"


def compute_scope(D, reg, scope):
    """Неподвижная точка охвата → ({wod: категория}, {wod: причина исключения})."""
    cand, why = {}, {}
    for r in reg:
        c = int(r["wod_code"])
        e = D.exclusion(c, r["placeholder"] in ("1", "True", "true")) or scope["exclude"].get(str(c))
        if e:
            why[c] = e
            continue
        cat = classify(D, c)
        if cat == "misc" and str(c) not in scope["misc"]:
            why[c] = "прочее без системы"
            continue
        cand[c] = cat
    mi, mf, ms = scope["map_item"], scope["map_func"], scope["map_sowel"]
    while True:
        drop = {}
        for c, cat in cand.items():
            if cat not in ("box", "booster"):
                continue
            ct = D.content(c)
            if not ct:
                drop[c] = "пустой контейнер" if ct == [] else "нет строки пакета/бустера"
                continue
            for x, _ in ct:
                if 1 <= x <= 710:
                    ok = str(x) in mf
                elif 7001 <= x < ITEM_FIRST:
                    ok = str(x) in ms
                else:
                    ok = x in cand or str(x) in mi
                if not ok:
                    drop[c] = "содержимое не собирается (%d)" % x
                    break
        if not drop:
            return cand, why
        for c, w in drop.items():
            del cand[c]
            why[c] = w


def cmd_scope(a):
    """Охват → FINAL_CSV (коды подряд с первого свободного кода клиента и сервера)."""
    D = Donor()
    reg = WI.load_registry()
    scope = load_scope()
    cand, why = compute_scope(D, reg, scope)
    old_first = min((int(r["our_code"]) for r in WI.load_csv(FINAL_CSV)), default=0) if os.path.isfile(FINAL_CSV) else 0
    first = a.first or old_first or next_free_code()   # коды реестра не сдвигаются соседними сегментами
    names = {int(r["wod_code"]): r["name"] for r in reg}
    old = WI.load_csv(FINAL_CSV) if os.path.isfile(FINAL_CSV) else []
    # свои строки пакетов/бустеров в живых таблицах (после заливки) — по @216 своих кодов в живом item_list
    own_codes = {int(o["our_code"]) for o in old}
    live_own = [r for r in WI.load_csv(os.path.join(ITEMDB_LIVE, "item_list.csv")) if int(r["ItemCode"]) in own_codes]
    own_pk = {int(r["DurationOrEnchant"]) for r in live_own if r["ItemType"] == "11" and int(r["ItemSubType"]) in BOX_SUBS}
    own_bs = {int(r["DurationOrEnchant"]) for r in live_own if r["ItemType"] == "11" and int(r["ItemSubType"]) == BOOSTER_SUB}
    # Номер пакета/бустера выделяется ЗДЕСЬ один раз (как коды) и дальше берётся из реестра: после заливки живые таблицы
    #   уже содержат наши строки, пересчёт «от длины таблицы» сдвинул бы номера.
    pkg = len([r for r in WI.load_csv(os.path.join(ITEMDB_LIVE, "packagetable.csv")) if int(r["Index"]) not in own_pk])
    bst = max(int(r["Code"]) for r in WI.load_csv(os.path.join(ITEMDB_LIVE, "booster_list.csv")) if int(r["Code"]) not in own_bs) + 1
    rows = []
    for k, c in enumerate(sorted(cand)):
        idx = ""
        if cand[c] == "box":
            idx, pkg = pkg, pkg + 1
        elif cand[c] == "booster":
            idx, bst = bst, bst + 1
        rows.append({"wod_code": c, "our_code": first + k, "cat": cand[c], "idx": idx, "name_en": names[c]})
    same = [(o["wod_code"], o["our_code"], o.get("idx", "")) for o in old] ==         [(str(r["wod_code"]), str(r["our_code"]), str(r["idx"])) for r in rows]
    if old and a.check:
        out("охват: %s" % ("совпадает" if same else "РАСХОДИТСЯ"))
        return 0 if same else 2
    if old and not same and not a.force:
        raise SystemExit("ОТКАЗ: охват изменился, а коды/пакеты реестра могли уже уйти на сервер — --force только до выкладки")
    with open(FINAL_CSV, "w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["wod_code", "our_code", "cat", "idx", "name_en"])
        w.writeheader()
        w.writerows(rows)
    out("охват: %d предметов, коды %d…%d → %s" % (len(rows), first, first + len(rows) - 1, FINAL_CSV))
    for k, n in collections.Counter(cand.values()).most_common():
        out("  %-8s %d" % (k, n))
    out("исключено %d:" % len(why))
    for k, n in collections.Counter(re.sub(r" \(\d+\)$", "", w) for w in why.values()).most_common():
        out("  %-32s %d" % (k, n))
    return 0


def next_free_code():
    """Первый свободный код: после последнего кода клиента item_list и сервера item_table (снимок srv_in)."""
    own = {int(r["our_code"]) for r in WI.load_csv(FINAL_CSV)} if os.path.isfile(FINAL_CSV) else set()   # свои (после заливки)
    cli = max(int(r["ItemCode"]) for r in WI.load_csv(os.path.join(ITEMDB_LIVE, "item_list.csv")) if int(r["ItemCode"]) not in own)
    p = os.path.join(SRV_IN, "GameServer", "item_table.txt")
    if not os.path.isfile(p):
        raise SystemExit("нет снимка сервера — сначала fetch-server")
    srv = max(int(toks(ln)[0]) for ln in read_srv(p)[1] if int(toks(ln)[0]) not in own)
    if cli != srv:
        raise SystemExit("ОТКАЗ: последний код клиента %d ≠ сервера %d — хвосты разошлись" % (cli, srv))
    return cli + 1


# ── баланс ─────────────────────────────────────────────────────────────────────────────────────────
def map_level(d, job):
    """Уровень донора → наш (опоры профессий + порог ступени по @92). 0/отрицательный — как есть."""
    if d <= 1:
        lv = max(d, 1) if d > 0 else d
    else:
        lv = None
        for (d0, l0), (d1, l1) in zip(LEVEL_ANCHORS, LEVEL_ANCHORS[1:]):
            if d <= d1:
                lv = int(round(l0 + (l1 - l0) * (d - d0) / float(d1 - d0)))
                break
        if lv is None:
            lv = LEVEL_ANCHORS[-1][1]
    return max(lv, JOB_TIER_FLOOR.get(job, 0)) if lv > 0 else lv


def tier_of(job):
    """Ступень профессии кода @92 (0 — без ограничения)."""
    for t, codes in ((1, (1, 12, 23, 40)), (2, (2, 13, 24, 41)), (3, (3, 14, 25, 42)), (4, (4, 15, 26, 43))):
        if job in codes:
            return t
    return 0


class Ours:
    """Наш каталог для аналогов и шаблонов."""

    def __init__(self):
        rows = WI.load_csv(os.path.join(ITEMDB_LIVE, "item_list.csv"))
        own = {int(r["our_code"]) for r in WI.load_csv(FINAL_CSV)} if os.path.isfile(FINAL_CSV) else set()
        self.items = {int(r["ItemCode"]): r for r in rows if int(r["ItemCode"]) not in own}   # без своих (после заливки)
        self.rec = {c: bytes.fromhex(r["DataHex"]) for c, r in self.items.items()}
        self.by_kind = collections.defaultdict(list)
        for c, b in self.rec.items():
            self.by_kind[(i32(b, 128), i32(b, 132))].append(c)
        self.premium = self._premium()

    def content(self, c):
        """Содержимое нашей коробки/бустера (коды), иначе []."""
        b = self.rec.get(c)
        if b is None or i32(b, 128) != MISC_TYPE_OUR:
            return []
        s, idx = i32(b, 132), i32(b, 216)
        if s in BOX_SUBS:
            return self.pkg.get(idx, [])
        return self.bst.get(idx, []) if s == BOOSTER_SUB else []

    def _premium(self):
        """Наши премиум-вещи (решение владельца 29.10: «обычные = обычные, премиум = премиум»): продаются за GXP
        (серверный SellItemTable.txt, витрина 6000 PremiumShopBin.json), выпадают из премиум-гачи (premium_gatya), а также
        содержимое таких коробок/бустеров (3 уровня вложенности)."""
        self.pkg = {int(r["Index"]): [int(r["Item%d" % j]) for j in range(5)]
                    for r in WI.load_csv(os.path.join(ITEMDB_LIVE, "packagetable.csv"))}
        self.bst = {int(r["Code"]): [int(r["Slot%02dItemCode" % j]) for j in range(35)]
                    for r in WI.load_csv(os.path.join(ITEMDB_LIVE, "booster_list.csv"))}
        sell = os.path.join(SRV_IN, "GameServer", "SellItemTable.txt")
        if not os.path.isfile(sell):
            raise SystemExit("нет снимка SellItemTable.txt — fetch-server")
        root = {int(x) for x in open(sell, "rb").read().split()[2:]}
        root |= {int(r["ItemId"]) for r in json.load(open(os.path.join(UPROJ, "RawAssets", "Data", "NPC", "PremiumShopBin.json"),
                                                          encoding="utf-8-sig"))}
        root |= {int(r["ItemCode"]) for r in WI.load_csv(os.path.join(ITEMDB_LIVE, "premium_gatya.csv"))}
        prem, front = set(root), set(root)
        for _ in range(3):
            front = {x for c in front for x in self.content(c) if x > 0} - prem
            prem |= front
        return prem

    def analog(self, drec, lvl, premium=False):
        """Ближайший наш аналог экипировки: тот же ItemType/SubType и вариант @144 (удочка ≠ лук), та же профессия
        (4-я ступень → 3-я), не костюм, премиальность как у донорской вещи; ближайший уровень, затем грейд.
        «(Старый)…» (устаревшие версии, часто без опций) — только если других нет; при равенстве — с группами опций.
        None — нет кандидатов."""
        t, s, job, grade = i32(drec, 128), i32(drec, 132), i32(drec, 92), i32(drec, 236)
        want_job = 0 if t in ACCESSORY_TYPES else JOB_ANALOG.get(job, job)   # украшения у нас без профессии
        pool = [c for c in self.by_kind.get((t, s), []) if (u16(self.rec[c], 136), u16(self.rec[c], 138)) != COSTUME_SPREAD
                and i32(self.rec[c], 144) == i32(drec, 144)
                and self.items[c]["ItemName"].strip() not in ("", "-", "нет")]   # «-»/«нет» — пустые записи-заглушки оригинала
        pool = [c for c in pool if (c in self.premium) == premium] or pool
        fresh = [c for c in pool if not self.items[c]["ItemName"].startswith("(Стар")]
        pool = fresh or pool
        same_job = [c for c in pool if i32(self.rec[c], 92) == want_job]
        if not same_job:
            same_job = [c for c in pool if tier_of(i32(self.rec[c], 92)) == tier_of(want_job)] or pool
        if not same_job:
            return None
        return min(same_job, key=lambda c: (abs(i32(self.rec[c], 80) - lvl) + 3 * abs(i32(self.rec[c], 236) - grade),
                                            0 if (u16(self.rec[c], 104) or u16(self.rec[c], 116)) else 1, c))


# ── снимок стойки ──────────────────────────────────────────────────────────────────────────────────
def stack_layout():
    """Единый состав стойки (3 GameServer) — AstoryServer/Patches/AsdaPatch/tools/stack_layout.py."""
    sys.path.insert(0, SRV_TOOLS)
    import stack_layout as SL   # noqa: E402
    return SL


def gs_dirs():
    """Папки GameServer стойки (stack_layout.GS_FOLDERS)."""
    return list(stack_layout().GS_FOLDERS)


def cmd_fetch_server(a):
    """Снимок таблиц 3 GameServer по SSH (srv_io.read_live: только чтение, сверка md5) → srv_in."""
    sys.path.insert(0, SRV_TOOLS)
    import srv_io   # noqa: E402
    for d in gs_dirs():
        for t in SRV_TABLES + (("SellItemTable.txt",) if d == "GameServer" else ()):   # SellItemTable — источник премиума (Ours)
            data = srv_io.read_live("%s/ItemTable_Info/%s" % (d, t))
            p = os.path.join(SRV_IN, d, t)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, "wb").write(data)
            out("снимок %-18s %-24s %d б" % (d, t, len(data)))
    return 0


def read_srv(path):
    """(счёт 1-й строки, строки тела без пустых) — CRLF-таблицы сервера."""
    lines = open(path, "rb").read().split(b"\r\n")
    return int(lines[0].split()[0]), [ln for ln in lines[1:] if ln.strip()]


def write_srv(path, count, body):
    """Счёт + строки, CRLF (как у стока)."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(str(count).encode("ascii") + b"\r\n")
        for ln in body:
            f.write(ln + b"\r\n")


def toks(line):
    """Токены строки серверной таблицы."""
    return line.decode("latin1").split()


# ── записи ─────────────────────────────────────────────────────────────────────────────────────────
def put_str(b, off, n, s, enc="cp1251"):
    """C-строка в поле n байт (с нулём)."""
    raw = s.encode(enc)
    if len(raw) >= n:
        raise SystemExit("строка «%s» длиннее поля %d" % (s, n))
    b[off:off + n] = raw + b"\0" * (n - len(raw))


def model_renames():
    """{имя меша/текстуры донора (нижний регистр): имя в порту} для действия wod (у нас одноимённое с другой геометрией)."""
    plan = json.load(open(MODELS_JSON, encoding="utf-8"))
    ren = {}
    for k in ("meshes", "textures"):
        for m in plan[k]:
            if m["action"] == "wod":
                ren[m["name"].lower()] = m["out"]
    return ren


def build_img(D, wod, code, renames):
    """item_img_table донора → наша: код, атласы wod_<имя> (@132/@172), меш/текстура wod_<имя> при действии wod."""
    g = bytearray(D.img(wod))
    struct.pack_into("<i", g, 0, code)
    for off in (4, 36, 68, 100):
        nm = WI.cstr(bytes(g[off:off + 32]))
        if nm.lower() in renames:
            put_str(g, off, 32, renames[nm.lower()], "ascii")
    for off in (132, 172):
        nm = WI.cstr(bytes(g[off:off + 32]))
        if nm:
            put_str(g, off, 32, WI.ICON_PREFIX + nm, "ascii")
    return bytes(g)


def build_gear(O, drec, code, row, name_ru, premium=False):
    """Экипировка §3а: поля донора + уровень по опорам + характеристики от аналога (разброс стата @136/@138 — он множитель
    базового соула аналога, ревизия REVIEW_wave4_5 п.1), опции/вес/пол/служебные/семейство LinkWordCode — от аналога
    (владелец 29.10: «меч = меч»); цена покупки донора, продажа /5 (заглушка донора «не продаётся» ≥ 10 млн → продажа 1).
    → (запись 248 б, код аналога)."""
    b = bytearray(drec)
    lvl = map_level(i32(drec, 80), i32(drec, 92))
    an = O.analog(drec, lvl, premium)
    if an is None:
        raise SystemExit("нет аналога для %d (%s)" % (i32(drec, 0), WI.cstr(drec[4:68])))
    ar = O.rec[an]
    struct.pack_into("<i", b, 0, code)
    put_str(b, 4, 64, name_ru)
    struct.pack_into("<i", b, 80, lvl)
    for off, n in ((72, 4), (88, 4), (104, 24), (184, 4), (188, 4), (192, 4), (220, 4)):
        b[off:off + n] = ar[off:off + n]          # вес, пол, 6 полей групп опций, служебные, LinkWordCode — от аналога
    lo, hi = u16(ar, 136), u16(ar, 138)
    if WI.cstr(drec[4:68]).startswith(ACT_PREFIX):
        lo, hi = round(lo * 110 / 90.0), round(hi * 130 / 110.0)   # «[Act.]» = усиленный близнец: у донора 110/130 против 90/110
    struct.pack_into("<2H", b, 136, lo, hi)
    if i32(drec, 128) in ACCESSORY_TYPES:
        struct.pack_into("<i", b, 92, 0)
        b[96:104] = ar[96:104]
    buy = i32(drec, 164)
    struct.pack_into("<i", b, 160, 1 if buy >= NOT_FOR_SALE else buy // 5)
    struct.pack_into("<i", b, 196, min(i32(drec, 196), 1))
    if i32(drec, 96) < 0:
        struct.pack_into("<i", b, 96, 0)          # вид −1 у одной записи донора (wod_item_record.md §2) → 0 (сервер u8)
    for off in (216, 228, 232):
        struct.pack_into("<i", b, off, 0)
    struct.pack_into("<2i", b, 240, row, row)
    return bytes(b), an


def build_container(O, drec, code, row, name_ru, sub, idx):
    """Коробка/бустер: образец нашей коробки + уровень (опоры)/профессия/категория/привязка/грейд донора; пакет или бустер idx;
    продажа 1, покупка 10 000; LinkWordCode 0 (группа вариантов не нужна)."""
    b = bytearray(O.rec[TPL_BOX])
    struct.pack_into("<i", b, 0, code)
    put_str(b, 4, 64, name_ru)
    job = i32(drec, 92)
    struct.pack_into("<i", b, 80, map_level(i32(drec, 80), job) if i32(drec, 80) > 0 else i32(b, 80))
    for off in (84, 92, 156, 200, 236):
        struct.pack_into("<i", b, off, i32(drec, off))
    struct.pack_into("<i", b, 132, sub)
    struct.pack_into("<i", b, 160, BOX_SELL)
    struct.pack_into("<i", b, 164, BOX_BUY)
    struct.pack_into("<i", b, 216, idx)
    struct.pack_into("<h", b, 220, 0)
    for off in (228, 232):
        struct.pack_into("<i", b, off, 0)
    struct.pack_into("<2i", b, 240, row, row)
    return bytes(b)


def build_misc(O, drec, code, row, name_ru, spec):
    """«Прочее» с описанной системой: запись донора, тип 20 → 11, подтип и правки полей — из wod_scope.json
    ({"sub": N, "set": {"смещение": значение}, "copy_from": наш код, "copy": [[смещение, длина]]}); уровень по опорам."""
    b = bytearray(drec)
    if spec.get("copy_from"):
        ar = O.rec[int(spec["copy_from"])]
        for off, n in spec.get("copy", []):
            b[off:off + n] = ar[off:off + n]
    struct.pack_into("<i", b, 0, code)
    put_str(b, 4, 64, name_ru)
    b[104:128] = bytes(24)                        # группы опций донора не переносятся; у нашего «прочего» блок нулевой (11 265 из 11 4xx)
    struct.pack_into("<i", b, 128, MISC_TYPE_OUR)
    struct.pack_into("<i", b, 132, int(spec.get("sub", i32(drec, 132))))
    if i32(drec, 80) > 1:
        struct.pack_into("<i", b, 80, map_level(i32(drec, 80), i32(drec, 92)))
    for off in (216, 228, 232):
        struct.pack_into("<i", b, off, 0)         # @216 — пакет/бустер донора: у «прочего» не нужен (иначе гейт GachaItemRate)
    for off, v in spec.get("set", {}).items():
        struct.pack_into("<i", b, int(off), int(v))
    struct.pack_into("<2i", b, 240, row, row)
    return bytes(b)


def recode(x, newcode, scope):
    """Позиция содержимого донора → наш код (предмет набора, доказанное соответствие предмета/F/S)."""
    if 1 <= x <= 710:
        return int(scope["map_func"][str(x)])
    if 7001 <= x < ITEM_FIRST:
        return int(scope["map_sowel"][str(x)])
    if x in newcode:
        return newcode[x]
    return int(scope["map_item"][str(x)])


# ── сборка ─────────────────────────────────────────────────────────────────────────────────────────
def ours(r, key="_source"):
    """Строка ведомости — этого генератора."""
    return str(r.get(key) or "").startswith(SRC_PREFIX)


def write_ledger_csv(path, rows, fields):
    """Ведомость хвоста: UTF-8 без BOM (как прежние генераторы)."""
    with open(path, "w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields, extrasaction="raise")
        w.writeheader()
        w.writerows(rows)


def decoder():
    """Модуль decode_item_db (реестр колонок и rd())."""
    sys.path.insert(0, os.path.dirname(DECODER))
    import decode_item_db as DEC   # noqa: E402
    return DEC


def dict_row(DEC, table, rowno, data, src):
    """Строка ведомости из байтов записи по колонкам реестра декодера."""
    row = {"_row": rowno}
    for (off, typ, nm) in DEC.REGISTRY[table]["cols"]:
        row[nm] = DEC.rd(data, off, typ)
    row["DataHex"] = data.hex()
    row["_source"] = src
    return row


def our_tooltip(code):
    """(RU, EN) подсказки нашего предмета: item_tooltip.csv по ItemCode + EN-оверлей по строке подсказки."""
    row = next(r for r in WI.load_csv(os.path.join(ITEMDB_LIVE, "item_tooltip.csv")) if int(r["ItemCode"]) == code)
    en = {r["Name"]: r["Text"] for r in WI.load_csv(os.path.join(EN_LIVE, "DT_Loc_item_tooltip_en.csv"))}
    return row["Description"], en.get("%s.Description" % row["_row"], "")


def load_texts():
    """{код WoD: тексты RU/EN} из data/wod/wod_item_texts.csv."""
    return {int(r["wod_code"]): r for r in WI.load_csv(WI.TEXTS_CSV)}


def plan():
    """Итоговый реестр (FINAL_CSV) + строки пакетов/бустеров по порядку кодов → список словарей."""
    rows = WI.load_csv(FINAL_CSV)
    for r in rows:
        r["wod_code"], r["our_code"] = int(r["wod_code"]), int(r["our_code"])
        r["idx"] = int(r["idx"]) if r.get("idx") else None     # строка пакета (с 0) / Code бустера (с 1) — из scope
    return rows


def container_pairs(D, P, scope):
    """{наш код коробки/бустера: [(наш код, кол-во|вес)]} — содержимое донора, перекодированное."""
    newcode = {r["wod_code"]: r["our_code"] for r in P}
    res = {}
    for r in P:
        if r["cat"] in ("box", "booster"):
            res[r["our_code"]] = [(recode(x, newcode, scope), q) for x, q in D.content(r["wod_code"])]
    return res


def build_client(D, O, P, scope):
    """Ведомости клиента: хвосты item_list / item_img_table / item_tooltip / packagetable / booster_list, EN item_list /
    item_tooltip. Повторный прогон идемпотентен: свои строки (метка wod:) пересобираются, чужие не трогаются.
    → ({наш код: запись 248 б}, {наш код: код аналога}) для сервера."""
    DEC = decoder()
    texts = load_texts()
    pairs = container_pairs(D, P, scope)
    ren = model_renames()
    live_it = WI.load_csv(os.path.join(ITEMDB_LIVE, "item_list.csv"))
    live_tip = WI.load_csv(os.path.join(ITEMDB_LIVE, "item_tooltip.csv"))
    codes = {r["our_code"] for r in P}
    # Строки — НА СВОИХ местах (повторная сборка после заливки и после чужих сегментов, например премиум-наборов, идёт так же):
    #   item_list/img плотные (строка = код − 20551), item_tooltip ключевая (свой код — прежняя строка, новый — в конец).
    if any(int(r["_row"]) != int(r["ItemCode"]) - ITEM_FIRST for r in live_it):
        raise SystemExit("ОТКАЗ: item_list не плотный (строка ≠ код − %d)" % ITEM_FIRST)
    have = {int(r["ItemCode"]) for r in live_it}
    fresh = [r["our_code"] for r in P if r["our_code"] not in have]
    if fresh != list(range(len(live_it) + ITEM_FIRST, len(live_it) + ITEM_FIRST + len(fresh))):
        raise SystemExit("ОТКАЗ: новые коды %s не продолжают item_list (последний %d)" % (fresh[:5], len(live_it) + ITEM_FIRST - 1))
    tip_row = {int(r["ItemCode"]): int(r["_row"]) for r in live_tip}
    new_tip = iter(range(len(live_tip), len(live_tip) + len(P)))
    L = {k: [] for k in ("item_list", "item_img_table", "item_tooltip", "packagetable", "booster_list")}
    en_name, en_tip, recs, analog = [], [], {}, {}
    for r in P:
        wod, code = r["wod_code"], r["our_code"]
        row = code - ITEM_FIRST
        tip_i = tip_row[code] if code in tip_row else next(new_tip)
        tx = dict(texts[wod])
        spec_m = scope["misc"].get(str(wod), {})
        if spec_m.get("tip_from"):                 # подсказка донора ошибочна — берётся у нашего предмета того же смысла
            tx["tip_ru"], tx["tip_en"] = our_tooltip(int(spec_m["tip_from"]))
            tx["tip_en"] = tx["tip_en"] or spec_m.get("tip_en", "")   # у образца EN-оверлея нет — перевод его RU в wod_scope
        src = SRC_PREFIX + r["cat"]
        drec = D.rec(wod)
        if r["cat"] == "gear":
            rec, analog[code] = build_gear(O, drec, code, row, tx["name_ru"], wod in D.premium)
        elif r["cat"] in ("box", "booster"):
            rec = build_container(O, drec, code, row, tx["name_ru"], i32(drec, 132), r["idx"])
        else:
            rec = build_misc(O, drec, code, row, tx["name_ru"], scope["misc"][str(wod)])
        recs[code] = rec
        L["item_list"].append(dict_row(DEC, "item_list.bin", row, rec, src))
        L["item_img_table"].append(dict_row(DEC, "item_img_table.bin", row, build_img(D, wod, code, ren), src))
        L["item_tooltip"].append({"_row": tip_i, "ItemCode": code, "Description": tx["tip_ru"], "_orig": "", "_source": src})
        why = "29.10.2026 перенос предмета донора WorldOfDreams %d (asda_wod_records.py)" % wod
        en_name.append({"key": "%d.ItemName" % row, "value": tx["name_en"], "was": None, "why": why, "source": src})
        if tx["tip_en"]:
            en_tip.append({"key": "%d.Description" % tip_i, "value": tx["tip_en"], "was": None, "why": why, "source": src})
        if r["cat"] == "box":
            pr = {"_row": r["idx"], "Index": r["idx"]}
            pp = pairs[code] + [(-1, 0)] * (5 - len(pairs[code]))
            for j, (x, q) in enumerate(pp):
                pr["Item%d" % j], pr["Qty%d" % j] = x, q
            pr.update({"Item5": 0, "Qty5": 0, "Reserved52": 0, "_source": src})
            L["packagetable"].append(pr)
        elif r["cat"] == "booster":
            b = bytearray(284)
            struct.pack_into("<i", b, 0, r["idx"])
            for j, (x, w) in enumerate(pairs[code]):
                struct.pack_into("<2i", b, 4 + 8 * j, x, w)
            L["booster_list"].append(dict_row(DEC, "booster_list.bin", r["idx"] - 1, bytes(b), src))
    # pricetable (ключ ItemCode): строка нашего аналога с нашим кодом — экипировке (ранг dw04 для окна заточки), «прочему»
    #   с аналогом (свиток 20954, зелье, яйцо); коробкам строка не нужна (как 79429… у премиум-коробок).
    live_pt = WI.load_csv(os.path.join(ITEMDB_LIVE, "pricetable.csv"))
    pt_by = {int(x["ItemCode"]): x for x in live_pt if int(x["ItemCode"]) not in codes}
    pt_row = {int(x["ItemCode"]): int(x["_row"]) for x in live_pt}        # ключевая: свой код — прежняя строка
    new_pt = iter(range(len(live_pt), len(live_pt) + len(P)))
    L["pricetable"] = []
    for r in P:
        spec = scope["misc"].get(str(r["wod_code"]), {})
        src_code = analog.get(r["our_code"]) or spec.get("copy_from") or spec.get("sc_from")
        if src_code and int(src_code) in pt_by:
            x = dict(pt_by[int(src_code)])
            x.update({"_row": pt_row[r["our_code"]] if r["our_code"] in pt_row else next(new_pt), "ItemCode": r["our_code"],
                      "_source": SRC_PREFIX + r["cat"]})
            L["pricetable"].append(x)
    # хвосты ItemDB: чужие строки (без метки) + наши; _row обязан продолжать таблицу
    for t in L:
        p = os.path.join(TAIL_DIR, t + ".csv")
        led = WI.load_csv(p) if os.path.isfile(p) else []      # pricetable: ведомость заводит этот генератор (29.10)
        keep = [x for x in led if not ours(x)]
        new = L[t]
        rows = sorted(keep + new, key=lambda x: int(x["_row"]))
        nums = [int(x["_row"]) for x in rows]
        if nums and nums != list(range(nums[0], nums[0] + len(nums))):
            raise SystemExit("ОТКАЗ: ведомость %s после замены своих строк — дыра/повтор строк" % t)
        if not new and len(keep) == len(led):
            continue                          # своих строк нет и не было — ведомость не трогаем
        fields = list(led[0].keys()) if led else list(new[0].keys())
        if "_source" not in fields:
            fields.append("_source")
        for x in keep:
            x.setdefault("_source", "")
        write_ledger_csv(p, rows, fields)
    for fn, new in (("item_list.json", en_name), ("item_tooltip.json", en_tip)):
        p = os.path.join(LOC_EN, fn)
        raw = open(p, "rb").read()
        doc = json.loads(raw.decode("utf-8"))
        keep = [x for x in doc["rows"] if not ours(x, "source")]
        clash = {x["key"] for x in keep} & {x["key"] for x in new}
        if clash:
            raise SystemExit("ключи EN уже заняты: %s" % sorted(clash)[:5])
        fresh = {x["key"]: x for x in new}     # свои ключи — на прежнем месте, новые — в конец (как asda_premium_box_add)
        doc["rows"] = [fresh.pop(x["key"], x) if ours(x, "source") else x for x in doc["rows"]
                       if not ours(x, "source") or x["key"] in fresh] + [x for x in new if x["key"] in fresh]
        nl = "\r\n" if b"\r\n" in raw[:4096] else "\n"
        with open(p, "w", encoding="utf-8", newline=nl) as f:
            f.write(json.dumps(doc, ensure_ascii=False, indent=2) + "\n")
    out("клиент: item_list/img +%d (строки %d…), подсказки +%d, пакеты +%d, бустеры +%d, EN +%d/+%d"
        % (len(L["item_list"]), P[0]["our_code"] - ITEM_FIRST if P else -1, len(L["item_tooltip"]), len(L["packagetable"]), len(L["booster_list"]),
           len(en_name), len(en_tip)))
    return recs, analog


IT_WIDTH = {   # ширина полей 71-байтовой записи item_table (server_item_tail_extend.IT_WIDTH)
    0: "i32", 1: "u8", 2: "i16", 3: "u8", 4: "u8", 5: "u8", 6: "u8", 7: "u8", 8: "u8",
    9: "i32", 10: "i16", 11: "u8", 12: "u8", 13: "i16", 14: "u8", 15: "u8", 16: "u8",
    17: "u8", 18: "i16", 19: "i16", 20: "u8", 21: "u8", 22: "u8", 23: "u8", 24: "u8",
    25: "i32", 26: "i32", 27: "u8", 28: "u8", 29: "i16", 30: "u8", 31: "i32", 32: "i16",
    33: "u8", 34: "u8", 35: "u8", 36: "u8", 37: "i16", 38: "u8", 39: "i16", 40: "i16", 41: "i32"}
LIMITS = {"u8": (0, 255), "i16": (-32768, 32767), "i32": (-2 ** 31, 2 ** 31 - 1)}


def colmap():
    """Серверная колонка item_table → (вид, смещение клиентской записи) — как server_item_tail_extend.COLMAP."""
    m = {c: ("<i", 68 + 4 * (c - 1)) for c in range(1, 18)}
    m[18], m[19] = ("<H", 136), ("<H", 138)
    m.update({c: ("<i", 68 + 4 * (c - 2)) for c in range(20, 42)})
    return m


def item_row(code, rec):
    """Строка item_table.txt из клиентской записи (42 токена); значение шире поля — отказ (сервер молча обрезал бы).
    Ток.10/13 (группы опций): клиент 0xFFFF = сервер −1 (decode_item_db, ledger_fields OptionGroup0/1)."""
    row = [code]
    for c, (fmt, off) in sorted(colmap().items()):
        v = struct.unpack_from(fmt, rec, off)[0]
        if c in (10, 13) and v & 0xFFFF == 0xFFFF:
            v = -1
        lo, hi = LIMITS[IT_WIDTH[c]]
        if not lo <= v <= hi:
            raise SystemExit("код %d: ток.%d = %d не влезает в %s" % (code, c, v, IT_WIDTH[c]))
        row.append(v)
    return "\t".join(str(v) for v in row).encode("ascii")


def md5f(path):
    """md5 файла."""
    import hashlib
    return hashlib.md5(open(path, "rb").read()).hexdigest()


def build_server(D, P, scope, recs, analog):
    """srv_out: item_table / SowelCombine / PackageItem_Table / GachaItemRateTable × 3 GameServer из снимка srv_in."""
    pairs = container_pairs(D, P, scope)
    codes = {r["our_code"] for r in P}
    for t in SRV_TABLES:
        for d in gs_dirs()[1:]:
            if md5f(os.path.join(SRV_IN, "GameServer", t)) != md5f(os.path.join(SRV_IN, d, t)):
                raise SystemExit("ОТКАЗ: %s/%s отличается от GameServer — правка вслепую запрещена" % (d, t))
    it_cnt, it_body = read_srv(os.path.join(SRV_IN, "GameServer", "item_table.txt"))
    sc_cnt, sc_body = read_srv(os.path.join(SRV_IN, "GameServer", "SowelCombine.txt"))
    pk_cnt, pk_body = read_srv(os.path.join(SRV_IN, "GameServer", "PackageItem_Table.txt"))
    gr_cnt, gr_body = read_srv(os.path.join(SRV_IN, "GameServer", "GachaItemRateTable.txt"))
    pkgs = {r["idx"] for r in P if r["cat"] == "box"}
    bsts = {r["idx"] for r in P if r["cat"] == "booster"}
    # Снимок обязан быть подряд; свои строки потом заменяются НА МЕСТЕ, новые дописываются (splice) — так же собирают
    #   соседние сегменты (asda_premium_box_add.py); итог проверяется на непрерывность.
    for nm, body, first in (("item_table", it_body, ITEM_FIRST), ("SowelCombine", sc_body, ITEM_FIRST),
                            ("PackageItem_Table", pk_body, 0), ("GachaItemRateTable", gr_body, 1)):
        if [int(toks(ln)[0]) for ln in body] != list(range(first, first + len(body))):
            raise SystemExit("ОТКАЗ: %s снимка — не подряд" % nm)
    sc = {int(toks(ln)[0]): ln for ln in sc_body}
    sc_tpl = toks(sc[TPL_BOX])
    if len(sc_tpl) != 57:
        raise SystemExit("серверный образец SowelCombine %d сменился" % TPL_BOX)
    it_new, sc_new, pk_new, gr_new = [], [], [], []
    for r in P:
        code = r["our_code"]
        it_new.append(item_row(code, recs[code]))
        spec = scope["misc"].get(str(r["wod_code"]), {})
        if code in analog:
            base = toks(sc[analog[code]])        # экипировка — базовый соул и ранг аналога
        elif spec.get("sc_from"):
            base = toks(sc[int(spec["sc_from"])])   # «прочее» — строка нашего аналога (яйцо: шанс вылупления ток.45)
        else:
            base = sc_tpl
        sc_new.append("\t".join([str(code)] + base[1:]).encode("ascii"))
        if r["cat"] == "box":
            pp = pairs[code] + [(-1, 0)] * (5 - len(pairs[code]))
            pk_new.append(("%d\t" % r["idx"] + "\t".join("%d\t%d" % p for p in pp)).encode("ascii"))
        elif r["cat"] == "booster":
            pp = pairs[code] + [(0, 0)] * (35 - len(pairs[code]))
            gr_new.append(("%d\t" % r["idx"] + "\t".join("%d\t%d" % p for p in pp)).encode("ascii"))
    def splice(body, new, first):
        """Свои строки (ключ = 1-й токен) — на место прежних, остальные — в конец; итог подряд с first."""
        own = {int(toks(ln)[0]): ln for ln in new}
        res = [own.pop(int(toks(ln)[0]), ln) for ln in body]
        res += [own[k] for k in sorted(own)]
        if [int(toks(ln)[0]) for ln in res] != list(range(first, first + len(res))):
            raise SystemExit("ОТКАЗ: после вставки таблица не подряд (новый код/пакет/бустер не продолжает таблицу)")
        return res
    it_body, sc_body = splice(it_body, it_new, ITEM_FIRST), splice(sc_body, sc_new, ITEM_FIRST)
    pk_body, gr_body = splice(pk_body, pk_new, 0), splice(gr_body, gr_new, 1)
    it_new, sc_new, pk_new, gr_new = [], [], [], []
    manifest = {"tag": "20261029_wod_items", "files": {}}
    for d in gs_dirs():
        n = len(it_body) + len(it_new)
        write_srv(os.path.join(SRV_OUT, d, "item_table.txt"), n, it_body + it_new)
        write_srv(os.path.join(SRV_OUT, d, "SowelCombine.txt"), n, sc_body + sc_new)
        write_srv(os.path.join(SRV_OUT, d, "PackageItem_Table.txt"), len(pk_body) + len(pk_new), pk_body + pk_new)
        write_srv(os.path.join(SRV_OUT, d, "GachaItemRateTable.txt"), len(gr_body) + len(gr_new), gr_body + gr_new)
        for t in SRV_TABLES:
            a, o = os.path.join(SRV_IN, d, t), os.path.join(SRV_OUT, d, t)
            if md5f(a) != md5f(o):
                manifest["files"][d + "\\ItemTable_Info\\" + t] = {"local": os.path.relpath(o, SRV_TOOLS).replace("\\", "/"),
                                                                   "base": md5f(a), "new": md5f(o)}
    with open(MANIFEST, "w", encoding="utf-8", newline="\n") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=1)
        f.write("\n")
    out("сервер: item_table/SowelCombine %d → %d, PackageItem %d → %d, GachaItemRate %d → %d; файлов к выкладке %d"
        % (it_cnt, len(it_body) + len(it_new), pk_cnt, len(pk_body) + len(pk_new), gr_cnt, len(gr_body) + len(gr_new),
           len(manifest["files"])))


# ── стенд ──────────────────────────────────────────────────────────────────────────────────────────
def verify(base=SRV_OUT, label="srv_out"):
    """То, что сделает GameServer со srv_out: загрузчики (счёт, подряд, 42/57 токенов), стартовые гейты (item_table ==
    SowelCombine; +63 коробки ≤ числа пакетов, бустера ≤ числа строк GachaItemRate; стопочный код дважды в пакете → exit(1)),
    содержимое — существующие коды (предмет 20551…макс, F из Function_Table — не проверяется здесь, S 7001+); прежние строки
    байт в байт; клиентские ведомости = серверу (цена, пакет/бустер)."""
    P = plan()
    codes = {r["our_code"] for r in P}
    errs = []
    for d in gs_dirs():
        it_cnt, it = read_srv(os.path.join(base, d, "item_table.txt"))
        sc_cnt, sc = read_srv(os.path.join(base, d, "SowelCombine.txt"))
        pk_cnt, pk = read_srv(os.path.join(base, d, "PackageItem_Table.txt"))
        gr_cnt, gr = read_srv(os.path.join(base, d, "GachaItemRateTable.txt"))
        for nm, cnt, body, ntok in (("item_table", it_cnt, it, 42), ("SowelCombine", sc_cnt, sc, 57)):
            if cnt != len(body):
                errs.append("%s/%s: счёт %d ≠ строк %d" % (d, nm, cnt, len(body)))
            for i, ln in enumerate(body):
                t = toks(ln)
                if int(t[0]) != ITEM_FIRST + i or len(t) != ntok:
                    errs.append("%s/%s: строка %d код %s токенов %d" % (d, nm, i, t[0], len(t)))
                    break
        if it_cnt != sc_cnt:
            errs.append("%s: item_table %d ≠ SowelCombine %d" % (d, it_cnt, sc_cnt))
        rows = {int(toks(ln)[0]): toks(ln) for ln in it}
        maxcode = ITEM_FIRST + it_cnt - 1
        stack = {c for c, t in rows.items() if t[23] == "1"}
        pkg = {}
        for i, ln in enumerate(pk):
            t = [int(x) for x in toks(ln)]
            if t[0] != i or len(t) != 11:
                errs.append("%s/PackageItem: строка %d = %r" % (d, i, t[:3])); break
            pkg[i] = [(t[1 + 2 * j], t[2 + 2 * j]) for j in range(5)]
        gacha = {}
        for i, ln in enumerate(gr):
            t = [int(x) for x in toks(ln)]
            if t[0] != i + 1 or len(t) != 71:
                errs.append("%s/GachaItemRate: строка %d = %r (токенов %d)" % (d, i, t[:3], len(t))); break
            gacha[t[0]] = [(t[1 + 2 * j], t[2 + 2 * j]) for j in range(35)]
        if pk_cnt != len(pk) or gr_cnt != len(gr):
            errs.append("%s: счёт пакетов %d/%d, бустеров %d/%d" % (d, pk_cnt, len(pk), gr_cnt, len(gr)))
        for r in P:
            c = r["our_code"]
            t = rows.get(c)
            if not t:
                errs.append("%s: нет строки %d" % (d, c)); continue
            if r["cat"] in ("box", "booster"):
                subs = BOX_SUBS if r["cat"] == "box" else (BOOSTER_SUB,)
                if int(t[16]) != MISC_TYPE_OUR or int(t[17]) not in subs:
                    errs.append("%s: %d тип %s/%s" % (d, c, t[16], t[17]))
                idx = int(t[39])
                if idx != r["idx"]:
                    errs.append("%s: %d ток.39 %d ≠ %d" % (d, c, idx, r["idx"]))
                content = pkg.get(idx) if r["cat"] == "box" else gacha.get(idx)
                if content is None:
                    errs.append("%s: %d — нет строки %s %d" % (d, c, "пакета" if r["cat"] == "box" else "бустера", idx)); continue
                items = [x for x, q in content if x > 0]
                if not items:
                    errs.append("%s: %d — пустое содержимое" % (d, c))
                for x in items:
                    if x >= ITEM_FIRST and x > maxcode:
                        errs.append("%s: %d содержит %d вне item_table" % (d, c, x))
                if r["cat"] == "box":
                    for x, q in content:
                        tx = rows.get(x)
                        if x <= 0 or not tx:
                            continue
                        stackable = tx[3] == "4" and tx[23] == "1" and tx[24] == "1" and tx[16] != "10"   # sub_55B7F0
                        if q > 1 and not stackable:
                            errs.append("%s: пакет %d — %d ×%d не стопочный (выдача пакетом невозможна)" % (d, idx, x, q))
                        if stackable and (q > 32767 or q * int(tx[2]) > 32767):
                            errs.append("%s: пакет %d — стопка %d ×%d не влезает в i16" % (d, idx, x, q))
                dup = [x for x, n in collections.Counter(items).items() if n > 1 and x in stack]
                if r["cat"] == "box" and dup:
                    errs.append("%s: пакет %d — стопочный код дважды %s (exit(1) на старте)" % (d, idx, dup))
        for t in SRV_TABLES:
            a = open(os.path.join(SRV_IN, d, t), "rb").read().split(b"\r\n", 1)[1]
            o = open(os.path.join(base, d, t), "rb").read().split(b"\r\n", 1)[1]
            own = ({str(c).encode() for c in codes} if t in ("item_table.txt", "SowelCombine.txt") else
                   {str(r["idx"]).encode() for r in P if r["cat"] == ("box" if t.startswith("Package") else "booster")})
            a_lines = [ln for ln in a.split(b"\r\n") if ln.strip() and ln.split()[0] not in own]
            o_lines = [ln for ln in o.split(b"\r\n") if ln.strip() and ln.split()[0] not in own]   # свои — на месте
            if o_lines != a_lines:
                errs.append("%s/%s: прежние строки изменены" % (d, t))
        out("%-8s %-17s item_table %d, пакетов %d, бустеров %d; наших %d" % (label, d, it_cnt, pk_cnt, gr_cnt, len(P)))
    tail = {int(r["ItemCode"]): r for r in WI.load_csv(os.path.join(TAIL_DIR, "item_list.csv")) if ours(r)}
    srv = {int(toks(ln)[0]): toks(ln) for ln in read_srv(os.path.join(base, "GameServer", "item_table.txt"))[1]}
    for r in P:
        x, t = tail.get(r["our_code"]), srv.get(r["our_code"])
        if not x or not t or int(x["Price"]) != int(t[26]) or int(x["DurationOrEnchant"]) != int(t[39]) \
                or int(x["RequiredLevel"]) != int(t[4]) or int(x["ItemType"]) != int(t[16]):
            errs.append("клиент ≠ сервер: %d" % r["our_code"])
    out("стенд %s: ошибок %d" % (label, len(errs)))
    for x in errs[:30]:
        out("  !", x)
    return not errs


# ── клиент: декод в боевой каталог, EN, задание точечной заливки DT (путь 1:1 asda_premium_box_add.cmd_client_apply) ──
CLIENT_TABLES = ("item_list", "item_img_table", "item_tooltip", "packagetable", "pricetable", "booster_list")
DT = {t: "/Game/AsdaAssets/Data/ItemDB/DT_%s" % t for t in CLIENT_TABLES}
DT.update({"en_item_list": "/Game/AsdaAssets/Text/L10N/en/DT_Loc_item_list_en",
           "en_item_tooltip": "/Game/AsdaAssets/Text/L10N/en/DT_Loc_item_tooltip_en"})


def cmd_client_apply(a):
    """Боевые CSV ItemDB (+наши строки в 6 таблицах), EN-оверлеи (+наши ключи), задание точечной заливки DT
    (a.backup/dt_point_jobs.json). Пробный декод в отдельную папку → сверка «меняются только наши строки» → боевой декод."""
    import shutil
    import asda_premium_box_add as PB
    P = plan()
    bdir = a.backup
    os.makedirs(bdir, exist_ok=True)
    live = {t: os.path.join(ITEMDB_LIVE, t + ".csv") for t in CLIENT_TABLES}
    live_json = [os.path.join(ITEMDB_LIVE, t + ".json") for t in CLIENT_TABLES]
    en = {t: os.path.join(EN_LIVE, "DT_Loc_%s_en.csv" % t) for t in ("item_list", "item_tooltip")}
    bk = PB._backup(list(live.values()) + [p for p in live_json if os.path.isfile(p)] + list(en.values()), bdir)
    tmp = os.path.join(bdir, "_decode_tmp")
    shutil.rmtree(tmp, ignore_errors=True)
    only = ",".join(t + ".bin" for t in CLIENT_TABLES)
    rc, log = PB._run([sys.executable, DECODER, "--out", tmp, "--only", only])
    open(os.path.join(bdir, "decode_tmp.log"), "w", encoding="utf-8").write(log)
    rc2, log2 = PB._run([sys.executable, os.path.join(HERE, "asda_apply_name_overlay.py"), "--itemdb", tmp,
                         "--tables", "item_list,item_tooltip"])
    open(os.path.join(bdir, "name_overlay_tmp.log"), "w", encoding="utf-8").write(log2)
    if rc or rc2:
        raise SystemExit("пробный декод/слой имён: коды %d/%d — журналы в %s" % (rc, rc2, bdir))
    codes = {r["our_code"] for r in P}
    tmp_rows = {t: WI.load_csv(os.path.join(tmp, t + ".csv")) for t in CLIENT_TABLES}
    row_of = {int(r["ItemCode"]): r["_row"] for r in tmp_rows["item_list"]}
    tip_of = {int(r["ItemCode"]): r["_row"] for r in tmp_rows["item_tooltip"]}
    own = {"item_list": [row_of[c] for c in codes], "item_img_table": [row_of[c] for c in codes],
           "item_tooltip": [tip_of[c] for c in codes],
           "packagetable": [str(r["idx"]) for r in P if r["cat"] == "box"],
           "booster_list": [str(r["idx"] - 1) for r in P if r["cat"] == "booster"],
           "pricetable": [r["_row"] for r in tmp_rows["pricetable"] if int(r["ItemCode"]) in codes]}
    have = {t: {r["_row"] for r in WI.load_csv(p)} for t, p in live.items()}
    errs = []
    for t, p in live.items():
        errs += PB._diff_only_added(p, os.path.join(tmp, t + ".csv"), "_row", [x for x in own[t] if x not in have[t]], own[t])
    if errs:
        for e in errs[:20]:
            out("  !", e)
        raise SystemExit("ОТКАЗ: пробный декод меняет не только наши строки — боевой каталог не тронут")
    out("пробный декод: меняются только наши строки — OK (%s)" % ", ".join("%s %d" % (t, len(v)) for t, v in own.items()))
    if any(PB.md5f(p) != PB.md5f(os.path.join(tmp, t + ".csv")) for t, p in live.items()):
        rc, log = PB._run([sys.executable, DECODER, "--only", only, "--accept-diff"])
        open(os.path.join(bdir, "decode_live.log"), "w", encoding="utf-8").write(log)
        if rc:
            raise SystemExit("боевой декод: код %d — журнал %s" % (rc, os.path.join(bdir, "decode_live.log")))
    for t, p in live.items():
        if PB.md5f(p) != PB.md5f(os.path.join(tmp, t + ".csv")):
            raise SystemExit("ОТКАЗ: боевой %s ≠ пробному декоду (см. %s)" % (t, bdir))
    out("боевые CSV ItemDB = пробному декоду")
    etmp = os.path.join(bdir, "_en_tmp")
    shutil.rmtree(etmp, ignore_errors=True)
    rc, log = PB._run([sys.executable, os.path.join(HERE, "extract_text_lang.py"), "--lang", "en", "--out", etmp,
                       "--only", "item_list,item_tooltip"], cwd=UPROJ)
    open(os.path.join(bdir, "extract_en.log"), "w", encoding="utf-8").write(log)
    if rc:
        raise SystemExit("extract_text_lang: код %d — журнал %s" % (rc, bdir))
    en_want = {"item_list": ["%s.ItemName" % row_of[c] for c in codes],
               "item_tooltip": ["%s.Description" % tip_of[c] for c in codes]}
    for t, p in en.items():
        cur = {r["Name"] for r in WI.load_csv(p)}
        new_keys = {r["Name"] for r in WI.load_csv(os.path.join(etmp, os.path.basename(p)))}
        want = [k for k in en_want[t] if k in new_keys]
        e = PB._diff_only_added(p, os.path.join(etmp, os.path.basename(p)), "Name", [k for k in want if k not in cur], want)
        if e:
            for x in e[:10]:
                out("  !", x)
            raise SystemExit("ОТКАЗ: EN %s меняет не только наши ключи" % t)
        shutil.copy2(os.path.join(etmp, os.path.basename(p)), p)
    out("EN-оверлеи: +ключи только наши — перенесены")
    jobs = [{"dt": DT[t], "csv_new": live[t], "csv_old": bk(live[t]), "allow_rows": own[t]} for t in CLIENT_TABLES if own[t]]
    jobs += [{"dt": DT["en_" + t], "csv_new": en[t], "csv_old": bk(en[t]), "allow_rows": en_want[t]} for t in en]
    for j in jobs:
        for k in ("csv_new", "csv_old"):
            j[k] = j[k].replace("\\", "/")
        j["allow_rows"] = [x for x in j["allow_rows"] if x is not None]
    with open(os.path.join(bdir, "dt_point_jobs.json"), "w", encoding="utf-8") as f:
        json.dump(jobs, f, ensure_ascii=False, indent=1)
    out("задание DT:", os.path.join(bdir, "dt_point_jobs.json"))
    return 0


def cmd_dt_apply(a):
    """Точечная заливка DT (asda_ml_dt_point.py через run_headless_safe.py) — asda_premium_box_add.cmd_dt_apply."""
    import asda_premium_box_add as PB
    return PB.cmd_dt_apply(a)


FISHING_SKEL = os.path.join(UPROJ, "Content", "AsdaAssets", "Models", "Skeletal", "Fishing")   # скелеталы удочек (рантайм)


class _Unpatched:
    """Обёртка zip донора: read() снимает доп. шифр серий 501/502 (asda_wod_items.unpatch), если он есть."""

    def __init__(self, z):
        self.z = z

    def namelist(self):
        return self.z.namelist()

    def read(self, name):
        return WI.unpatch(self.z.read(name))


def cmd_rods_export(a):
    """Скелеталы удочек донора для показа во время лова (sys_petfish.md §2): меш img-имени удочки + «_00» из CharModel.bnz
    донора, клипы fs_fr_ani_* — наши (общие, кости совпадают) → RawAssets/Models/Fishing/*.glb (импорт —
    fishing_import_models.py, инкрементально: существующие папки пропускаются)."""
    import zipfile
    import fishing_export_assets as FE
    rows = [r for r in WI.load_csv(os.path.join(TAIL_DIR, "item_img_table.csv")) if ours(r)]
    rods = sorted({r["MeshMale"] + "_00" for r in rows if r["MeshMale"].lower().startswith("ch1_n_00_fird_")})
    todo = [m for m in rods if not os.path.isdir(os.path.join(FISHING_SKEL, m))]
    out("удочек в наборе: %d мешей %s; к экспорту (нет в Skeletal/Fishing): %s" % (len(rods), rods, todo))
    zm = _Unpatched(zipfile.ZipFile(WI.WOD_MODEL_BNZ))
    za = zipfile.ZipFile(FE.ECC.MOTION_ARC)
    zmi, zai = FE._zi(zm), FE._zi(za)
    files = ["fs_fr_ani_%s.jbb" % s for s in FE.CLIP_ORDER]
    bad = []
    for m in todo:
        rec = FE.build_object(zm, zmi, za, zai, m, files, FE.OUT_MODELS)
        bones = [x.lower() for x in (rec or {}).get("bones", [])]
        if not (rec and rec["ok"] and "8a1_dummy" in bones and "bone24" in bones):
            bad.append(m)
    out("экспорт удочек: %d, проблемных %d %s" % (len(todo), len(bad), bad))
    return 1 if bad else 0


def main(argv=None):
    """Точка входа."""
    ap = argparse.ArgumentParser(description="записи предметов донора WorldOfDreams (IMPORT.md §3)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("fetch-server")
    p = sub.add_parser("scope")
    p.add_argument("--first", type=int, default=0)
    p.add_argument("--check", action="store_true")
    p.add_argument("--force", action="store_true")
    sub.add_parser("build")
    sub.add_parser("verify")
    sub.add_parser("rods-export")
    p = sub.add_parser("client-apply")
    p.add_argument("--backup", required=True)
    p = sub.add_parser("dt-apply")
    p.add_argument("--backup", required=True)
    p.add_argument("--dry", action="store_true")
    a = ap.parse_args(argv)
    if a.cmd == "fetch-server":
        return cmd_fetch_server(a)
    if a.cmd == "scope":
        return cmd_scope(a)
    if a.cmd == "build":
        D, O, scope, P = Donor(), Ours(), load_scope(), plan()
        recs, analog = build_client(D, O, P, scope)
        build_server(D, P, scope, recs, analog)
        return 0 if verify() else 2
    if a.cmd == "verify":
        return 0 if verify() else 2
    if a.cmd == "rods-export":
        return cmd_rods_export(a)
    if a.cmd == "client-apply":
        return cmd_client_apply(a)
    if a.cmd == "dt-apply":
        return cmd_dt_apply(a)


if __name__ == "__main__":
    sys.exit(main())
