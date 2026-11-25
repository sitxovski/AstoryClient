# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_war_shop.py — ВОЕННЫЙ МАГАЗИН (окно HONOR_itemmall_Win 67200): два сырых файла
#   оригинала → две игровые таблицы.
#
#     Data\Honor_price_table.bin   → /Game/AsdaAssets/Data/Character/DT_WarShopList (140 строк)
#     Data\CampWar_HonorTable.BIN  → /Game/AsdaAssets/Data/Character/DT_WarRank     ( 21 строка)
#
# ══════════════════ ФОРМАТ `Honor_price_table.bin` (реверс загрузчика, НЕ догадка) ═══════════════
# `HonorPriceTable_Load@0xB76BB0`: заголовок 20 байт, записи по 48 байт подряд.
#   Заголовок: +0 u32 (в нашем файле 0) | +4 i32 ЧИСЛО ЗАПИСЕЙ | +8 u32 (12) | +12 магия "HPT\0"
#              | +16 u32 (0). Размер = 20 + 48*count и сходится побайтово (6740 = 20 + 48*140).
#   Запись — 12 × i32, порядок полей 1:1 как их читает загрузчик:
#     +0  StorePos     — индекс строки; ИМЕННО он уходит в пакет покупки 6162
#     +4  ItemCode     — код товара
#     +8  Flag8        — 0/1/10; читателей в срезе клиента нет
#     +12 PriceType0   — тип колонки цены 0 (в данных всегда 1 = «предмет»)
#     +16 PriceType1   — тип колонки цены 1 (в данных всегда 2 = «звание»)
#     +20 PriceCode0   — код валюты колонки 0 (у ВСЕХ 140 строк это 20628 «Монеты славы»)
#     +24 PriceCode1   — код валюты колонки 1 (в данных всегда −1: у «звания» кода нет)
#     +28 PriceQty0    — сколько монет стоит товар
#     +32 PriceQty1    — ТРЕБУЕМОЕ ВОИНСКОЕ ЗВАНИЕ (номер записи CampWar_HonorTable)
#     +36 Category     — 0…3, фильтр переключателя 67202 (Воин/Лучник/Маг/Обычный)
#     +40 VictorOnly   — 1 = товар только для победившей армии (значок 67401+i)
#     +44 RegionFilter — фильтр города: −1 любой, иначе 0/1/2
#
# ══════════════════ ФОРМАТ `CampWar_HonorTable.BIN` ══════════════════════════════════════════════
# `CMap_LoadCampWarHonorTable@0xCA0CC0`: заголовок 20 байт (+0 float 100.0, +4 i32 ЧИСЛО ЗАПИСЕЙ,
#   +12 магия "HRT\0"), записи по 56 байт. Размер 1196 = 20 + 56*21 сходится побайтово.
#   ★ ЗАПИСЬ НА ДИСКЕ ОБФУСЦИРОВАНА, и загрузчик её РАСКОДИРУЕТ, оставляя в памяти 40 байт:
#       [0..15]  MD5 — 16 байт контрольной суммы, загрузчик их ОТБРАСЫВАЕТ;
#       +16 i32  → RankIndex     = значение − 43;
#       +20 i32  → HonorRequired = значение − 43;
#       +24 char[32] → Title, КАЖДЫЙ БАЙТ − 1 (в файле лежит "Qsjwbuf" → "Private").
#     Дальше клиент берёт имя как `запись+8` (`sub_935854` → `sub_CA1530@0xCA1530`).
#   ⚠ У записи 0 имя ПУСТОЕ (32 нуля), и после «−1» оно остаётся пустым — так в данных.
#     Именно поэтому `RankName(0)` у эталона печатает пустую строку.
#   В DataTable ложится УЖЕ РАСКОДИРОВАННОЕ тело — то, что эталон держит в памяти.
#   ★ 10.10.2026: поверх него — ведомость RU-названий `Tools/data/warrank_ru_overrides.json` (RU-сборка .bin английская;
#     источник русских имён — та же таблица клиента «Фантазиум»). EN-имена из ORIG — оверлей `DT_Loc_WarRank_en`.
#
# ══════════════════ ПРОВЕРКА ════════════════════════════════════════════════════════════════════
# `--verify` собирает из разобранных строк исходные байты ОБРАТНО (включая MD5 из файла, сдвиги
#   +43 и +1) и сверяет md5 с сырым файлом. Совпадение = разбор полный и без потерь.
#
# Запуск:  Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_import_war_shop.py
#   без UE: python asda_import_war_shop.py --json-only
#   сверка: python asda_import_war_shop.py --verify
import os as _os
import struct
import sys as _sys

_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_dt_common as _DT

# ── Источники: ОРИГИНАЛЬНЫЙ клиент, каталог Data (а не копии в Content — их там больше нет) ──
PRICE_BIN = _os.path.join(_P.DATA, "Honor_price_table.bin")
RANK_BIN = _os.path.join(_P.DATA, "CampWar_HonorTable.BIN")

OUT_DIR = _os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Data", "Character")
PRICE_JSON = _os.path.join(OUT_DIR, "WarShopList.json")
RANK_JSON = _os.path.join(OUT_DIR, "WarRank.json")
# ★ 10.10.2026: ведомость RU-названий званий (RU-сборка .bin английская; разбор — `Reports/owner_1010/esc_ranks.md`).
RANK_RU_LEDGER = _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "data", "warrank_ru_overrides.json")

PKG = "/Game/AsdaAssets/Data/Character"
PRICE_NAME = "DT_WarShopList"
RANK_NAME = "DT_WarRank"
PRICE_STRUCT = "/Script/AsdaReborn.AsdaWarShopRow"
RANK_STRUCT = "/Script/AsdaReborn.AsdaWarRankRow"

PRICE_REC = 48          # размер записи витрины на диске
RANK_REC = 56           # размер записи звания на диске (16 MD5 + 40 тела)
HDR = 20                # общий заголовок обоих файлов

# Имена 12 полей записи витрины в том же порядке, в каком они лежат в файле.
PRICE_FIELDS = ("StorePos", "ItemCode", "Flag8", "PriceType0", "PriceType1", "PriceCode0",
                "PriceCode1", "PriceQty0", "PriceQty1", "Category", "VictorOnly", "RegionFilter")


def _read(path):
    with open(path, "rb") as f:
        return f.read()


def _header_count(raw, magic, rec, path):
    """Число записей из заголовка + проверка магии и размера. None — файл не тот."""
    if len(raw) < HDR:
        return None
    if raw[12:15] != magic:
        return None
    count = struct.unpack_from("<i", raw, 4)[0]
    if count < 0 or HDR + rec * count != len(raw):
        return None
    return count


def parse_price(raw):
    """`Honor_price_table.bin` → список словарей-строк витрины (12 × i32 на запись)."""
    count = _header_count(raw, b"HPT", PRICE_REC, PRICE_BIN)
    if count is None:
        return None
    rows = []
    for i in range(count):
        vals = struct.unpack_from("<12i", raw, HDR + PRICE_REC * i)
        rows.append(dict(zip(PRICE_FIELDS, vals)))
    return rows


def parse_rank(raw):
    """`CampWar_HonorTable.BIN` → список раскодированных званий (как их держит эталон в памяти)."""
    count = _header_count(raw, b"HRT", RANK_REC, RANK_BIN)
    if count is None:
        return None
    rows = []
    for i in range(count):
        body = HDR + RANK_REC * i + 16          # 16 байт MD5 загрузчик отбрасывает
        a, b = struct.unpack_from("<ii", raw, body)
        name = raw[body + 8:body + 40]
        # Побайтовый сдвиг −1; нулевой байт (и 0x01, дающий после сдвига 0) обрывает строку.
        # ★ дедуп №2 В2 (хвост Т5 волны В1): НЕ заменять на `asda_bintable.decipher_field(TERM_RU)` — здесь 1:1 с
        #   оригиналом, а не с шифром item-таблиц. Эталон `CMap_LoadCampWarHonorTable@0xCA0CC0`
        #   (`RE/etalon/world_map.c:5492-5497`): nameLen = strlen(поле) — сырой 0x00 = КОНЕЦ, затем каждый байт −1, и
        #   сырой 0x01 становится 0x00 (тоже конец при показе). Закон item-string-cipher-terminator («0x00 = я») относится
        #   к `dec_str` таблиц ItemDB; у этой таблицы «я» (сырой 0x00) оригинал обрезает сам. Проверено 11.10.2026.
        out = bytearray()
        for ch in name:
            if ch == 0:
                break
            d = (ch - 1) & 0xFF
            if d == 0:
                break
            out.append(d)
        rows.append({
            "RankIndex": a - 43,
            "HonorRequired": b - 43,
            # cp1251: в RU-сборке весь текст .bin лежит в ней; у званий байты чисто ASCII.
            "Title": out.decode("cp1251"),
        })
    return rows


# ═══════════════════════════ ОБРАТНАЯ СБОРКА (сверка разбора) ═══════════════════════════════════

def rebuild_price(raw, rows):
    """Собрать байты `Honor_price_table.bin` обратно из разобранных строк."""
    out = bytearray(raw[:HDR])
    for r in rows:
        out += struct.pack("<12i", *(r[k] for k in PRICE_FIELDS))
    return bytes(out)


def rebuild_rank(raw, rows):
    """Собрать байты `CampWar_HonorTable.BIN` обратно (MD5 берём из исходника — он не в таблице)."""
    out = bytearray(raw[:HDR])
    for i, r in enumerate(rows):
        src = HDR + RANK_REC * i
        out += raw[src:src + 16]                                    # MD5 1:1
        out += struct.pack("<ii", r["RankIndex"] + 43, r["HonorRequired"] + 43)
        name = r["Title"].encode("cp1251")
        enc = bytes(((c + 1) & 0xFF) for c in name)                 # обратный сдвиг +1
        # Хвост записи в исходнике не всегда нули — берём его как есть, чтобы сверка была честной.
        tail = raw[src + 24 + len(enc):src + RANK_REC]
        out += enc + tail
    return bytes(out)


def verify(log):
    """Сверка «разобрал → собрал обратно → побайтово равно исходнику» для обоих файлов."""
    import hashlib
    ok = True
    for path, parse, rebuild, tag in ((PRICE_BIN, parse_price, rebuild_price, "витрина"),
                                      (RANK_BIN, parse_rank, rebuild_rank, "звания")):
        raw = _read(path)
        rows = parse(raw)
        if rows is None:
            log("!! %s: файл не бьётся с заголовком — %s" % (tag, path))
            ok = False
            continue
        back = rebuild(raw, rows)
        same = hashlib.md5(back).hexdigest() == hashlib.md5(raw).hexdigest()
        log("%s: строк %d, обратная сборка %s (%d б)"
            % (tag, len(rows), "СОВПАЛА побайтово" if same else "РАСХОДИТСЯ", len(raw)))
        ok = ok and same
    return ok


def apply_rank_ru_ledger(rows, log):
    """★ 10.10.2026: наложить ведомость RU-названий званий (`Tools/data/warrank_ru_overrides.json`) на разобранные строки.
    Запись — только если имя из .bin = `was` (или уже = `value`); иначе исключение (файл сменился — разбирать заново).
    `--verify` её НЕ зовёт: обратная сборка сверяет байты исходника, а не русские имена. Возврат: число заменённых."""
    import json
    if not _os.path.isfile(RANK_RU_LEDGER):
        log("ведомости RU-званий нет (%s) — имена как в .bin" % RANK_RU_LEDGER)
        return 0
    with open(RANK_RU_LEDGER, encoding="utf-8") as f:
        led = {int(r["key"]): r for r in json.load(f)["rows"]}
    n = 0
    for r in rows:
        e = led.get(r["RankIndex"])
        if e is None or r["Title"] == e["value"]:
            continue
        if r["Title"] != e["was"]:
            raise RuntimeError("звание %d: в .bin «%s» ≠ ведомостному прежнему «%s» — стоп" % (r["RankIndex"], r["Title"], e["was"]))
        r["Title"] = e["value"]
        n += 1
    log("ведомость RU-званий: заменено %d из %d" % (n, len(led)))
    return n


def main(unreal=None):
    def log(m):
        if unreal:
            unreal.log_warning("[военный магазин] %s" % m)
        else:
            print("[военный магазин] %s" % m)

    price = parse_price(_read(PRICE_BIN))
    rank = parse_rank(_read(RANK_BIN))
    if price is None or rank is None:
        log("!! исходники не разобраны: %s / %s" % (PRICE_BIN, RANK_BIN))
        return False
    log("витрина %d строк, званий %d" % (len(price), len(rank)))
    apply_rank_ru_ledger(rank, log)   # ★ 10.10.2026: русские названия званий (ведомость; EN — оверлей из ORIG)

    price_text = _DT.json_rows(price, lambda r: str(r["StorePos"]))
    rank_text = _DT.json_rows(rank, lambda r: str(r["RankIndex"]))
    # ★ 19.11.2026 (админ-центр А4.1, закон owner-asset-edits-never-lose): сторож ручной правки ОБОИХ DT — ДО записи JSON и DT
    if unreal is not None and not (asset_matches_previous_json(unreal, PRICE_NAME, PRICE_JSON, log)
                                   and asset_matches_previous_json(unreal, RANK_NAME, RANK_JSON, log)):
        log("ЗАВЕРШЕНО: импорт %s (СТОП: ручная правка)" % PRICE_NAME)
        return False
    _os.makedirs(OUT_DIR, exist_ok=True)
    for path, text in ((PRICE_JSON, price_text), (RANK_JSON, rank_text)):
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write(text)
        log("JSON: %s" % path)

    if unreal is None:
        return True
    a = _DT.fill_dt(unreal, PKG, PRICE_NAME, PRICE_STRUCT, price_text, log, fmt="json",
                    missing_hint="Public/UI/AsdaWarShopSubsystem.h, FAsdaWarShopRow")
    b = _DT.fill_dt(unreal, PKG, RANK_NAME, RANK_STRUCT, rank_text, log, fmt="json",
                    missing_hint="Public/UI/AsdaWarShopSubsystem.h, FAsdaWarRankRow")
    ok = a is not None and b is not None
    log("ЗАВЕРШЕНО: импорт %s (%s)" % (PRICE_NAME, "ok" if ok else "ПРОВАЛ"))   # ★ 19.11 А4.1: маркер ПОСЛЕ сохранения обоих DT
    return ok


def _norm_cell(v):
    """Ячейка для сравнения ассета с прежним JSON: числа по значению, bool и строки без регистра."""
    if isinstance(v, bool):
        return str(v).lower()
    try:
        return float(v)
    except (TypeError, ValueError):
        return str(v).strip().lower()


def asset_matches_previous_json(unreal, name, prev_json, log):
    """★ 19.11.2026 (админ-центр А4.1): сторож ручной правки — строки ассета PKG/name == прежний prev_json (до этого прогона) по
    всем общим полям. Нет ассета или прежнего JSON — сравнивать нечего (True). Образец — asda_import_premium_shop_bin."""
    import json
    dt = unreal.EditorAssetLibrary.load_asset(PKG + "/" + name)
    if not dt or not _os.path.isfile(prev_json):
        return True
    prev = {str(r.get("Name")): r for r in json.load(open(prev_json, encoding="utf-8"))}
    cur = {str(r["Name"]): r for r in json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(dt))}
    diff = sorted(k for k in set(prev) ^ set(cur))
    for k in set(prev) & set(cur):
        a, b = prev[k], cur[k]
        # ★ ревизия 33 К-3: ВСЕ поля, которые импортёр пишет; поля нет в экспорте ассета — тоже расхождение
        if any(f not in b or _norm_cell(a[f]) != _norm_cell(b[f]) for f in a if f != "Name"):
            diff.append(k)
    if diff:
        log("СТОП: %s расходится с прежним %s (ручная правка?) — строки %s; ассет не трогаю" % (name, prev_json, sorted(diff)[:10]))
        return False
    return True


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__":
    if "--verify" in _sys.argv:
        verify(lambda m: print("[военный магазин] %s" % m))
    elif "--json-only" in _sys.argv:
        main(None)
    else:
        import unreal as _u
        main(_u)
