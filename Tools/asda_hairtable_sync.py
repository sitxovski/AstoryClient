# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_hairtable_sync.py — ★★★ СИММЕТРИЯ 1:1 КЛИЕНТСКОЙ И СЕРВЕРНОЙ ТАБЛИЦ ПРИЧЁСОК
#
# ═══ ЗАЧЕМ ═══════════════════════════════════════════════════════════════════════════════════
# Ассортимент парикмахерской (окно 15000) хранится ДВАЖДЫ и обе копии обязаны совпадать:
#   • КЛИЕНТ — `Data/Character/hairtable.BIN`, 20 б заголовка + 228 записей по 312 б
#     (загрузчик `CMap_LoadHairTable@0xCED230`, `world_map.c:5563`); в порт импортирована как
#     `RawAssets/Data/ItemDB/hairtable.csv` → `DT_hairtable`;
#   • СЕРВЕР — `ItemTable_Info/HairTypeTable.txt`, ТЕКСТ, stride 75 б после разбора
#     (загрузчик `sub_4B4970@0x4B4970`, `GameServer.c:72180`).
# Витрину рисует КЛИЕНТ (цена карточки 15007+9i из `+40`), а деньги списывает СЕРВЕР по своей
# колонке 6 (`+69`). Разошлись цены — игрок видит одно, платит другое (дефект D2 отчёта
# `Reports/npc_hairshop_30029.md`, подтверждён живыми числами владельца 2026-08-28).
#
# ═══ ЧТО ДЕЛАЕТ ══════════════════════════════════════════════════════════════════════════════
#   без ключей   — СРАВНИВАЕТ построчно все шесть общих полей и печатает отчёт (ничего не пишет);
#   --apply      — приводит КЛИЕНТСКУЮ таблицу к СЕРВЕРНОЙ (сервер = авторитет по деньгам):
#                  ★ 2026-09-25 пишет ВЕДОМОСТЬ ЗНАЧЕНИЙ `RawAssets/ItemDB/_value_overlay/hairtable.csv`
#                  (было = значение в АРХИВЕ Bin.bbz, стало = сервер) и перегенерирует hairtable.csv/.json
#                  декодером `Tools/decode_item_db.py --only hairtable.bin`, который накладывает ведомость
#                  на байты записи (колонка и `DataHex` согласованы, запись 1:1);
#   --server PATH— другой источник (по умолчанию живой сервер, ★ ТОЛЬКО ЧТЕНИЕ).
# После `--apply` таблицу надо переимпортировать в `DT_hairtable`
#   (`Tools/_import_one_itemdb_table.py`) — иначе игра продолжит читать старую.
#
# ★★★ ПЕРВОПРИЧИНА ПОТЕРИ 28.08 → 31.08.2026 (разбор волны tablesA). Прежний `--apply` правил ТОЛЬКО
#   hairtable.csv (+DataHex). 31.08.2026 17:54 `decode_item_db.py --only soulmateskill.ssi,hairtable.bin`
#   (возврат миснейма Reserved35→RequiredLevel в реестр) перезаписал hairtable.csv из архива — цены 19/29
#   вернулись к 90/120/150/180, и DT_hairtable был переимпортирован уже со старыми ценами. Сверку никто не
#   перезапускал. Теперь приведение живёт в ведомости, которую декодер накладывает на КАЖДОЙ перегенерации.
#
# ⚠ ЗАКОН `tools-no-import-side-effects`: при импорте модуля скрипт НИЧЕГО не делает.
# ⚠ Живой сервер открываем ТОЛЬКО НА ЧТЕНИЕ; серверный файл не правим отсюда никогда.
import argparse
import csv
import io
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_server_txt as _ST  # noqa: E402  ★ дедуп №2 В2 Т7: путь к живому серверу и чтение .txt как у сервера

HERE = os.path.dirname(os.path.abspath(__file__))
CLIENT_CSV = os.path.join(HERE, "..", "RawAssets", "Data", "ItemDB", "hairtable.csv")
# ★ 2026-09-25: генератор CSV (реестр раскладок + ведомости) — на уровень ВЫШЕ порта.
DECODER = os.path.normpath(os.path.join(HERE, "..", "..", "AstoryClient", "Tools", "ItemDB", "decode_item_db.py"))
# ★ дедуп №2 В2 Т7: путь к живому серверу — один на все инструменты (`asda_server_txt.LIVE_V2`, переопределение
#   ASDA_SERVER_V2). Прежняя форма `\\?\UNC\…` больше не нужна: 11.10.2026 все написания открываются одинаково.
SERVER_TXT = _ST.live_itemtable("HairTypeTable.txt")

# (Смещения полей в сырой записи 312 б теперь берутся из реестра декодера `REGISTRY['hairtable.bin']`:
#   Gender +0x22, RequiredLevel +0x23, Enabled +0x24, Price +0x28, HairStyle +0x2C, ColorIdx +0x4D.)
# Колонка CSV → колонка серверного файла (0-based, после разбора по табам).
SRV_COL = {"Gender": 2, "RequiredLevel": 3, "Enabled": 4, "Price": 5, "HairStyle": 6, "ColorIdx": 7}


# Ширина поля в записи сервера (stride 75): колонка 0 → +0 WORD, 1 → +2 строка (%s), 2 → +66, 3 → +67, 4 → +68 BYTE,
#   5 → +69 DWORD, 6 → +73, 7 → +74 BYTE (`GameServer.c:72283-72314`).
_SRV_WIDTH = {2: _ST.as_u8, 3: _ST.as_u8, 4: _ST.as_u8, 5: _ST.as_i32, 6: _ST.as_u8, 7: _ST.as_u8}


def read_server(path):
    """HairTypeTable.txt — ★ ИСПРАВЛЕНИЕ дедуп №2 В2 Т7: читается ТАК, КАК ЕГО ЧИТАЕТ СЕРВЕР (sub_4B4970,
    `RE/ida/server_work/GameServer.c:72264-72315`): `fscanf("%d")` — число N; затем ровно N записей ПОТОКОМ СЛОВ
    (`fscanf("%s")`): Id (`atol` → WORD, обязан идти подряд +1, иначе сервер бросает загрузку «Sync Error»), имя
    (одно слово), шесть чисел (`atol`, ширина поля — `_SRV_WIDTH`). Строки и табуляции серверу не важны.
    Было: разбор ПО СТРОКАМ и ТАБАМ, строки короче 8 полей молча пропускались, `int()` вместо `atol`. Имя с пробелом
    или пустая колонка сдвинули бы у сервера все следующие поля — скрипт бы этого не увидел. На живой таблице
    11.10.2026 итог тот же (228 записей). Возвращает (declared, {Id: {колонка: значение}})."""
    ts = _ST.TokenStream(path)
    declared = ts.count()
    rows, prev = {}, None
    for _ in range(max(0, declared)):
        rid = _ST.as_i16(ts.i32())
        if prev is not None and prev + 1 != rid:
            raise SystemExit("HairTypeTable.txt: Id %d после %d — сервер остановит загрузку «Sync Error»" % (rid, prev))
        prev = rid
        ts.word()                                   # имя причёски (+2, %s)
        vals = {c: _SRV_WIDTH[c](ts.i32()) for c in range(2, 8)}
        rows[rid] = {k: vals[c] for k, c in SRV_COL.items()}
    if ts.lossy:
        print("  ⚠ atol сервера усёк слова: %s" % ts.lossy_text())
    if ts.short:
        print("  ⚠ слов меньше, чем обещает заголовок %d — сервер дочитает нули" % declared)
    if ts.rest():
        print("  ⚠ после %d записей в файле ещё %d слов — сервер их не читает" % (declared, len(ts.rest())))
    return declared, rows


def read_client(path):
    with io.open(path, encoding="utf-8-sig", newline="") as f:
        rdr = csv.DictReader(f)
        return rdr.fieldnames, list(rdr)


def client_field(row, name):
    """Имя колонки RequiredLevel исторически было `Reserved35` (миснейм M10) — понимаем оба."""
    if name in row:
        return name
    if name == "RequiredLevel" and "Reserved35" in row:
        return "Reserved35"
    return None


def load_decoder():
    """Модуль `Tools/decode_item_db.py` (реестр раскладок, пути ведомостей, decode_table)."""
    sys.path.insert(0, os.path.dirname(DECODER))
    import decode_item_db as D
    return D


def archive_rows(D):
    """Записи hairtable ПРЯМО ИЗ АРХИВА (без ведомостей) — «было» для ведомости значений.

    ★ Нельзя брать «было» из CSV: после первого --apply в CSV уже серверные значения, сверка дала бы
    0 расхождений и перезаписала ведомость ПУСТОЙ — ровно та же потеря, только другим путём."""
    import zipfile
    z = zipfile.ZipFile(D.DEFAULT_BBZ)
    names = {n.lower(): n for n in z.namelist()}
    data = z.read(names["hairtable.bin"])
    return D.decode_table("hairtable.bin", data, D.REGISTRY["hairtable.bin"], use_ledgers=False)["rows"]


def write_value_ledger(D, srv):
    """Ведомость значений: каждое поле, где АРХИВ расходится с сервером. Возвращает число правок."""
    rows = archive_rows(D)
    out = []
    for r in rows:
        s = srv.get(int(r["Id"]))
        if s is None:
            continue
        for k, c in SRV_COL.items():
            if int(r[k]) != s[k]:
                out.append({"row": r["_row"], "column": k, "was": r[k], "now": s[k],
                            "source": "HairTypeTable.txt кол.%d (Id %s)" % (c, r["Id"]),
                            "reason": "авторитет по деньгам = сервер (решение владельца 25.09.2026)"
                                      if k == "Price" else "сервер = авторитет"})
    os.makedirs(D.VALUE_LEDGER_DIR, exist_ok=True)
    path = os.path.join(D.VALUE_LEDGER_DIR, "hairtable.csv")
    # ★ 19.11.2026 (ревизия 33 М-5, правило itemdb-ledger-overlays «ведомости только дописывать/сливать»): ведомость пересобирается
    #   целиком, поэтому ЧУЖИЕ строки (не «HairTypeTable.txt кол.…» этого инструмента — например, наборы изменений админ-центра)
    #   стёрлись бы молча. Есть чужие — отказ, ничего не пишется.
    if os.path.isfile(path):
        with io.open(path, encoding="utf-8", newline="") as f:
            foreign = [r for r in csv.DictReader(f) if not str(r.get("source") or "").startswith("HairTypeTable.txt кол.")]
        if foreign:
            raise SystemExit("СТОП: в %s чужие строки (%d, первая: row %s %s — %s); пересборка их сотрёт — сначала выложите/откатите "
                             "набор изменений или слейте ведомость вручную" % (path, len(foreign), foreign[0].get("row"),
                                                                                foreign[0].get("column"), foreign[0].get("source")))
    tmp = path + ".tmp"
    with io.open(tmp, "w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=D.VALUE_LEDGER_COLS)
        w.writeheader()
        w.writerows(out)
    os.replace(tmp, path)
    print("ведомость значений: %s — правок %d" % (path, len(out)))
    return len(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Сверка/синхронизация таблиц причёсок клиент↔сервер")
    ap.add_argument("--server", default=SERVER_TXT, help="серверный HairTypeTable.txt (только чтение)")
    ap.add_argument("--client", default=CLIENT_CSV, help="клиентский hairtable.csv")
    ap.add_argument("--apply", action="store_true", help="привести КЛИЕНТ к СЕРВЕРУ (иначе только отчёт)")
    a = ap.parse_args(argv)

    declared, srv = read_server(a.server)
    names, cli = read_client(a.client)
    print("записей: клиент %d, сервер %d (заголовок обещает %d)" % (len(cli), len(srv), declared))
    if declared != len(srv):
        print("  ⚠ заголовок серверного файла не сходится с числом строк")

    diffs = {k: [] for k in SRV_COL}
    missing = []
    for row in cli:
        i = int(row["Id"])
        s = srv.get(i)
        if s is None:
            missing.append(i)
            continue
        for k in SRV_COL:
            col = client_field(row, k)
            if col is None:
                continue
            if int(row[col]) != s[k]:
                diffs[k].append((i, int(row[col]), s[k]))
    only_srv = sorted(set(srv) - {int(r["Id"]) for r in cli})
    print("нет у сервера: %s" % (missing or "—"))
    print("нет у клиента: %s" % (only_srv or "—"))
    for k, v in diffs.items():
        mark = "★" if v else " "
        print("%s %-14s расхождений %3d %s" % (mark, k, len(v),
              ("примеры (id, клиент, сервер): %s" % v[:5]) if v else ""))

    if a.apply:
        # ★ 2026-09-25: ведомость (было = архив) + перегенерация CSV декодером, затем повторная сверка.
        D = load_decoder()
        n = write_value_ledger(D, srv)
        r = subprocess.run([sys.executable, DECODER, "--only", "hairtable.bin"])
        if r.returncode != 0:
            print("⚠ декодер вернул %d — CSV НЕ перегенерирован" % r.returncode)
            return 2
        names, cli = read_client(a.client)
        left = sum(1 for row in cli for k in SRV_COL
                   if client_field(row, k) and int(row["Id"]) in srv
                   and int(row[client_field(row, k)]) != srv[int(row["Id"])][k])
        print("ЗАПИСАНО: ведомость %d правок; после перегенерации расхождений с сервером: %d. "
              "Переимпортируй DT_hairtable." % (n, left))
        return 0 if left == 0 else 1
    return 0 if not any(diffs.values()) else 1


# ★ ЗАКОН `tools-no-import-side-effects`: при импорте модуля НИЧЕГО не выполняется.
if __name__ == "__main__" or globals().get("__spec__") is None:
    sys.exit(main())
