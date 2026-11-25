# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_fishbook_dt.py — ТАБЛИЦА «КНИГИ РЫБАКА» (окно 74000) = DT_FishBook (волна В3 рыбалки, 08.10.2026).
#
# ЗАКОНЫ: client-server-table-desync (авторитет — СЕРВЕР), tables-merge-not-replace (ОБЪЕДИНЯТЬ по ключу, без потерь,
#   «потеряно записей: 0»), never-invent-only-original (только данные оригинала: клиентский .BIN и серверные .txt).
#
# ИСТОЧНИКИ (вещественное доказательство):
#   клиент  RawAssets/Data/Fish_collection_data.BIN — копия Donors\AsdaChroniclesInterlude_MINE\Data, md5 39e40093…,
#           3152 б = шапка 20 + 9 × 348. Загрузчик эталона `CFishCollection_LoadPictureBookData@0xFD5FD0`
#           (game.c:611035-611136): +0 i32 номер тома, +4 i32 id книги-предмета (подтип 210), +8 char[64] имя тома,
#           +72 i32 флаг (клиентом НЕ читается), +76 i32[30] рыбы (−1 пусто), +196 i32[30] требуемое количество,
#           +316 i32[4] награды, +332 i32[4] количества наград.
#   сервер  @@ASTORY_DEV_ROOT@@\Donors\GameServer_stock\Fishing_Info\Fishing_BookInfo.txt (md5 15cb08ae…, == живой, server.md §6.3):
#           первая строка — число строк; строка: idx, тип книги (0/1/2), 30 рыб, 30 требуемых, 4 награды, 4 количества
#           (загрузчик GS:74200-74420, `fscanf("%s")` + `atol`).
#   сервер  @@ASTORY_DEV_ROOT@@\Donors\GameServer_stock\ItemTable_Info\item_table.txt — книга-предмет тома: запись с колонкой 17
#           (ItemDB+27 «класс») == 210 и колонкой 8 (ItemDB+12 «номер книги») == номер тома (`Recv_FishingBookRegist
#           @0x411720` GS:7748-7759). У томов 5…8 такой записи НЕТ (69682…69685 на сервере — наживка 204).
#           ★ 09.10.2026 решение владельца: 69682…69685 — книги томов 5…8, как в клиенте; staging сервера —
#           `AstoryServer/Docs/fishbooks_69682` (до выкладки DT собирается с ASDA_FISHBOOK_SRV_ITEMS на staging).
#
# ОБЪЕДИНЕНИЕ (ключ — номер тома): строка DT хранит И клиентские, И серверные значения (ничего не теряется).
#   Выбор набора — в рантайме ключом `asda.FishBook.ClientTable` (0 = сервер, умолчание; 1 = клиент, как оригинал).
#   Имя тома есть ТОЛЬКО у клиента (у тома 5…8 в клиенте `????` — битая локализация; ★ 09.10.2026 п. 1 владельца: имена
#   5…8 заданы ВЕДОМОСТЬЮ `RawAssets/Data/_value_overlay/FishBook.csv` по схеме томов 1…4, см. `apply_overlay`).
#
# Выход: RawAssets/Data/FishBook.csv (ВНЕ Content!) + /Game/AsdaAssets/Data/DT_FishBook (RowStruct FAsdaFishBookRow,
#   Public/UI/AsdaFishBookSubsystem.h — ТРЕБУЕТ сборки C++ до запуска в редакторе).
# Запуск (в редакторе): python Tools/run_headless_safe.py Tools/asda_build_fishbook_dt.py --idle 200
# Запуск (только CSV и сверка): python Tools/asda_build_fishbook_dt.py
# ★ СВЕРКА: «записей: N (клиент C, сервер S)», «потеряно записей: 0» и таблица расхождений клиент/сервер.
# ★★ Ревизия В3, МЕЛК-7 (08.10.2026): «потеряно 0» считалось по тем же ключам, по которым строились строки (тавтология).
#   Теперь: (1) порядок клиента — утверждение `+0 (номер тома) == порядок в файле` (индекс эталона = порядок записи);
#   (2) ОБРАТНОЕ ЧТЕНИЕ записанного CSV и сверка КАЖДОГО поля КАЖДОЙ строки с исходниками (.BIN / BookInfo / item_table),
#   а не со строками сборки; (3) в редакторе — обратное чтение DT (`get_data_table_column_as_string`) и та же сверка.
#   Только сверка готовой DT без записи: ASDA_FISHBOOK_DT_VERIFY_ONLY=1 (ассет не трогается).
# ★★ W3fix2 (09.10.2026, ревизия `fishing_impl_W3fix_W6_REVIEW.md`, МЕЛК-6): «сверка с исходниками» выше была обратным
#   чтением CSV/DT против ТОГО ЖЕ разбора исходников (ошибку разбора она не ловит). Теперь исходники читаются ВТОРЫМ,
#   независимым способом (`parse_client2`/`parse_server2`/`parse_server_books2`: ctypes / поток токенов / csv по TAB),
#   оба способа сверяются поле в поле (`cross_check`), CSV и DT сверяются со ВТОРЫМ. Гейт записи: при любом расхождении
#   CSV не пишется, DT не сохраняется (сверка CSV в памяти ДО записи, DT — ДО `save_loaded_asset`).
# ★ ПРАВИЛО tools-no-import-side-effects: НИКАКИХ действий при импорте модуля — только через main().
import csv
import ctypes
import hashlib
import io
import os
import re
import struct

try:
    import unreal
except ImportError:      # штатный режим «только CSV» вне редактора
    unreal = None

import sys as _sys
_sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
# ★ дедуп №2 В1 Т5: ПЕРВЫЙ способ чтения .BIN (parse_client) — через общий `asda_bintable`; второй (parse_client2,
#   ctypes) остаётся независимым — это оракул сверки, его в общий код не выносим.
import asda_bintable as _BT     # noqa: E402
import asda_ue_paths as _U      # noqa: E402  ★ дедуп №2 В1 Т20: /Game — реестр путей

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))          # AstoryClient
DATA = os.path.join(ROOT, "RawAssets", "Data")                              # сырьё порта
BIN_BOOK = os.path.join(DATA, "Fish_collection_data.BIN")                   # клиентская таблица томов
SRV_DIR = os.path.normpath(os.path.join(ROOT, "..", "Donors", "GameServer_stock"))    # @@ASTORY_DEV_ROOT@@\Donors\GameServer_stock
SRV_BOOK = os.path.join(SRV_DIR, "Fishing_Info", "Fishing_BookInfo.txt")    # серверная таблица томов
SRV_ITEMS = os.path.join(SRV_DIR, "ItemTable_Info", "item_table.txt")       # серверная таблица предметов
# ★ 09.10.2026 (fishbooks_69682): другой файл item_table — переменной ASDA_FISHBOOK_SRV_ITEMS (например, staging-пакет
#   `AstoryServer/Docs/fishbooks_69682/GameServer/ItemTable_Info/item_table.txt`, где 69682…69685 — книги томов 5…8).
#   Пусто — локальная копия выше. Путь печатается в сверке («item_table md5 …»).
SRV_ITEMS = os.environ.get("ASDA_FISHBOOK_SRV_ITEMS", "").strip() or SRV_ITEMS   # итоговый путь item_table
CSV_OUT = os.path.join(DATA, "FishBook.csv")                                # CSV для DT
# ★ Решение владельца 09.10.2026 (п. 1): ВЕДОМОСТЬ ЗНАЧЕНИЙ (как `_value_overlay` ItemDB, itemdb-ledger-overlays) — правки
#   поверх клиентского .BIN, переживают перегенерацию. Формат: row (номер тома), column (поле разбора клиента), was (сырое
#   значение .BIN — если не совпало, сборка останавливается), now, source, reason. Накладывается на ОБА способа чтения.
OVERLAY = os.path.join(DATA, "_value_overlay", "FishBook.csv")               # ведомость правок значений клиента
OUT_PKG = _U.G_DATA                                                         # папка DT (реестр путей; дедуп №2 Т20)
DT_NAME = "DT_FishBook"                                                     # имя DT
ROW_STRUCT = "/Script/AsdaReborn.AsdaFishBookRow"                           # RowStruct
HDR = 20             # шапка .BIN (`fread(Buffer, 0x14u, 1u, Stream)`, число записей на +4)
REC = 348            # запись .BIN (`fread(rawData, 0x15Cu, count, Stream)`)
FISH_N = 30          # рыб в томе
REW_N = 4            # наград в томе
MD5_BIN = "39e400937a10e96776a42deba18c2b08"      # md5 клиентского .BIN (MINE)
# ★ 01.11.2026 (item_dedup, этап 2): в RawAssets-копии .BIN награды-дубли томов 1 и 5 (+316 Reward[1]: 76→21, 77→22)
#   перекодированы на основной код (`asda_item_dedup.py --apply`, бэкап `_backup/2026-11-01_item-dedup`) — md5 копии стал
#   другим; оба значения считаются «своими». Сборка всё равно перекодирует дубли сама (dedup_tomes) — копия из донора даст то же.
MD5_BIN_DEDUP = "843d3d7899100d23fa740ad899b25595"  # md5 копии .BIN после перекодировки item_dedup 01.11.2026
MD5_SRV_BOOK = "15cb08ae814a4333abc81936b5492197"  # md5 серверного BookInfo (== живой, server.md §6)
COL_ITEM_TOME = 8    # item_table: колонка ItemDB+12 (номер книги у предмета 210)
COL_ITEM_CLASS = 17  # item_table: колонка ItemDB+27 (класс: 210 книга, 204 наживка …)
CLASS_BOOK = 210     # класс книги-предмета


def log(m):
    """Печать: в редакторе — warning-лог (виден в headless), иначе stdout с заменой непечатного."""
    if unreal:
        unreal.log_warning("[fishbook-dt] %s" % m)
        return
    line = "[fishbook-dt] %s" % m
    enc = (getattr(__import__("sys").stdout, "encoding", None) or "utf-8")
    print(line.encode(enc, errors="replace").decode(enc, errors="replace"))


def md5(path):
    """md5 файла (hex) или '' при ошибке."""
    try:
        with open(path, "rb") as f:
            return hashlib.md5(f.read()).hexdigest()
    except OSError:
        return ""


def atol(tok):
    """`atol` сервера: ведущее целое числа (пробелы/хвост отбрасываются), нет числа → 0."""
    s = tok.strip()
    sign, i, v = 1, 0, 0
    if i < len(s) and s[i] in "+-":
        sign = -1 if s[i] == "-" else 1
        i += 1
    digits = False
    while i < len(s) and s[i].isdigit():
        v = v * 10 + int(s[i])
        i += 1
        digits = True
    return sign * v if digits else 0


def parse_client():
    """Fish_collection_data.BIN → {том: поля}. Пусто — файла нет/битый."""
    try:
        with open(BIN_BOOK, "rb") as f:
            d = f.read()
    except OSError as e:
        log("!! не прочитать %s: %s" % (BIN_BOOK, e))
        return {}, 0
    n_hdr = _BT.record_count(d)                                               # i32@4
    n_file = (len(d) - HDR) // REC
    if (len(d) - HDR) % REC:
        log("!! хвост .BIN %d б не кратен записи %d" % ((len(d) - HDR) % REC, REC))
    out = {}
    for i, rec in _BT.iter_records(d, REC, count=min(n_hdr, n_file), base=HDR):   # смещения — от начала записи
        tome, book = struct.unpack_from("<ii", rec, 0)
        name = _BT.cstr(rec, 8, 64, "cp1251", "replace")
        out[i] = {
            "Tome": tome,                                                     # +0 номер тома
            "Order": i,                                                       # порядок файла = индекс тома эталона
            "Name": name,                                                     # +8 имя тома (64 б)
            "BookItemId": book,                                               # +4 книга-предмет
            "Flag": struct.unpack_from("<i", rec, 72)[0],                     # +72 флаг (не читается)
            "Fish": list(struct.unpack_from("<%di" % FISH_N, rec, 76)),       # +76
            "Req": list(struct.unpack_from("<%di" % FISH_N, rec, 196)),       # +196
            "Reward": list(struct.unpack_from("<%di" % REW_N, rec, 316)),     # +316
            "RewardCnt": list(struct.unpack_from("<%di" % REW_N, rec, 332)),  # +332
        }
    return out, n_hdr


def dedup_tomes(tomes, what):
    """★ 01.11.2026 (item_dedup, этап 2): рыбы и награды томов (`Fish`, `Reward`) — код-дубль -> основной (общий модуль
    asda_item_dedup, карта Tools/data/item_dedup_map.json). Книга-предмет тома (BookItemId) — собственный предмет тома, не
    трогается. Применяется к ОБОИМ способам чтения клиента и сервера (иначе разойдутся сверки). ASDA_NO_DEDUP_MAP=1 — выкл."""
    import asda_item_dedup as _DD
    dmap = _DD.active_map()
    n = sum(_DD.remap_keys(t, ("Fish", "Reward"), dmap) for t in tomes.values())
    log("item_dedup (%s): перекодировано рыб/наград-дублей %d" % (what, n))
    return n


def apply_overlay(cli, what):
    """★ п. 1 (09.10.2026): наложить ведомость OVERLAY на разбор клиента `cli` ({порядок: поля}) на месте.
    Возврат — список ошибок (пусто — легла целиком): поле/том не найдены, сырое значение ≠ `was`. Нет файла — пусто."""
    if not os.path.exists(OVERLAY):
        return []
    bad, n = [], 0
    with open(OVERLAY, "r", encoding="utf-8-sig", newline="") as f:
        for r in csv.DictReader(f):
            k, col = int(r["row"]), r["column"]
            rec = cli.get(k)
            if rec is None or col not in rec:
                bad.append("том %d поле %s: нет в разборе клиента" % (k, col))
                continue
            if str(rec[col]) != r["was"]:
                bad.append("том %d %s: в .BIN «%s», ведомость ждёт «%s»" % (k, col, rec[col], r["was"]))
                continue
            rec[col] = r["now"]
            n += 1
    log("ведомость %s (%s): наложено %d, ошибок %d%s" % (os.path.basename(OVERLAY), what, n, len(bad),
                                                        ("" if not bad else ": " + "; ".join(bad))))
    return bad


def parse_server():
    """Fishing_BookInfo.txt → {том: поля}; число строк из шапки."""
    try:
        with open(SRV_BOOK, "r", encoding="cp1251", errors="replace") as f:
            toks_lines = [ln.split() for ln in f.read().splitlines() if ln.strip()]
    except OSError as e:
        log("!! не прочитать %s: %s" % (SRV_BOOK, e))
        return {}, 0
    n_hdr = atol(toks_lines[0][0]) if toks_lines else 0
    out = {}
    need = 2 + FISH_N * 2 + REW_N * 2
    for row in toks_lines[1:1 + n_hdr]:
        if len(row) < need:
            log("!! строка сервера короче %d токенов: %s" % (need, " ".join(row)))
            continue
        v = [atol(t) for t in row[:need]]
        idx = v[0]
        out[idx] = {
            "Tome": idx,                                       # +0 i16 номер (обязан == номеру строки)
            "Type": v[1],                                      # +2 u8 тип книги (0/1/2 → запись игрока +4)
            "Fish": v[2:2 + FISH_N],                           # +3 i32[30]
            "Req": v[2 + FISH_N:2 + 2 * FISH_N],               # +123 i16[30]
            "Reward": v[2 + 2 * FISH_N:2 + 2 * FISH_N + REW_N],                 # +183 i32[4]
            "RewardCnt": v[2 + 2 * FISH_N + REW_N:2 + 2 * FISH_N + 2 * REW_N],  # +199 i32[4]
        }
    return out, n_hdr


def parse_server_books():
    """item_table.txt → {номер тома: код книги-предмета} (класс 210). Несколько на том — берётся первый и предупреждение."""
    books = {}
    try:
        with open(SRV_ITEMS, "r", encoding="cp1251", errors="replace") as f:
            lines = f.read().splitlines()
    except OSError as e:
        log("!! не прочитать %s: %s" % (SRV_ITEMS, e))
        return books
    for ln in lines[1:]:
        t = ln.split()
        if len(t) <= COL_ITEM_CLASS:
            continue
        if atol(t[COL_ITEM_CLASS]) != CLASS_BOOK:
            continue
        code, tome = atol(t[0]), atol(t[COL_ITEM_TOME])
        if tome in books:
            log("!! у тома %d на сервере две книги-предмета: %d и %d — берётся первая" % (tome, books[tome], code))
            continue
        books[tome] = code
    return books


def arr(v):
    """TArray<int32> для CSV DataTable: «(a,b,c)»."""
    return '"(%s)"' % ",".join(str(x) for x in v)


def diff(name, a, b):
    """Строка расхождения поля клиент/сервер (пусто — совпадает)."""
    if a == b:
        return ""
    if isinstance(a, list):
        idx = [i for i in range(max(len(a), len(b))) if (a[i] if i < len(a) else None) != (b[i] if i < len(b) else None)]
        return "%s[%s]: клиент %s / сервер %s" % (name, ",".join(str(i) for i in idx),
                                                   [a[i] for i in idx if i < len(a)], [b[i] for i in idx if i < len(b)])
    return "%s: клиент %s / сервер %s" % (name, a, b)


def build():
    """Объединение по ключу «номер тома» → (строки CSV, ожидаемое число, вердикт)."""
    cli, cli_hdr = parse_client()
    bad_ov = apply_overlay(cli, "разбор 1")          # ★ п. 1: имена томов 5…8 (ведомость), «было» сверяется с .BIN
    srv, srv_hdr = parse_server()
    dedup_tomes(cli, "клиент, разбор 1")             # ★ 01.11.2026 item_dedup: после ведомости (её «было» — сырьё .BIN)
    dedup_tomes(srv, "сервер, разбор 1")
    books = parse_server_books()
    log("md5: клиент .BIN %s (%s), сервер BookInfo %s (%s), item_table %s"
        % (md5(BIN_BOOK), "совпал" if md5(BIN_BOOK) in (MD5_BIN, MD5_BIN_DEDUP) else "ИНОЙ",
           md5(SRV_BOOK), "совпал" if md5(SRV_BOOK) == MD5_SRV_BOOK else "ИНОЙ", md5(SRV_ITEMS)))
    log("клиент: записей %d (шапка %d); сервер: строк %d (шапка %d); книг-предметов 210 на сервере: %s"
        % (len(cli), cli_hdr, len(srv), srv_hdr, ", ".join("%d→%d" % kv for kv in sorted(books.items()))))
    keys = sorted(set(cli) | set(srv))
    rows, diffs = [], []
    for k in keys:
        c = cli.get(k)
        s = srv.get(k)
        empty_f, empty_r = [-1] * FISH_N, [-1] * REW_N
        r = {
            "Tome": k,
            "Name": (c["Name"] if c else ""),
            "ClientTome": (c["Tome"] if c else -1),
            "ClientBookItemId": (c["BookItemId"] if c else -1),
            "ServerBookItemId": books.get(k, -1),
            "ClientFlag": (c["Flag"] if c else 0),
            "ServerType": (s["Type"] if s else 0),
            "bClient": 1 if c else 0,
            "bServer": 1 if s else 0,
            "ClientFish": (c["Fish"] if c else empty_f), "ClientReq": (c["Req"] if c else empty_f),
            "ClientReward": (c["Reward"] if c else empty_r), "ClientRewardCnt": (c["RewardCnt"] if c else empty_r),
            "ServerFish": (s["Fish"] if s else empty_f), "ServerReq": (s["Req"] if s else empty_f),
            "ServerReward": (s["Reward"] if s else empty_r), "ServerRewardCnt": (s["RewardCnt"] if s else empty_r),
        }
        rows.append(r)
        d = [x for x in (
            "" if c else "НЕТ В КЛИЕНТЕ",
            "" if s else "НЕТ НА СЕРВЕРЕ",
            diff("книга-предмет", r["ClientBookItemId"], r["ServerBookItemId"]),
            diff("флаг/тип", r["ClientFlag"], r["ServerType"]),
            diff("рыбы", r["ClientFish"], r["ServerFish"]),
            diff("требуется", r["ClientReq"], r["ServerReq"]),
            diff("награды", r["ClientReward"], r["ServerReward"]),
            diff("кол-ва наград", r["ClientRewardCnt"], r["ServerRewardCnt"]),
        ) if x]
        diffs.append((k, r["Name"], d))
    expect = len(keys)
    lost = expect - len(rows)
    log("=== РАСХОЖДЕНИЯ клиент/сервер по томам (авторитет — сервер, ключ asda.FishBook.ClientTable 0) ===")
    for k, nm, d in diffs:
        log("том %d «%s»: %s" % (k, nm, "совпадает" if not d else "; ".join(d)))
    log("записей: %d (клиент %d, сервер %d), потеряно записей: %d" % (len(rows), len(cli), len(srv), lost))
    # ★ МЕЛК-7: индекс тома эталона = порядок записи в файле; номер +0 обязан с ним совпасть (иначе ключ «номер тома» врёт).
    order_bad = [(i, c["Tome"]) for i, c in sorted(cli.items()) if c["Tome"] != c["Order"]]
    log("клиент: номер тома +0 == порядок в файле — %s" % ("ДА у всех %d" % len(cli) if not order_bad else "НЕТ: %s" % order_bad))
    srv_bad = [k for k, v in srv.items() if v["Tome"] != k]
    ok = (not bad_ov and lost == 0 and len(cli) == cli_hdr and len(srv) == srv_hdr and set(cli) <= set(keys) and set(srv) <= set(keys)
          and not order_bad and not srv_bad)
    return rows, expect, ok, (cli, srv, books)


def to_csv(rows):
    """Строки → CSV DataTable (первая колонка — RowName = номер тома)."""
    cols = ["Tome", "Name", "ClientTome", "ClientBookItemId", "ServerBookItemId", "ClientFlag", "ServerType", "bClient",
            "bServer", "ClientFish", "ClientReq", "ClientReward", "ClientRewardCnt", "ServerFish", "ServerReq",
            "ServerReward", "ServerRewardCnt"]
    out = ["---," + ",".join(cols)]
    for r in rows:
        vals = []
        for c in cols:
            v = r[c]
            if isinstance(v, list):
                vals.append(arr(v))
            elif isinstance(v, str):
                vals.append('"%s"' % v.replace('"', '""'))
            elif c in ("bClient", "bServer"):
                vals.append("True" if v else "False")
            else:
                vals.append(str(v))
        out.append("%d,%s" % (r["Tome"], ",".join(vals)))
    return "\n".join(out) + "\n"


def expected_row(k, src):
    """Ожидаемые значения строки тома k ПРЯМО из исходников (не из строк сборки): {колонка: значение}."""
    cli, srv, books = src
    c, s = cli.get(k), srv.get(k)
    ef, er = [-1] * FISH_N, [-1] * REW_N
    return {
        "Tome": k, "Name": c["Name"] if c else "", "ClientTome": c["Tome"] if c else -1,
        "ClientBookItemId": c["BookItemId"] if c else -1, "ServerBookItemId": books.get(k, -1),
        "ClientFlag": c["Flag"] if c else 0, "ServerType": s["Type"] if s else 0,
        "bClient": bool(c), "bServer": bool(s),
        "ClientFish": c["Fish"] if c else ef, "ClientReq": c["Req"] if c else ef,
        "ClientReward": c["Reward"] if c else er, "ClientRewardCnt": c["RewardCnt"] if c else er,
        "ServerFish": s["Fish"] if s else ef, "ServerReq": s["Req"] if s else ef,
        "ServerReward": s["Reward"] if s else er, "ServerRewardCnt": s["RewardCnt"] if s else er,
    }


def norm(col, v):
    """Значение ячейки CSV/DT → значение Python для сверки (массив «(a,b)» → list, bool «True/False», число, строка)."""
    v = (v or "").strip()
    if col.startswith(("Client", "Server")) and col not in ("ClientTome", "ClientBookItemId", "ServerBookItemId",
                                                            "ClientFlag", "ServerType"):
        inner = v.strip("()")
        return [int(x) for x in inner.split(",")] if inner else []
    if col in ("bClient", "bServer"):
        return v.lower() == "true"
    if col == "Name":
        return v
    return int(v)


def verify_rows(get_cell, row_names, src, what):
    """Сверка КАЖДОГО поля КАЖДОЙ строки, прочитанной обратно (`get_cell(ключ, колонка)`), с исходниками.
    Возврат (число строк, список расхождений)."""
    cli, srv, _books = src
    keys = sorted(set(cli) | set(srv))
    bad = []
    if sorted(int(r) for r in row_names) != keys:
        bad.append("ключи строк %s ≠ тома исходников %s" % (sorted(row_names), keys))
    for k in keys:
        exp = expected_row(k, src)
        for col, want in exp.items():
            try:
                got = norm(col, get_cell(k, col))
            except Exception as e:                       # noqa: BLE001
                bad.append("том %d %s: не прочитать (%s)" % (k, col, e))
                continue
            if got != want:
                bad.append("том %d %s: %s ≠ исходник %s" % (k, col, got, want))
    log("%s: обратное чтение — строк %d, полей сверено %d, расхождений %d%s" % (
        what, len(row_names), len(keys) * 17, len(bad), ("" if not bad else ": " + "; ".join(bad[:8]))))
    return len(row_names), bad


def verify_csv(src):
    """Перечитать записанный CSV и сверить с исходниками."""
    with open(CSV_OUT, "r", encoding="utf-8-sig", newline="") as f:
        rd = list(csv.reader(f))
    hdr = rd[0]
    rows = {int(r[0]): dict(zip(hdr[1:], r[1:])) for r in rd[1:] if r}
    return verify_rows(lambda k, c: rows[k][c], list(rows), src, "CSV %s" % os.path.basename(CSV_OUT))


# ── ★ W3fix2 (ревизия `fishing_impl_W3fix_W6_REVIEW.md`, МЕЛК-6): НЕЗАВИСИМЫЙ ВТОРОЙ МЕТОД ЧТЕНИЯ ИСХОДНИКОВ ──────────────
#   Прежняя «сверка с исходниками» брала ожидаемое из `cli/srv/books`, которые вернул ТОТ ЖЕ разбор, что строил строки, —
#   ошибку разбора .BIN / BookInfo / item_table она не видела. Здесь исходники читаются ДРУГИМ способом:
#     • .BIN — `ctypes.LittleEndianStructure` по раскладке эталона (смещения полей проверяются: 0/4/8/72/76/196/316/332,
#       размер 348), запись берётся `from_buffer_copy` целиком (а не `struct.unpack_from` по полям);
#     • BookInfo — ПОТОК ТОКЕНОВ всего файла, как `fscanf("%s")` сервера (GS:74200-74420): число, затем по 70 токенов на
#       строку подряд, без деления на строки (первый способ делит по строкам);
#     • item_table — `csv.reader` с разделителем TAB (первый способ — `str.split()` по любым пробелам), число — регуляркой.
#   Оба способа сравниваются поле в поле (`cross_check`), а CSV/DT сверяются со ВТОРЫМ. Любое расхождение — CSV не
#   пишется, DT не сохраняется.
_INT_RE = re.compile(r"^\s*([+-]?\d+)")   # ведущее целое (семантика `atol`, второй способ)


def _int2(tok):
    """Ведущее целое токена регуляркой (второй способ); нет числа → 0."""
    m = _INT_RE.match(tok or "")
    return int(m.group(1)) if m else 0


class _BinRec(ctypes.LittleEndianStructure):
    """Запись Fish_collection_data.BIN (348 б) по загрузчику эталона `CFishCollection_LoadPictureBookData@0xFD5FD0`."""
    _pack_ = 1
    _fields_ = [("tome", ctypes.c_int32),                 # +0 номер тома
                ("book", ctypes.c_int32),                 # +4 книга-предмет
                ("name", ctypes.c_char * 64),             # +8 имя тома
                ("flag", ctypes.c_int32),                 # +72 флаг
                ("fish", ctypes.c_int32 * FISH_N),        # +76 рыбы
                ("req", ctypes.c_int32 * FISH_N),         # +196 требуемое количество
                ("reward", ctypes.c_int32 * REW_N),       # +316 награды
                ("rcnt", ctypes.c_int32 * REW_N)]         # +332 количества наград


# Смещения полей записи по реверсу (проверка раскладки `_BinRec` против эталона).
_BIN_OFFS = {"tome": 0, "book": 4, "name": 8, "flag": 72, "fish": 76, "req": 196, "reward": 316, "rcnt": 332}


def parse_client2():
    """Второй способ чтения .BIN (ctypes) → ({порядок: поля}, число из шапки)."""
    bad = [(n, getattr(_BinRec, n).offset, o) for n, o in _BIN_OFFS.items() if getattr(_BinRec, n).offset != o]
    if bad or ctypes.sizeof(_BinRec) != REC:
        raise RuntimeError("раскладка _BinRec ≠ реверс: %s, размер %d" % (bad, ctypes.sizeof(_BinRec)))
    with open(BIN_BOOK, "rb") as f:
        d = f.read()
    n_hdr = ctypes.c_int32.from_buffer_copy(d, 4).value
    out = {}
    i = 0
    while i < n_hdr and HDR + REC * (i + 1) <= len(d):
        r = _BinRec.from_buffer_copy(d, HDR + REC * i)
        out[i] = {"Tome": r.tome, "Order": i, "Name": r.name.decode("cp1251", "replace"), "BookItemId": r.book,
                  "Flag": r.flag, "Fish": list(r.fish), "Req": list(r.req), "Reward": list(r.reward),
                  "RewardCnt": list(r.rcnt)}
        i += 1
    return out, n_hdr


def parse_server2():
    """Второй способ чтения BookInfo — поток токенов всего файла (как `fscanf("%s")`) → ({том: поля}, число из шапки)."""
    with open(SRV_BOOK, "rb") as f:
        toks = [m.group(0).decode("cp1251", "replace") for m in re.finditer(rb"\S+", f.read())]
    if not toks:
        return {}, 0
    n_hdr = _int2(toks[0])
    per = 2 + 2 * FISH_N + 2 * REW_N
    out = {}
    pos = 1
    for _ in range(n_hdr):
        if pos + per > len(toks):
            break
        v = [_int2(t) for t in toks[pos:pos + per]]
        pos += per
        a = 2
        out[v[0]] = {"Tome": v[0], "Type": v[1], "Fish": v[a:a + FISH_N], "Req": v[a + FISH_N:a + 2 * FISH_N],
                     "Reward": v[a + 2 * FISH_N:a + 2 * FISH_N + REW_N],
                     "RewardCnt": v[a + 2 * FISH_N + REW_N:a + 2 * FISH_N + 2 * REW_N]}
    return out, n_hdr


def parse_server_books2():
    """Второй способ чтения item_table — `csv.reader` по TAB → {номер тома: первая книга-предмет класса 210}."""
    books = {}
    with open(SRV_ITEMS, "r", encoding="cp1251", errors="replace", newline="") as f:
        rd = csv.reader(f, delimiter="\t")
        next(rd, None)                                   # первая строка — число записей
        for row in rd:
            if len(row) <= COL_ITEM_CLASS or _int2(row[COL_ITEM_CLASS]) != CLASS_BOOK:
                continue
            books.setdefault(_int2(row[COL_ITEM_TOME]), _int2(row[0]))
    return books


def cross_check(src, src2):
    """Сверка первого и второго способа чтения исходников поле в поле → список расхождений (пусто — совпали)."""
    (cli, srv, books), (cli2, srv2, books2) = src, src2
    bad = []
    for what, a, b in (("клиент .BIN", cli, cli2), ("сервер BookInfo", srv, srv2)):
        if sorted(a) != sorted(b):
            bad.append("%s: ключи %s ≠ %s" % (what, sorted(a), sorted(b)))
        for k in sorted(set(a) & set(b)):
            for col in sorted(set(a[k]) | set(b[k])):
                if a[k].get(col) != b[k].get(col):
                    bad.append("%s том %d %s: %s ≠ второй способ %s" % (what, k, col, a[k].get(col), b[k].get(col)))
    if books != books2:
        bad.append("item_table книги 210: %s ≠ второй способ %s" % (sorted(books.items()), sorted(books2.items())))
    log("независимая сверка исходников (второй способ чтения): томов клиента %d/%d, сервера %d/%d, книг 210 %d/%d, "
        "расхождений %d%s" % (len(cli), len(cli2), len(srv), len(srv2), len(books), len(books2), len(bad),
                              ("" if not bad else ": " + "; ".join(bad[:8]))))
    return bad


def verify_csv_text(text, src):
    """Сверка CSV ДО записи (строка в памяти) с исходниками — гейт записи CSV и DT."""
    rd = list(csv.reader(io.StringIO(text)))
    hdr = rd[0]
    rows = {int(r[0]): dict(zip(hdr[1:], r[1:])) for r in rd[1:] if r}
    return verify_rows(lambda k, c: rows[k][c], list(rows), src, "CSV (в памяти, до записи)")


def verify_dt(dt, src):
    """Перечитать DT из редактора (колонки строками) и сверить с исходниками."""
    lib = unreal.DataTableFunctionLibrary
    names = [str(n) for n in dt.get_row_names()]
    cols = {}
    for col in expected_row(0, src).keys():
        vals = list(lib.get_data_table_column_as_string(dt, col))
        cols[col] = dict(zip(names, vals))
    return verify_rows(lambda k, c: cols[c][str(k)], names, src, DT_NAME)


def fill(text):
    """Создать/открыть DT и залить CSV В ПАМЯТИ (★ W3fix2 МЕЛК-6: БЕЗ сохранения — сохраняет `save_dt` только после
    сверки обратным чтением). Возврат (DT, число строк); (None, −1) — не удалось."""
    rs = unreal.load_object(None, ROW_STRUCT)
    if not rs:
        log("!! RowStruct %s НЕ НАЙДЕН — нужна СБОРКА C++ (AsdaFishBookSubsystem.h). DT не создан." % ROW_STRUCT)
        return None, -1
    full = "%s/%s" % (OUT_PKG, DT_NAME)
    dt = unreal.EditorAssetLibrary.load_asset(full) if unreal.EditorAssetLibrary.does_asset_exist(full) else None
    if not dt:
        factory = unreal.DataTableFactory()
        factory.set_editor_property("struct", rs)
        dt = unreal.AssetToolsHelpers.get_asset_tools().create_asset(DT_NAME, OUT_PKG, unreal.DataTable, factory)
    if not dt:
        log("!! DataTable %s не создан" % DT_NAME)
        return None, -1
    if not unreal.DataTableFunctionLibrary.fill_data_table_from_csv_string(dt, text):
        log("!! %s: заливка CSV вернула ошибку — см. LogDataTable выше" % DT_NAME)
        return dt, -1
    return dt, len(dt.get_row_names())


def save_dt(dt):
    """Сохранить DT (только после успешной сверки). Возврат — сохранён ли."""
    full = "%s/%s" % (OUT_PKG, DT_NAME)
    if not unreal.EditorAssetLibrary.save_loaded_asset(dt):
        log("!! %s не сохранён" % full)
        return False
    unreal.log_warning("ASDA_WRITES_ASSET: %s" % full)
    return True


def main():
    """Сборка CSV (+ DT в редакторе) и сверка обратным чтением против НЕЗАВИСИМО прочитанных исходников.
    ★ W3fix2 МЕЛК-6: при любом расхождении ни CSV, ни DT не записываются (гейт ДО записи)."""
    rows, expect, ok, src = build()
    cli2, _n1 = parse_client2()
    bad_ov2 = apply_overlay(cli2, "разбор 2")        # ★ п. 1: та же ведомость на второй способ чтения (иначе сверки разойдутся)
    srv2, _n2 = parse_server2()
    dedup_tomes(cli2, "клиент, разбор 2")            # ★ 01.11.2026 item_dedup: тот же шаг на втором способе чтения
    dedup_tomes(srv2, "сервер, разбор 2")
    src2 = (cli2, srv2, parse_server_books2())       # исходники, прочитанные вторым способом
    bad_src = cross_check(src, src2) + bad_ov2
    if unreal and os.environ.get("ASDA_FISHBOOK_DT_VERIFY_ONLY", "") == "1":
        # Только сверка готовой DT (ни CSV, ни ассет не пишутся).
        full = "%s/%s" % (OUT_PKG, DT_NAME)
        dt = unreal.EditorAssetLibrary.load_asset(full) if unreal.EditorAssetLibrary.does_asset_exist(full) else None
        n_dt, bad_dt = verify_dt(dt, src2) if dt else (0, ["нет %s" % full])
        good = ok and not bad_src and not bad_dt and n_dt == expect
        log("СВЕРКА %s (только чтение DT: строк %d, расхождений %d, исходников %d)" % (
            "ОК" if good else "РАСХОЖДЕНИЕ", n_dt, len(bad_dt), len(bad_src)))
        print("ЗАВЕРШЕНО")
        return
    text = to_csv(rows)
    n_mem, bad_mem = verify_csv_text(text, src2)
    if not ok or bad_src or bad_mem or n_mem != expect:
        log("СВЕРКА РАСХОЖДЕНИЕ — CSV и %s НЕ записаны (сборка %s, исходники %d, CSV в памяти %d/%d строк, расхождений %d)"
            % (DT_NAME, "ок" if ok else "НЕТ", len(bad_src), n_mem, expect, len(bad_mem)))
        print("ЗАВЕРШЕНО")
        return
    with open(CSV_OUT, "w", encoding="utf-8-sig", newline="\n") as f:
        f.write(text)
    log("CSV записан: %s" % CSV_OUT)
    n_csv, bad_csv = verify_csv(src2)
    if not unreal:
        log("редактора нет — шаг «CSV → DataTable» пропущен (штатный режим «только CSV»)")
        log("СВЕРКА %s (по CSV: строк %d, расхождений %d)" % ("ОК" if not bad_csv and n_csv == expect else "РАСХОЖДЕНИЕ",
                                                           n_csv, len(bad_csv)))
        print("ЗАВЕРШЕНО")
        return
    if bad_csv or n_csv != expect:
        log("СВЕРКА РАСХОЖДЕНИЕ — записанный CSV не сошёлся при обратном чтении, %s НЕ записан" % DT_NAME)
        print("ЗАВЕРШЕНО")
        return
    dt, n = fill(text)
    n_dt, bad_dt = verify_dt(dt, src2) if dt is not None and n >= 0 else (0, ["DT не залит"])
    log("%s: записей: %d (ожидалось %d)" % (DT_NAME, n, expect))
    if bad_dt or n != expect or n_dt != expect:
        log("СВЕРКА РАСХОЖДЕНИЕ — %s НЕ сохранён (строк %d/%d, расхождений %d)" % (DT_NAME, n_dt, expect, len(bad_dt)))
        print("ЗАВЕРШЕНО")
        return
    log("СВЕРКА %s" % ("ОК" if save_dt(dt) else "РАСХОЖДЕНИЕ — DT не сохранён"))
    print("ЗАВЕРШЕНО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    main()
