# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_pet_server_tables.py — ТАБЛИЦЫ ПИТОМЦЕВ (волна 1 порта питомцев).
#
#   GameServer\PetData_Info\PetInfo_Table.txt          → /Game/AsdaAssets/Data/Pet/DT_PetInfo
#   GameServer\PetData_Info\PetExp_Table.txt           → …/DT_PetExpServer
#   GameServer\PetData_Info\PetIncubateRate_Table.txt  → …/DT_PetIncubateRate
#   GameServer\PetData_Info\Petdetailevotable.txt      → …/DT_PetUnionRecipe
#   GameServer\PetData_Info\Petlimitbreaktable.txt     → …/DT_PetLimitBreak
#   Data\PetEvolutionTable.bin (клиент, тег PEV)       → …/DT_PetEvoStage
#
# ══════════════════════════ ПОЧЕМУ ИМЕННО ЭТИ ИСТОЧНИКИ ═══════════════════════════════════════
# Закон client-server-table-desync: авторитет — СЕРВЕР. Клиентские pettable/petexptable/
# petoptiontable уже в порту (DT_pettable и др.) и остаются источником ВИДА питомца; правила
# (пределы, опыт, бонусы, корм, рецепты, пороги) живут только в серверных .txt.
# Исключение одно — КАРТА РАЗВИТИЯ: её сервер не читает вовсе (парсера в диапазоне загрузчика
# GameServer.c:69613-69833 нет), а серверный Petevotable.txt = лишь колонка StuffIdx из
# клиентского PEV. Поэтому карта берётся из клиентского .bin, где есть ещё PetType[7]/Level[7].
#
# ══════════════════════════ РАСКЛАДКА — С ЗАГРУЗЧИКА, НЕ С ВИДА ФАЙЛА ═════════════════════════
# `CParamManager::LoadPetTables`, RE/ida/server_work/GameServer.c:69613-69833. Каждая таблица
# читается ПОТОКОМ ТОКЕНОВ `fscanf(f, "%s")`: сначала count, затем по N токенов на запись.
#
# ★★★ ДЕФЕКТ ОРИГИНАЛА Д4 — Petlimitbreaktable. Загрузчик делает РОВНО ЧЕТЫРЕ fscanf на запись
#   16 байт (GameServer.c:69814-69828, `operator new(16 * v9)` на :69802), а файл имеет ПЯТЬ
#   колонок. Поток токенов сдвигается на один с каждой строкой. Контроль вторым методом: у всех
#   прочих пет-таблиц число fscanf совпадает с числом колонок (PetInfo 17/17, PetExp 4/4,
#   PetIncubateRate 2/2, Petdetailevotable 9/9) — расходится только эта.
#   Поэтому в таблицу кладутся ОБА набора: колонки файла (Thr1..Thr4) и четыре dword'а так, как
#   их реально кладёт загрузчик (ServerD0..ServerD3 — воспроизведён поток токенов).
#   ⚠ На ЖИВОМ образе файл не проверен (\\@@ASTORY_SERVER_HOSTNAME@@\h\ASDA\Working_ASDA\v2 недоступен).
#
# ★ ПРАВИЛО tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает. Вся работа —
#   в main(), который зовётся только из-под `__main__`.
#
# Запуск:
#   headless UE:  Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_import_pet_server_tables.py
#   без UE:       python asda_import_pet_server_tables.py --json-only
#   сверка:       python asda_import_pet_server_tables.py --verify
import json
import os as _os
import struct
import sys as _sys

# ★ ПРАВИЛО tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает — ни путей в
#   `sys.path` не правит, ни кодировку консоли не трогает. Обе операции нужны только когда тул
#   реально работает, поэтому они вынесены в функции и зовутся из `verify()` / `main()`.
_PATHS = None


def _paths():
    """Реестр корней проекта (`asda_paths`). Путь в `sys.path` добавляется при ПЕРВОМ обращении."""
    global _PATHS
    if _PATHS is None:
        here = _os.path.dirname(_os.path.abspath(__file__))
        if here not in _sys.path:
            _sys.path.insert(0, here)
        import asda_paths
        _PATHS = asda_paths
    return _PATHS


def _utf8_console():
    """Консоль Windows бывает в cp1251 и роняет print на «★»/«─» (грабля tools-no-import-side-effects
    /cp1251). Тот же приём, что в `set0_run_game.py`, но НЕ при импорте, а при запуске."""
    for _s in (_sys.stdout, _sys.stderr):
        try:
            _s.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass


def _srv_dir():
    """Каталог серверных пет-таблиц. ★ дедуп №2 В2 Т7 (закон server-live-path): ЖИВОЙ сервер, если доступен
    (`asda_server_txt.LIVE_GAMESERVER`, только чтение); иначе локальная копия «@@ASTORY_DEV_ROOT_FWD@@/Donors/GameServer_stock».
    11.10.2026 все пять файлов на живом и в копии побайтно равны (md5), поэтому вывод не меняется."""
    _paths()                                    # путь Tools в sys.path — при первом обращении, не при импорте
    import asda_server_txt as _ST
    live = _ST.live_path("GameServer", "PetData_Info")
    if _os.path.isdir(live):
        return live
    return _os.path.join(_paths().GAMESERVER, "PetData_Info")   # стоковый сервер: <ROOT>\Donors\GameServer_stock


def _pev_bin():
    return _os.path.join(_paths().DATA, "PetEvolutionTable.bin")


def _pde_bin():
    return _os.path.join(_paths().DATA, "PetStuff.bin")


def _out_dir():
    return _os.path.join(_paths().ROOT, "AstoryClient", "RawAssets", "Data", "Pet")


PKG = "/Game/AsdaAssets/Data/Pet"

# Имя UStruct → путь строки таблицы в C++ (Public/Pet/AsdaPetTableRows.h).
RS = "/Script/AsdaReborn.AsdaPet%sRow"


def _tokens(path):
    """Файл как поток пробельно-разделённых токенов — ровно так его видит fscanf(f, "%s")."""
    with open(path, "r", encoding="latin-1") as f:
        return f.read().split()


def _name16(tok):
    """Имя породы: cp949-байты как они лежат в файле. Возвращает (строка для показа, hex 16 байт)."""
    raw = tok.encode("latin-1")[:16]
    raw = raw + b"\x00" * (16 - len(raw))
    try:
        shown = raw.rstrip(b"\x00").decode("cp949")
    except UnicodeDecodeError:
        shown = raw.rstrip(b"\x00").decode("latin-1")
    return shown, raw.hex()


# ────────────────────────────── разбор серверных .txt ────────────────────────────────────────

def parse_petinfo(path):
    """17 колонок на запись (GameServer.c:69635-69675). Смещения — в комментариях структуры."""
    t = _tokens(path)
    n = int(t[0])
    p = 1
    rows = []
    for _ in range(n):
        idx = int(t[p]); p += 1                       # +0  u16
        shown, hexname = _name16(t[p]); p += 1        # +2  char[16] cp949
        u18 = int(t[p]); p += 1                       # +18 u8
        u19 = int(t[p]); p += 1                       # +19 u8
        base_max = int(t[p]); p += 1                  # +20 u8  BaseMaxLevel
        exp_rate = int(t[p]); p += 1                  # +21 u8  ExpRateX10
        u22 = int(t[p]); p += 1                       # +22 i32
        u26 = int(t[p]); p += 1                       # +26 i32
        u30 = int(t[p]); p += 1                       # +30 i32
        at, av = [], []
        for _j in range(3):
            at.append(int(t[p])); p += 1              # +34+6j u16
            av.append(int(t[p])); p += 1              # +36+6j i32
        feed_mask = int(t[p]); p += 1                 # +52 i32
        decay_ms = int(t[p]); p += 1                  # +56 i32
        rows.append({
            "Name": str(idx), "PetIndex": idx, "DefaultName": shown, "DefaultNameHex": hexname,
            "Unk18": u18, "Unk19": u19, "BaseMaxLevel": base_max, "ExpRateX10": exp_rate,
            "Unk22": u22, "Unk26": u26, "Unk30": u30,
            "AbilType": at, "AbilValue": av,
            "FeedTypeMask": feed_mask, "FullnessDecayMs": decay_ms,
        })
    return rows


def parse_petexp(path):
    """4 колонки (GameServer.c:69701-69712), запись 13 байт."""
    t = _tokens(path)
    n = int(t[0]); p = 1
    rows = []
    for _ in range(n):
        lv = int(t[p]); p += 1
        ex = int(t[p]); p += 1
        rate = int(t[p]); p += 1
        unk = int(t[p]); p += 1
        rows.append({"Name": str(lv), "Level": lv, "Exp": ex, "AbilRatePercent": rate, "Unk9": unk})
    return rows


def parse_incubate(path):
    """2 колонки (GameServer.c:69735-69742), запись 5 байт."""
    t = _tokens(path)
    n = int(t[0]); p = 1
    rows = []
    for _ in range(n):
        g = int(t[p]); p += 1
        r = int(t[p]); p += 1
        rows.append({"Name": str(g), "Grade": g, "Rate": r})
    return rows


def parse_union(path):
    """9 колонок (GameServer.c:69766-69794), запись 18 байт.
    ★ НАПРАВЛЕНИЕ: c1 = ResultPetIndex (+2), c2 = ResultPetLevel (+4),
      c3/c4/c5 = ReqPetIndex[0..2] (+6/+10/+14), c6/c7/c8 = ReqLevel[0..2] (+8/+12/+16).
      Доказано трижды: загрузчиком, обработчиком юниона (GameServer.c:36000-36032) и
      побайтовым дампом клиентского PetStuff.bin."""
    t = _tokens(path)
    n = int(t[0]); p = 1
    rows = []
    prev = None
    for _ in range(n):
        ui = int(t[p]); p += 1
        if prev is not None and ui != prev + 1:
            raise ValueError("Petdetailevotable.txt Sync Error: индексы не подряд (%s → %s)" % (prev, ui))
        prev = ui
        res_idx = int(t[p]); p += 1
        res_lv = int(t[p]); p += 1
        req_idx = [int(t[p + k]) for k in range(3)]; p += 3
        req_lv = [int(t[p + k]) for k in range(3)]; p += 3
        rows.append({"Name": str(ui), "UnionIndex": ui,
                     "ResultPetIndex": res_idx, "ResultPetLevel": res_lv,
                     "ReqPetIndex": req_idx, "ReqLevel": req_lv})
    return rows


def parse_limitbreak(path):
    """★ ДВА чтения одного файла.
       • по колонкам файла (5 на строку) — как задумывал издатель;
       • по потоку токенов ЧЕТВЁРКАМИ — как реально делает загрузчик сервера (дефект Д4).

    ★★★ КЛЮЧ СТРОКИ — ИНДЕКС МАССИВА (0..9), А НЕ КОЛОНКА `Level` ФАЙЛА.
      Сервер адресует таблицу `base + 16 * MaxLevel` (GameServer.c:36725/36727/36729), то есть
      ПОРЯДКОВЫМ НОМЕРОМ записи в массиве. Колонка 1 файла идёт 1..10, поэтому её значение
      отличается от индекса на единицу даже без дефекта Д4. Ключ по ней дал бы сдвиг ещё на один.
      Само значение колонки сохраняется в поле `Level` — как справочное."""
    t = _tokens(path)
    n = int(t[0])
    body = t[1:]
    cols = len(body) // n if n else 0
    rows = []
    for i in range(n):
        # как в файле
        c = body[i * cols:(i + 1) * cols]
        lv = int(c[0])
        thr = [int(x) for x in c[1:5]] + [0] * max(0, 5 - len(c))
        # как в памяти сервера: строго четыре токена подряд, без оглядки на границы строк
        d = body[i * 4:(i + 1) * 4]
        d = [int(x) for x in d] + [0] * (4 - len(d))
        rows.append({"Name": str(i), "RowIndex": i, "Level": lv,
                     "Thr1": thr[0], "Thr2": thr[1], "Thr3": thr[2], "Thr4": thr[3],
                     "ServerD0": d[0], "ServerD1": d[1], "ServerD2": d[2], "ServerD3": d[3]})
    return rows, cols


# ────────────────────────────── разбор клиентского PEV ───────────────────────────────────────

def parse_pev(path):
    """Заголовок 20 б: i32 0, i32 count, i32 dwordsPerRecord, тег 'PEV\\0', i32 0.
       Запись 22 dword: +0 EvoIndex, +4 PetType[7], +32 Level[7], +60 StuffIdx[7]."""
    raw = open(path, "rb").read()
    count = struct.unpack_from("<i", raw, 4)[0]
    dw = struct.unpack_from("<i", raw, 8)[0]
    tag = raw[12:15]
    if tag != b"PEV":
        raise ValueError("PetEvolutionTable.bin: ожидался тег PEV, получен %r" % tag)
    if 20 + count * dw * 4 != len(raw):
        raise ValueError("PetEvolutionTable.bin: размер %d не равен 20 + %d*%d*4" % (len(raw), count, dw))
    rows = []
    for i in range(count):
        off = 20 + i * dw * 4
        v = struct.unpack_from("<%di" % dw, raw, off)
        rows.append({"Name": str(v[0]), "EvoIndex": v[0],
                     "PetType": list(v[1:8]), "Level": list(v[8:15]), "StuffIdx": list(v[15:22])})
    return rows


def parse_pde(path):
    """PetStuff.bin (тег PDE): 9 записей по 9 dword — КЛИЕНТСКАЯ копия Petdetailevotable.txt.
       Используется только для сверки (в DataTable не кладётся: источник — сервер)."""
    raw = open(path, "rb").read()
    count = struct.unpack_from("<i", raw, 4)[0]
    dw = struct.unpack_from("<i", raw, 8)[0]
    if raw[12:15] != b"PDE":
        raise ValueError("PetStuff.bin: ожидался тег PDE")
    return [list(struct.unpack_from("<%di" % dw, raw, 20 + i * dw * 4)) for i in range(count)]


# ─────────────────────────────────── сборка ──────────────────────────────────────────────────

TABLES = [
    ("DT_PetInfo",         "Info",         lambda: parse_petinfo(_os.path.join(_srv_dir(), "PetInfo_Table.txt"))),
    ("DT_PetExpServer",    "Exp",          lambda: parse_petexp(_os.path.join(_srv_dir(), "PetExp_Table.txt"))),
    ("DT_PetIncubateRate", "IncubateRate", lambda: parse_incubate(_os.path.join(_srv_dir(), "PetIncubateRate_Table.txt"))),
    ("DT_PetUnionRecipe",  "UnionRecipe",  lambda: parse_union(_os.path.join(_srv_dir(), "Petdetailevotable.txt"))),
    ("DT_PetLimitBreak",   "LimitBreak",   lambda: parse_limitbreak(_os.path.join(_srv_dir(), "Petlimitbreaktable.txt"))[0]),
    ("DT_PetEvoStage",     "EvoStage",     lambda: parse_pev(_pev_bin())),
]


def verify(log=print):
    _utf8_console()
    """Сверка без UE: колонки, счётчики, дефект Д4 и совпадение клиентского PDE с серверным .txt."""
    ok = True
    info = parse_petinfo(_os.path.join(_srv_dir(), "PetInfo_Table.txt"))
    log("PetInfo_Table: %d пород (сервер). Породы клиента (DT_pettable) — 32; лишняя недостижима." % len(info))

    exp = parse_petexp(_os.path.join(_srv_dir(), "PetExp_Table.txt"))
    log("PetExp_Table: %d уровней (сервер), у клиента 11 — расхождение штатное, берём сервер." % len(exp))

    inc = parse_incubate(_os.path.join(_srv_dir(), "PetIncubateRate_Table.txt"))
    log("PetIncubateRate_Table: %d строк. ★ Применяется ТОЛЬКО к cash-яйцам (GameServer.c:34619-34621);"
        " обычное яйцо вылупляется гарантированно (v15 = 100000, :34639)." % len(inc))

    uni = parse_union(_os.path.join(_srv_dir(), "Petdetailevotable.txt"))
    log("Petdetailevotable: %d рецептов. Направление c1=Result:" % len(uni))
    for r in uni:
        log("   рецепт %d: РЕЗУЛЬТАТ порода %d ур.%d ← %s" %
            (r["UnionIndex"], r["ResultPetIndex"], r["ResultPetLevel"],
             " + ".join("%d(ур.%d)" % (r["ReqPetIndex"][k], r["ReqLevel"][k]) for k in range(3))))

    # третий метод: клиентский PDE побайтово повторяет серверный .txt
    if _os.path.exists(_pde_bin()):
        pde = parse_pde(_pde_bin())
        # Сверка идёт в ПОРЯДКЕ КОЛОНОК ФАЙЛА (он же порядок dword'ов в PDE):
        #   c0 UnionIndex, c1 Result, c2 ResultLevel, c3..c5 ReqPetIndex[0..2], c6..c8 ReqLevel[0..2].
        #   ⚠ В ПАМЯТИ сервера эти же поля лежат ЧЕРЕДУЯСЬ (+6/+8/+10/+12/+14/+16) — порядок
        #     колонок и порядок смещений у этой таблицы РАЗНЫЕ, путать нельзя.
        same = all(pde[i] == [uni[i]["UnionIndex"], uni[i]["ResultPetIndex"], uni[i]["ResultPetLevel"]]
                   + uni[i]["ReqPetIndex"] + uni[i]["ReqLevel"]
                   for i in range(len(pde)))
        log("Клиентский PetStuff.bin (PDE) == серверный Petdetailevotable.txt: %s" % ("ДА" if same else "НЕТ"))
        ok = ok and same
    else:
        log("PetStuff.bin не найден — сверка третьим методом пропущена")

    lb, cols = parse_limitbreak(_os.path.join(_srv_dir(), "Petlimitbreaktable.txt"))
    log("Petlimitbreaktable: колонок в файле %d, загрузчик читает 4 ⇒ %s" %
        (cols, "ДЕФЕКТ Д4, значения в памяти сервера СДВИНУТЫ" if cols != 4 else "сдвига нет"))
    log("   ключ строки = ИНДЕКС МАССИВА (им сервер адресует таблицу: base + 16*MaxLevel)")
    for r in lb:
        log("   idx=%2d (файл Level=%2d)  файл: %6d %6d %6d %6d | память сервера: %6d %6d %6d %6d" %
            (r["RowIndex"], r["Level"], r["Thr1"], r["Thr2"], r["Thr3"], r["Thr4"],
             r["ServerD0"], r["ServerD1"], r["ServerD2"], r["ServerD3"]))

    pev = parse_pev(_pev_bin())
    log("PetEvolutionTable.bin (PEV): %d карт развития" % len(pev))
    for r in pev:
        stages = " ".join("[—]" if r["PetType"][k] < 0
                          else "[порода %d ур.%d рец.%d]" % (r["PetType"][k], r["Level"][k], r["StuffIdx"][k])
                          for k in range(7))
        log("   карта %d: %s" % (r["EvoIndex"], stages))
    return ok


def write_json():
    """JSON-копии разобранных таблиц в RawAssets (закон «сырым файлам в Content не место»)."""
    out_dir = _out_dir()
    if not _os.path.isdir(out_dir):
        _os.makedirs(out_dir)
    out = {}
    for name, _rs, fn in TABLES:
        rows = fn()
        path = _os.path.join(out_dir, name + ".json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(rows, f, ensure_ascii=False, indent=0)
        out[name] = (len(rows), path)
    return out


def build_tables(unreal):
    """Создать/обновить шесть DataTable. RowStruct обязан быть уже скомпилирован."""
    _paths()                                    # путь Tools в sys.path (лениво, правило tools-no-import-side-effects)
    import asda_dt_common as _DTC               # ★ дедуп №2 В2 Т4: общий шаг «заливка → сохранение» DataTable
    def log(m):
        unreal.log_warning("[питомцы-таблицы] %s" % m)

    made = 0
    # ★ 21.10.2026: ASDA_PET_TABLES_ONLY=DT_PetInfo[,…] — пересобрать только перечисленные таблицы (точечная запись: после
    #   смены кличек пород на сервере менялся один PetInfo_Table.txt, остальные пять DataTable трогать незачем).
    only = [s.strip() for s in _os.environ.get("ASDA_PET_TABLES_ONLY", "").split(",") if s.strip()]
    for name, rs_name, fn in TABLES:
        if only and name not in only:
            continue
        rows = fn()
        rs_path = RS % rs_name
        rs = unreal.load_object(None, rs_path)
        if not rs:
            log("!! RowStruct %s НЕ НАЙДЕН — нужна СБОРКА C++ "
                "(Public/Pet/AsdaPetTableRows.h ещё не скомпилирован). %s не создан." % (rs_path, name))
            continue
        full = "%s/%s" % (PKG, name)
        dt = _DTC.open_or_create_dt(unreal, PKG, name, rs)  # дедуп №2 В2 Т4: общий «открыть-или-создать»
        if not dt:
            log("!! DataTable %s не создан" % name)
            continue
        text = json.dumps(rows, ensure_ascii=False, indent=0)
        problems = _DTC.fill_and_save(unreal, dt, text, log, fmt="json")  # ★ ИСПРАВЛЕНИЕ дедуп №2 В2 Т4: провал заливки → DT не сохраняется; маркер ASDA_WRITES_ASSET
        log("%s: строк=%d (разобрано %d), заполнение=%s" % (name, len(dt.get_row_names()), len(rows), problems))
        made += 1
    return made


def main():
    _utf8_console()
    args = _sys.argv[1:]
    if "--verify" in args:
        verify()
        return
    out = write_json()
    for k, (n, p) in sorted(out.items()):
        print("JSON %-22s строк=%3d → %s" % (k, n, p))
    if "--json-only" in args:
        return
    try:
        import unreal  # noqa: F401  (есть только внутри редактора)
    except ImportError:
        print("unreal недоступен — выполнен только разбор в JSON (используй --json-only осознанно)")
        return
    import unreal as _u
    verify(lambda m: _u.log_warning("[питомцы-таблицы] %s" % m))
    n = build_tables(_u)
    _u.log_warning("[питомцы-таблицы] готово: таблиц собрано %d из %d" % (n, len(TABLES)))


if __name__ == "__main__":
    main()
