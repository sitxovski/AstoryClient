# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""asda_item_dedup.py — ОБЩАЯ точка дедупликации предметов в клиентских данных порта (item_dedup, этап 2, 01.11.2026).

ЗАЧЕМ. Решение владельца 01.11.2026: предметы-дубли (одинаковые имя и характеристики, разные коды; категории A, B, C1, C2,
  C3, далее C4) сливаются в один «основной» код. Строки дублей в самих таблицах предметов (item_list, item_img_table,
  item_tooltip, pricetable, special_itemlist, sowell_list …) НЕ удаляются и не меняются: это плотные массивы
  «индекс = код − база», удаление сдвинуло бы все коды. Меняются только ССЫЛКИ на дубль в других местах: состав бустеров и
  коробок, пул премиум-гачи, обмены у NPC, стартовая экипировка, награды и цели заданий, витрина премиум-магазина, рыбалка.

КАРТА. `Tools/data/item_dedup_map.json` = {"map": {"<дубль>": <основной>}, "categories": {"<дубль>": "A B C2 …"}}; строит
  её `Reports/item_dedup/_work/dd_plan.py` (plan_map.csv). Карту ПЕРЕГЕНЕРИРУЮТ (добавятся коды C4) — поэтому всё,
  что здесь, читает карту из файла при каждом запуске и идемпотентно (повторный прогон при той же карте — 0 изменений).
  Цепочки (дубль → код, который сам дубль) разворачиваются до неподвижной точки; цикл — отказ.

КТО ПРИМЕНЯЕТ (одна реализация на всех):
  • декодер ItemDB `AstoryClient/Tools/ItemDB/decode_item_db.py` — шаг после ведомостей по колонкам `ITEMDB_CODE_FIELDS`
    (отключение `--no-dedup-map`);
  • генераторы JSON/CSV: asda_quest_target_areas, asda_import_quest_tables (SmInfo), asda_import_premium_shop_bin,
    asda_import_nsr_dialog, asda_import_quest_reward_table, asda_build_fishbook_dt, asda_quest_turnin_restore,
    asda_import_npc_tables (обмены), asda_build_event_item_server_dt, RE/tools/dump_monster_quest_items.py —
    функцией `remap()`/`remap_list()`/`remap_nsr()`; отключение для аудита — переменная окружения ASDA_NO_DEDUP_MAP=1.
  • ТОЧЕЧНАЯ правка боевых файлов (`python asda_item_dedup.py --apply`): меняются только значения-коды в известных
    полях, остальные байты файла (порядок ключей, отступы, кавычки, BOM, перевод строк) сохраняются побайтово; бэкап
    каждого файла до правки — `@@ASTORY_DEV_ROOT@@\_backup\<дата>_item-dedup\<путь от @@ASTORY_DEV_ROOT@@>` (asda_headless_tool.backup_file).

Запуск:
  python asda_item_dedup.py              — пробный прогон: сколько замен по каждому файлу (ничего не пишется)
  python asda_item_dedup.py --apply      — записать (с бэкапом); повторный запуск при той же карте даёт 0 замен
  python asda_item_dedup.py --only booster_list,FishBook.csv  — подмножество целей (подстрока ключа цели)
Побочных эффектов при импорте модуля нет (закон tools-no-import-side-effects).
"""
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))                         # AstoryClient/Tools
PROJECT = os.path.dirname(HERE)                                           # AstoryClient
PORT = os.path.dirname(PROJECT)                                           # port
MAP_PATH = os.path.join(HERE, "data", "item_dedup_map.json")             # карта «дубль → основной»
ENV_OFF = "ASDA_NO_DEDUP_MAP"                                             # «1» — генераторы не перекодируют (аудит)
BACKUP_SET = "item-dedup"                                                 # имя набора бэкапа
BOM = b"\xef\xbb\xbf"                                                     # UTF-8 BOM

_CACHE = {}                                                               # путь -> (mtime, размер, карта)
CODE_NUM = re.compile(r"^-?\d+$")                                         # строка-ячейка — целое число


# ════════════════════════════════════════ КАРТА ════════════════════════════════════════════════
def load_map(path=None):
    """Карта {дубль: основной} (int → int) из JSON; цепочки развёрнуты. Кэш сбрасывается при изменении файла."""
    p = os.path.abspath(path or MAP_PATH)
    st = os.stat(p)
    hit = _CACHE.get(p)
    if hit and hit[0] == st.st_mtime and hit[1] == st.st_size:
        return hit[2]
    with open(p, "r", encoding="utf-8") as f:
        raw = json.load(f)["map"]
    m = {int(k): int(v) for k, v in raw.items()}
    out = {}
    for k in m:
        seen, v = {k}, m[k]
        while v in m:                                                     # цепочка: основной сам оказался дублем
            if v in seen:
                raise ValueError("item_dedup_map: цикл через код %d" % v)
            seen.add(v)
            v = m[v]
        if v == k:
            raise ValueError("item_dedup_map: код %d указывает сам на себя" % k)
        out[k] = v
    _CACHE[p] = (st.st_mtime, st.st_size, out)
    return out


def enabled():
    """Перекодировка в генераторах включена (по умолчанию да; ASDA_NO_DEDUP_MAP=1 — выключена)."""
    return os.environ.get(ENV_OFF, "").strip().lower() not in ("1", "true", "yes", "on")


def active_map():
    """Карта для генераторов: пустая, если перекодировка выключена или файла карты нет."""
    if not enabled() or not os.path.isfile(MAP_PATH):
        return {}
    return load_map()


def remap(code, m=None):
    """Код → основной код; не-код (не целое, bool, −1, 0 …) и не-дубль возвращаются как есть (тот же тип)."""
    if m is None:
        m = active_map()
    if isinstance(code, bool):
        return code
    if isinstance(code, str) and not CODE_NUM.match(code):
        return code                                                       # « 123», «1.0», «(1)» — не трогаем
    try:
        c = int(code)
    except (TypeError, ValueError):
        return code
    if c not in m:
        return code
    return m[c] if not isinstance(code, str) else str(m[c])


def remap_list(seq, m=None):
    """Список кодов → список с основными кодами (новый список)."""
    if m is None:
        m = active_map()
    return [remap(v, m) for v in seq]


def remap_keys(d, keys, m=None):
    """Перекодировать поля-коды словаря `d` на месте (скаляры и списки). Возврат — число заменённых значений."""
    if m is None:
        m = active_map()
    n = 0
    for k in keys:
        if k not in d:
            continue
        v = d[k]
        if isinstance(v, list):
            nv = remap_list(v, m)
            n += sum(1 for a, b in zip(v, nv) if a != b)
            d[k] = nv
        else:
            nv = remap(v, m)
            if nv != v:
                n += 1
                d[k] = nv
    return n


# ═══════════════════════════════ NSR (страницы диалогов NPC) ═══════════════════════════════════
# Поля-коды предметов в NSR — те же, что разбирает индекс ссылок (`_work/refs_client/collect_refs_client.py`, _nsr_walk):
#   Cond.ObjIds[5] (предмет цели), Cond.ReqItemIds[5] (требуемый), Cond.HaveItemId (имеющийся);
#   Objs[k].A52/A60/A68/A76/A84 (награда, виды 8…12: `itemSlotSetupEquipSowel@0xD7F130`), Objs[k].S112/S120/S128 (выборная
#   награда, виды 10…12: `questRewardSlotParse@0xD81460`), Objs[k].Need136 (порог цели `questNpc_IsTargetAvailable`).
NSR_COND_FIELDS = ("ObjIds", "ReqItemIds", "HaveItemId")
NSR_OBJ_FIELDS = ("A52", "A60", "A68", "A76", "A84", "S112", "S120", "S128", "Need136")
NSR_SKIP_KEYS = ("set", "was")      # ведомости правок условий: «было/стало» относятся к сырью — их не трогаем


def remap_nsr(o, m=None):
    """Рекурсивно по дереву NSR (страницы/кнопки/ведомости): перекодировать Cond.* и Objs.*. Возврат — число замен."""
    if m is None:
        m = active_map()
    n = 0
    if isinstance(o, dict):
        if isinstance(o.get("Cond"), dict):
            n += remap_keys(o["Cond"], NSR_COND_FIELDS, m)
        if isinstance(o.get("Objs"), list):
            for ob in o["Objs"]:
                if isinstance(ob, dict):
                    n += remap_keys(ob, NSR_OBJ_FIELDS, m)
        for k, v in o.items():
            if k in ("Cond", "Objs") or k in NSR_SKIP_KEYS:
                continue
            n += remap_nsr(v, m)
    elif isinstance(o, list):
        for v in o:
            n += remap_nsr(v, m)
    return n


# ═══════════════════════════════ ItemDB: колонки-коды таблиц ═══════════════════════════════════
# Обоснование списка — индекс ссылок этапа 1 (`Reports/item_dedup/_work/refs_client/coverage.tsv`, ITEMDB_CODECOLS
#   сборщика): только эти колонки несут коды ЧУЖИХ предметов. Колонки-ключи собственной строки (ItemCode у item_list,
#   pricetable, item_tooltip, decompositiontable, item_upgrade_cost; Code у special_itemlist/packagelist; Id у sowell_*)
#   НЕ трогаются. packagetable Item5/Qty5/Reserved52 — цена/скидка/плашка витрины, НЕ коды.
# Форма: таблица -> [(колонка CSV или None, смещение i32 в записи без MD5-префикса)]. Колонка None — поле живёт только в
#   DataHex (event_item_list: +68 i32[5] требуется, +108 i32[5] выдаётся; сверено с серверной EventItem_Table.txt).
#   Смещения декодер сверяет со своим реестром (защита от расхождения раскладок).
ITEMDB_CODE_FIELDS = {
    "booster_list": [("Slot%02dItemCode" % k, 4 + 8 * k) for k in range(35)],       # барабан бустера (Weight — не код)
    "packagetable": [("Item%d" % k, 4 + 8 * k) for k in range(5)],                   # состав коробки (Item5 — цена!)
    "premium_gatya": [("ItemCode", 8)],                                               # пул премиум-гачи
    "event_item_list": [(None, 68 + 4 * j) for j in range(5)] + [(None, 108 + 4 * j) for j in range(5)],  # обмен у NPC
    "charequiptable": [("ItemCode", 0), ("PrevCode", 12)],                           # стартовая экипировка
    "decompositiontable": [("Result%dCode" % g, 8 + 16 * g) for g in range(6)],      # результаты разборки
    "making_recipe": [("ResultCode", 72)] + [("Mat%d" % k, 76 + 4 * k) for k in range(6)],  # крафт: результат и сырьё
    "making_rate": [("OutItem%d" % k, 8 + 4 * k) for k in range(7)],                 # исходы ступеней крафта
    "refinehigh": [("Mat1Code", 24), ("Mat2Code", 36), ("Mat3Code", 48)],             # материалы высокой ковки
}
# Таблицы, у которых декодер пишет DataHex (запись целиком): код в колонке продублирован в DataHex по тому же смещению.
ITEMDB_DUMP_HEX = ("booster_list", "event_item_list")


def itemdb_code_fields(table):
    """[(колонка|None, смещение)] таблицы ItemDB (имя без .bin, пробелы → '_'); пусто — у таблицы нет полей-ссылок."""
    return ITEMDB_CODE_FIELDS.get(table, [])


def _hex_i32(h, off):
    """i32 little-endian из hex-строки записи по смещению байта (None — запись короче)."""
    a = 2 * off
    if len(h) < a + 8:
        return None
    return struct.unpack("<i", bytes.fromhex(h[a:a + 8]))[0]


def _hex_put_i32(h, off, v):
    """Hex-строка записи с заменой i32 по смещению (регистр hex сохраняется как у исходной строки)."""
    a = 2 * off
    new = struct.pack("<i", v).hex()
    if h[a:a + 8] != h[a:a + 8].lower():
        new = new.upper()
    return h[:a] + new + h[a + 8:]


def remap_itemdb_row(table, row, m=None, hex_key="DataHex"):
    """Перекодировать строку таблицы ItemDB на месте. `row` — dict (значения int или str, как в CSV).
    Код в колонке и его копия в DataHex (у ITEMDB_DUMP_HEX) меняются вместе; поля только-DataHex — по смещению.
    Возврат — список замен [(поле, было, стало)]; поле = имя колонки или «DataHex+<смещение>»."""
    if m is None:
        m = active_map()
    out = []
    fields = itemdb_code_fields(table)
    has_hex = table in ITEMDB_DUMP_HEX and isinstance(row.get(hex_key), str)
    for col, off in fields:
        if col is not None:
            if col not in row:
                continue
            v = row[col]
            nv = remap(v, m)
            if nv == v:
                continue
            row[col] = nv
            out.append((col, int(v), int(nv)))
            if has_hex and _hex_i32(row[hex_key], off) == int(v):
                row[hex_key] = _hex_put_i32(row[hex_key], off, int(nv))
        elif has_hex:
            v = _hex_i32(row[hex_key], off)
            if v is None or v not in m:
                continue
            row[hex_key] = _hex_put_i32(row[hex_key], off, m[v])
            out.append(("%s+%d" % (hex_key, off), v, m[v]))
    return out


# ═══════════════════════════ ТОЧЕЧНАЯ правка файлов (байты сохраняются) ═════════════════════════
def csv_records(text):
    """Сырые записи CSV: [[(начало, конец) поля в text]] — кавычки, «""», переводы строк внутри кавычек учитываются.
    Разметка нужна, чтобы менять значение ячейки, не трогая ни одного другого байта файла."""
    recs, i, n = [], 0, len(text)
    while i < n:
        fields = []
        while True:
            s = i
            if i < n and text[i] == '"':
                i += 1
                while True:
                    j = text.find('"', i)
                    if j < 0:
                        raise ValueError("CSV: незакрытая кавычка с позиции %d" % s)
                    if j + 1 < n and text[j + 1] == '"':
                        i = j + 2
                        continue
                    i = j + 1
                    break
                if i < n and text[i] not in ',\r\n':
                    raise ValueError("CSV: мусор после кавычки на позиции %d" % i)
            else:
                while i < n and text[i] not in ',\r\n':
                    i += 1
            fields.append((s, i))
            if i < n and text[i] == ",":
                i += 1
                continue
            break
        if i < n and text[i] == "\r":
            i += 1
        if i < n and text[i] == "\n":
            i += 1
        recs.append(fields)
    return recs


def _csv_cell(raw):
    """Значение ячейки из сырого текста поля."""
    if raw.startswith('"'):
        return raw[1:-1].replace('""', '"')
    return raw


def _csv_raw(old_raw, value):
    """Сырой текст ячейки с новым значением в той же форме (в кавычках, если исходная была в кавычках)."""
    if old_raw.startswith('"'):
        return '"' + value.replace('"', '""') + '"'
    if any(c in value for c in ',"\r\n'):
        raise ValueError("CSV: значение %r требует кавычек, а исходная ячейка без них" % value)
    return value


def patch_csv_bytes(data, row_fn):
    """Точечная правка CSV. row_fn(номер_записи_данных, {колонка: значение}) -> {колонка: новое значение} или None.
    Возврат (новые байты, [(запись, колонка, было, стало)]). Меняются только сырые тексты изменённых ячеек."""
    bom = data.startswith(BOM)
    text = data[len(BOM):].decode("utf-8") if bom else data.decode("utf-8")
    recs = csv_records(text)
    if not recs:
        return data, []
    hdr = [_csv_cell(text[a:b]) for a, b in recs[0]]
    edits, log = [], []
    for ri, rec in enumerate(recs[1:]):
        if len(rec) == 1 and rec[0][0] == rec[0][1]:
            continue                                                        # пустая строка
        vals = {h: _csv_cell(text[a:b]) for h, (a, b) in zip(hdr, rec)}
        new = row_fn(ri, dict(vals))
        if not new:
            continue
        for col, nv in new.items():
            if nv == vals[col]:
                continue
            a, b = rec[hdr.index(col)]
            edits.append((a, b, _csv_raw(text[a:b], nv)))
            log.append((ri, col, vals[col], nv))
    for a, b, s in sorted(edits, reverse=True):
        text = text[:a] + s + text[b:]
    out = text.encode("utf-8")
    return (BOM + out if bom else out), log


def json_format_of(data):
    """Параметры json.dumps, при которых файл воспроизводится ПОБАЙТОВО -> (объект, формат). Не нашлось — ValueError
    (тогда файл не правится: иначе изменились бы байты вне правки)."""
    bom = data.startswith(BOM)
    text = (data[len(BOM):] if bom else data).decode("utf-8")
    crlf = "\r\n" in text
    norm = text.replace("\r\n", "\n") if crlf else text
    obj = json.loads(norm)
    for indent in (0, 1, 2, 4, None, "\t"):
        for ea in (False, True):
            s = json.dumps(obj, ensure_ascii=ea, indent=indent)
            for tail in ("", "\n"):
                if s + tail == norm:
                    return obj, dict(indent=indent, ensure_ascii=ea, tail=tail, crlf=crlf, bom=bom)
    raise ValueError("JSON не воспроизводится json.dumps побайтово — точечная правка невозможна")


def json_bytes(obj, fmt):
    """Сериализация объекта в том же формате, что исходный файл (json_format_of)."""
    s = json.dumps(obj, ensure_ascii=fmt["ensure_ascii"], indent=fmt["indent"]) + fmt["tail"]
    if fmt["crlf"]:
        s = s.replace("\n", "\r\n")
    b = s.encode("utf-8")
    return BOM + b if fmt["bom"] else b


def patch_json_bytes(data, mutate):
    """Точечная правка JSON: mutate(объект) -> число замен. Возврат (новые байты, число замен)."""
    obj, fmt = json_format_of(data)
    n = mutate(obj)
    if not n:
        return data, 0
    return json_bytes(obj, fmt), n


# ═══════════════════════════════════ ЦЕЛИ ПРАВКИ ═══════════════════════════════════════════════
RAW = os.path.join(PROJECT, "RawAssets")
ITEMDB_DIR = os.path.join(RAW, "Data", "ItemDB")                          # боевые CSV/JSON ItemDB
LEDGER_DIR = os.path.join(RAW, "ItemDB")                                  # ведомости декодера


def _itemdb_csv_fn(table, m, log_rows):
    """row_fn для боевого CSV / ведомости хвоста ItemDB."""
    def fn(ri, vals):
        row = dict(vals)
        ch = remap_itemdb_row(table, row, m)
        if not ch:
            return None
        log_rows.extend((ri,) + c for c in ch)
        return {k: row[k] for k in vals if row[k] != vals[k]}
    return fn


def plan_itemdb_csv(path, table, m):
    """Боевой CSV или хвост-ведомость ItemDB. -> (новые байты, [замены])."""
    data = open(path, "rb").read()
    ch = []
    out, _cells = patch_csv_bytes(data, _itemdb_csv_fn(table, m, ch))
    return out, ch


def plan_itemdb_json(path, table, m):
    """JSON-двойник CSV декодера (тот же набор строк; ключ строки — Name = _row)."""
    data = open(path, "rb").read()
    ch = []

    def mutate(obj):
        n = 0
        for r in obj:
            c = remap_itemdb_row(table, r, m)
            ch.extend((r.get("Name"),) + x for x in c)
            n += len(c)
        return n
    out, _n = patch_json_bytes(data, mutate)
    return out, ch


def plan_value_overlay(path, table, m):
    """Ведомость значений ItemDB (row,column,was,now,…): «now» кодовой колонки = дубль -> основной. «was» не трогается —
    это значение архива, по нему декодер проверяет, что источник не сменился."""
    cols = {c for c, _o in itemdb_code_fields(table) if c}
    data = open(path, "rb").read()
    ch = []

    def fn(ri, v):
        if "column" in v and v["column"] in cols and CODE_NUM.match(v.get("now", "")):
            nv = remap(v["now"], m)
            if nv != v["now"]:
                ch.append((ri, v["column"] + " (now)", int(v["now"]), int(nv)))
                return {"now": nv}
        # event_item_list: формат импортёра asda_import_npc_tables (ItemIds через пробел; was_ItemIds не трогаем)
        if "ItemIds" in v and "was_ItemIds" in v:
            toks = v["ItemIds"].split(" ")
            nt = [remap(t, m) if CODE_NUM.match(t) else t for t in toks]
            if nt != toks:
                for a, b in zip(toks, nt):
                    if a != b:
                        ch.append((ri, "ItemIds", int(a), int(b)))
                return {"ItemIds": " ".join(nt)}
        return None
    out, _cells = patch_csv_bytes(data, fn)
    return out, ch


def plan_eventitem_snapshot(path, m):
    """Снимок серверной EventItem_Table.txt (cp949): число строк, затем строка = idx, имя, 10 пар (код, кол-во) — 0…4
    требуется, 5…9 выдаётся (разбор 1:1 с GameServer.c:69481-69523). Меняется только текст токена-кода (чистое целое)."""
    data = open(path, "rb").read()
    toks = [(mt.start(), mt.end()) for mt in re.finditer(rb"\S+", data)]
    ch, edits = [], []
    if not toks:
        return data, ch
    try:
        n = int(data[toks[0][0]:toks[0][1]])
    except ValueError:
        return data, ch
    p = 1
    for i in range(n):
        if p + 22 > len(toks):
            break
        for j in range(10):
            a, b = toks[p + 2 + 2 * j]
            t = data[a:b]
            if re.match(rb"^-?\d+$", t) and int(t) in m:
                edits.append((a, b, str(m[int(t)]).encode("ascii")))
                ch.append(("idx=%d" % i, "пара %d" % j, int(t), m[int(t)]))
        p += 22
    for a, b, s in sorted(edits, reverse=True):
        data = data[:a] + s + data[b:]
    return data, ch


def _json_target(keys_fn):
    """Цель-JSON: keys_fn(объект, карта, список_замен) -> число замен."""
    def plan(path, m):
        data = open(path, "rb").read()
        ch = []
        out, _n = patch_json_bytes(data, lambda obj: keys_fn(obj, m, ch))
        return out, ch
    return plan


def _rows_keys(keys):
    """JSON = список строк; поля `keys` — коды (скаляр или список)."""
    def fn(obj, m, ch):
        n = 0
        for i, r in enumerate(obj):
            before = {k: (list(r[k]) if isinstance(r.get(k), list) else r.get(k)) for k in keys}
            k_n = remap_keys(r, keys, m)
            if k_n:
                for k in keys:
                    a, b = before[k], r.get(k)
                    if isinstance(a, list):
                        ch.extend((r.get("Name", i), "%s[%d]" % (k, j), x, y) for j, (x, y) in enumerate(zip(a, b)) if x != y)
                    elif a != b:
                        ch.append((r.get("Name", i), k, a, b))
            n += k_n
        return n
    return fn


def _premium_shop(obj, m, ch):
    """PremiumShopBin.json: ItemId и его копия DataHex+0 (запись PremiumShop.BIN 68 б, +0 i32 код)."""
    n = 0
    for r in obj:
        v = r.get("ItemId")
        nv = remap(v, m)
        if nv == v:
            continue
        r["ItemId"] = nv
        if isinstance(r.get("DataHex"), str) and _hex_i32(r["DataHex"], 0) == v:
            r["DataHex"] = _hex_put_i32(r["DataHex"], 0, nv)
        ch.append((r.get("Name"), "ItemId", v, nv))
        n += 1
    return n


def _nsr_target(obj, m, ch):
    """NpcNsrPage.json / nsr_turnin_pages.json: поля-коды NSR (см. NSR_*_FIELDS)."""
    import copy
    before = copy.deepcopy(obj)
    n = remap_nsr(obj, m)
    if n:
        _diff_leaves(before, obj, "", ch)
    return n


def _diff_leaves(a, b, path, ch):
    """Различающиеся листья двух деревьев JSON одинаковой формы -> ch [(путь, было, стало)]."""
    if isinstance(a, dict):
        for k in a:
            _diff_leaves(a[k], b[k], "%s.%s" % (path, k) if path else k, ch)
    elif isinstance(a, list):
        for i, (x, y) in enumerate(zip(a, b)):
            _diff_leaves(x, y, "%s[%d]" % (path, i), ch)
    elif a != b:
        ch.append((path, "", a, b))


FISH_HDR, FISH_REC = 20, 348                                              # Fish_collection_data.BIN: шапка, запись
FISH_CODE_OFFS = [76 + 4 * j for j in range(30)] + [316 + 4 * j for j in range(4)]   # рыбы[30] и награды[4]


def plan_fish_bin(path, m):
    """Fish_collection_data.BIN (`CFishCollection_LoadPictureBookData@0xFD5FD0`): +76 i32[30] рыбы, +316 i32[4] награды.
    Книга-предмет тома (+4) — собственный предмет тома, не трогается (роль keyed индекса)."""
    data = bytearray(open(path, "rb").read())
    n = min(struct.unpack_from("<i", data, 4)[0], (len(data) - FISH_HDR) // FISH_REC)
    ch = []
    for i in range(n):
        base = FISH_HDR + FISH_REC * i
        for off in FISH_CODE_OFFS:
            v = struct.unpack_from("<i", data, base + off)[0]
            if v in m:
                struct.pack_into("<i", data, base + off, m[v])
                ch.append(("rec=%d" % i, "+%d" % off, v, m[v]))
    return bytes(data), ch


FISHBOOK_CODE_COLS = ("ClientFish", "ClientReward", "ServerFish", "ServerReward")   # FishBook.csv: кортежи «(a,b,…)»


def plan_fishbook_csv(path, m):
    """FishBook.csv (asda_build_fishbook_dt.py): кортежи рыб и наград клиента и сервера; книги (BookItemId) не трогаются."""
    data = open(path, "rb").read()
    ch = []

    def fn(ri, v):
        new = {}
        for col in FISHBOOK_CODE_COLS:
            cell = v.get(col, "")
            inner = cell.strip()
            if not (inner.startswith("(") and inner.endswith(")")):
                continue
            toks = inner[1:-1].split(",")
            nt = [remap(t, m) if CODE_NUM.match(t) else t for t in toks]
            if nt != toks:
                for j, (a, b) in enumerate(zip(toks, nt)):
                    if a != b:
                        ch.append((v.get("Tome", ri), "%s[%d]" % (col, j), int(a), int(b)))
                new[col] = cell.replace(inner, "(" + ",".join(nt) + ")")
        return new or None
    out, _cells = patch_csv_bytes(data, fn)
    return out, ch


ROULETTE_HDR, ROULETTE_REC = 20, 340                                     # Soulmate_Roulette.bin: шапка, запись


def plan_roulette_bin(path, m):
    """Soulmate_Roulette.bin (asda_build_soulmate_roulette_dt.py): сектор s — тип +0x14+4s, значение +0x24+4s;
    значение — код предмета ТОЛЬКО при типе 2 (опыт/ОЗ-ОМ — числа, не коды). Источник для CSV и DT рулетки."""
    data = bytearray(open(path, "rb").read())
    n = min(struct.unpack_from("<i", data, 4)[0], (len(data) - ROULETTE_HDR) // ROULETTE_REC)
    ch = []
    for i in range(n):
        base = ROULETTE_HDR + ROULETTE_REC * i
        for s in range(4):
            if struct.unpack_from("<i", data, base + 0x14 + 4 * s)[0] != 2:
                continue
            v = struct.unpack_from("<i", data, base + 0x24 + 4 * s)[0]
            if v in m:
                struct.pack_into("<i", data, base + 0x24 + 4 * s, m[v])
                ch.append(("rec=%d" % i, "values[%d]" % s, v, m[v]))
    return bytes(data), ch


def plan_roulette_csv(path, m):
    """SoulmateRoulette.csv (выход asda_build_soulmate_roulette_dt.py): кортеж Values, где в Types стоит 2 (предмет)."""
    data = open(path, "rb").read()
    ch = []

    def fn(ri, v):
        types = v.get("Types", "").strip()
        vals = v.get("Values", "")
        inner = vals.strip()
        if not (types.startswith("(") and inner.startswith("(")):
            return None
        tt = types[1:-1].split(",")
        toks = inner[1:-1].split(",")
        nt = [remap(t, m) if (j < len(tt) and tt[j].strip() == "2" and CODE_NUM.match(t)) else t for j, t in enumerate(toks)]
        if nt == toks:
            return None
        for j, (a, b) in enumerate(zip(toks, nt)):
            if a != b:
                ch.append((v.get("RowId", ri), "Values[%d]" % j, int(a), int(b)))
        return {"Values": vals.replace(inner, "(" + ",".join(nt) + ")")}
    out, _cells = patch_csv_bytes(data, fn)
    return out, ch


def plan_mob_quest_drop(path, m):
    """DT_MobQuestDrop.csv (RE/tools/dump_monster_quest_items.py): колонка ItemId."""
    data = open(path, "rb").read()
    ch = []

    def fn(ri, v):
        c = v.get("ItemId", "")
        if CODE_NUM.match(c):
            nv = remap(c, m)
            if nv != c:
                ch.append((v.get("Name"), "ItemId", int(c), int(nv)))
                return {"ItemId": nv}
        return None
    out, _cells = patch_csv_bytes(data, fn)
    return out, ch


def targets():
    """Все цели точечной правки: [(ключ, путь, функция plan(путь, карта) -> (байты, замены))]."""
    t = []
    for table in ITEMDB_CODE_FIELDS:
        for ext, fn in ((".csv", plan_itemdb_csv), (".json", plan_itemdb_json)):
            p = os.path.join(ITEMDB_DIR, table + ext)
            if os.path.isfile(p):
                t.append(("ItemDB/%s%s" % (table, ext), p, (lambda f, tb: lambda path, m: f(path, tb, m))(fn, table)))
        p = os.path.join(LEDGER_DIR, "_tail_append", table + ".csv")
        if os.path.isfile(p):
            t.append(("_tail_append/%s.csv" % table, p, (lambda tb: lambda path, m: plan_itemdb_csv(path, tb, m))(table)))
        p = os.path.join(LEDGER_DIR, "_value_overlay", table + ".csv")
        if os.path.isfile(p):
            t.append(("_value_overlay/%s.csv" % table, p, (lambda tb: lambda path, m: plan_value_overlay(path, tb, m))(table)))
    t += [
        ("_server_snapshot/EventItem_Table.txt", os.path.join(LEDGER_DIR, "_server_snapshot", "EventItem_Table.txt"),
         plan_eventitem_snapshot),
        ("Quest/QuestTargetArea.json", os.path.join(RAW, "Data", "Quest", "QuestTargetArea.json"), _json_target(_rows_keys(("ItemId",)))),
        ("Quest/SmInfo.json", os.path.join(RAW, "Data", "Quest", "SmInfo.json"), _json_target(_rows_keys(("ReqItemIds",)))),
        ("NPC/PremiumShopBin.json", os.path.join(RAW, "Data", "NPC", "PremiumShopBin.json"), _json_target(_premium_shop)),
        ("NPC/NpcNsrPage.json", os.path.join(RAW, "Data", "NPC", "NpcNsrPage.json"), _json_target(_nsr_target)),
        ("NPC/QuestRewardTable.json", os.path.join(RAW, "Data", "NPC", "QuestRewardTable.json"),
         _json_target(_rows_keys(("ItemCode1", "ItemCode2", "ChoiceItemCode")))),
        ("quest/DT_MobQuestDrop.csv", os.path.join(RAW, "quest", "DT_MobQuestDrop.csv"), plan_mob_quest_drop),
        ("Data/FishBook.csv", os.path.join(RAW, "Data", "FishBook.csv"), plan_fishbook_csv),
        ("Data/Fish_collection_data.BIN", os.path.join(RAW, "Data", "Fish_collection_data.BIN"), plan_fish_bin),
        ("Tools/data/nsr_turnin_pages.json", os.path.join(HERE, "data", "nsr_turnin_pages.json"), _json_target(_nsr_target)),
        # ★ 01.11 (функционалки F1): обмены у NPC (требуемые предметы) и рулетка друзей (сектор-предмет)
        ("NPC/EventItem.json", os.path.join(RAW, "Data", "NPC", "EventItem.json"), _json_target(_rows_keys(("ItemIds",)))),
        ("Data/Soulmate_Roulette.bin", os.path.join(RAW, "Data", "Soulmate_Roulette.bin"), plan_roulette_bin),
        ("Data/SoulmateRoulette.csv", os.path.join(RAW, "Data", "SoulmateRoulette.csv"), plan_roulette_csv),
    ]
    return [x for x in t if os.path.isfile(x[1])]


def run(write=False, only=None, map_path=None, log=print, backup_date=None):
    """Пройти все цели: посчитать замены; write=True — записать (бэкап до записи). Возврат {ключ: [замены]}."""
    m = load_map(map_path)
    res = {}
    for key, path, plan in targets():
        if only and not any(o.lower() in key.lower() for o in only):
            continue
        new, ch = plan(path, m)
        res[key] = ch
        old = open(path, "rb").read()
        if new == old:
            log("  = %-44s замен 0" % key)
            continue
        log("  * %-44s замен %d" % (key, len(ch)))
        if write:
            sys.path.insert(0, HERE)
            import asda_headless_tool as _H
            bak = _H.backup_file(path, BACKUP_SET, date=backup_date, verify="size", log=log)
            if not bak or os.path.getsize(bak) <= 0:
                raise RuntimeError("бэкап %s не создан — запись запрещена" % path)
            with open(path, "wb") as f:
                f.write(new)
    log("итого замен: %d в %d файлах%s" % (sum(len(v) for v in res.values()), sum(1 for v in res.values() if v),
                                           "" if write else " (пробный прогон, ничего не записано)"))
    return res


def main(argv=None):
    import argparse
    for st in (sys.stdout, sys.stderr):
        try:
            st.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass
    ap = argparse.ArgumentParser(description="Перекодировка ссылок на предметы-дубли в боевых файлах порта")
    ap.add_argument("--apply", action="store_true", help="записать изменения (с бэкапом); без ключа — пробный прогон")
    ap.add_argument("--only", default="", help="подстроки ключей целей через запятую")
    ap.add_argument("--map", default=None, help="другой файл карты (по умолчанию Tools/data/item_dedup_map.json)")
    ap.add_argument("--backup-date", default=None, help="дата набора бэкапа ГГГГ-ММ-ДД (по умолчанию сегодня)")
    a = ap.parse_args(argv)
    only = [s.strip() for s in a.only.split(",") if s.strip()]
    run(write=a.apply, only=only or None, map_path=a.map, backup_date=a.backup_date)
    return 0


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__":
    sys.exit(main())
