# Copyright (c) 2026 @sitxovski (Discord)
# Экстрактор ВСЕГО текста игры из Data-таблиц → CSV по категориям (для импорта в UE DataTable).
# Формат (4 агента, Reports/text_*_re.md): 20б заголовок [reserved][u32 count@4][meta][magic 4char@0xC][reserved]
#   + N фикс-записей, stride ХАРДКОЖЕН (не в заголовке). Layout A: записи с 0x14. Layout B: доп. fread(count) → записи с 0x18.
# ДЕДУП: таблицы из Bin.bbz (skilltooltip/emotiontip/hairtable/itemoptiontable/soulstateinfotip) уже в item-DB
#   (/Game/AsdaAssets/Data/ItemDB, доступ AsdaItemTableSubsystem) → здесь НЕ дублируем. /Text/ = только LOOSE Data-файлы.
import struct, os, csv, sys
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_bintable as _BT   # общий разбор .bin: записи, C-строки, pick_enc (волна 2)

DATA = _P.DATA
OUT  = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/RawAssets/Text"

def decode(b, enc):
    """Строка до NUL. enc='auto' -> кодировка выбирается по содержимому (см. pick_enc)."""
    b = bytes(_BT.cstr_bytes(b))
    if not b: return ""
    if enc == "auto":
        enc = pick_enc(b)
    return b.decode(enc, "replace")

# Реестр таблиц: (файл, категория, stride, layoutB?, [(поле, offset, тип)])  тип: 's'=строка(до NUL), 'i'=int32, 'h'=int16, 'b'=byte
# offset строки = внутри записи; длину строки берём до NUL. Необязательный 4-й элемент поля = ФИКСИРОВАННАЯ
#   длина char[N] (кламп): без него строка, занявшая поле целиком без NUL, утекает в соседние поля.
TABLES = [
    # NPC / квесты / диалоги
    # ★ name = char[32] @+4 (сверено с DT_NpcNames и записью id=81, где поле заполнено без NUL);
    #   дальше в записи идут байты-поля и имя модели — без клампа они утекали в имя.
    ("NPC/npc_list.BIN",              "NPC",     136,  False, [("id",0,'i'), ("name",4,'s',32)]),
    ("NPC/Episode/Episode_Table.BIN", "Quest",   288,  False, [("id",236,'i'), ("name",0,'s')]),
    # ★ 09.10.2026 (мультиязычность, этап 0, `Reports/owner_1009/multilang/03_sources.md` §3.4): имя квеста —
    #   char[120] @+12 (было @+18: имя без первых 6 байт), описание — char[256] @+132 (в порт не переносилось).
    #   В RU-файле MINE по @+12 гибрид «Slash ание крыс-рубак"» (слияние писало RU с +18) — RU-текст кладёт
    #   ведомость `RawAssets/Data/_value_overlay/Raid_quest_data.csv` (Фантазиум @+12/@+132, сверено с EN по смыслу).
    ("NPC/Raid_quest_data.BIN",       "Quest",   388,  False, [("id",0,'i'), ("name",12,'s',120), ("desc",132,'s',256)]),
    ("NPC/SNQInfo.Bin",               "Quest",   1796, True,  [("id",0,'i'), ("name",2,'s'), ("subtitle",34,'s')]),
    ("NPC/NFT.BIN",                   "NPC",     36,   False, [("id",0,'i'), ("family",4,'s')]),
    ("NPC/TIM_List.BIN",              "World",   204,  False, [("id",0,'i'), ("village",4,'s')]),
    # мир / прочее
    # ★ 19.11.2026 (ревизия 34 М-2, админ-центр А4.2): `Map_info.BIN` ОТСЮДА УБРАН (был `("Map_info.BIN","World",392,False,
    #   [("id",0,'i'),("name",4,'s')])`). Владелец `RawAssets/Text/World/Map_info.csv` — реестр карт
    #   `Tools/data/asda_map_registry.json` (`loc_name`), CSV рендерит `asda_map_registry.py` (единый источник, аудит дублей 14.09).
    #   Этот экстрактор писал тот же файл из клиентского BIN и молча стирал правки имён карт, сделанные через реестр.
    #   Других потребителей у ветки нет (import_text_tables читает CSV каталогов, места лова — asda_build_fishing_dt сам читает BIN).
    ("Constellation.bin",             "World",   65,   False, [("id",0,'b'), ("name",1,'s')]),
    ("SoulGuarderTable.BIN",          "Skill",   1956, False, [("id",0,'i'), ("name",16,'s')]),
    ("Fish_collection_data.bin",      "Fishing", 348,  False, [("id",0,'i'), ("name",8,'s')]),
    ("Tutorial_text.bin",             "Tutorial",204,  False, [("id",0,'i'), ("text",4,'s')]),
    ("Tutorial_text_answer.bin",      "Tutorial",204,  False, [("id",0,'i'), ("text",4,'s')]),
    # ★ 09.10.2026 (этап 0, `03_sources.md` §3.5): у MINE этот файл ПОДМЕНЁН копией Tutorial_text_answer Фантазиума
    #   (тег TAX, «Назад/Далее»). Настоящий файл сборки — TRC, 5 записей по 68 Б (id i32 + char[64], корейский) — берётся
    #   из EN-оригинала той же сборки (SOURCE_OVERRIDE ниже); RU-текст — ведомость
    #   `RawAssets/Data/_value_overlay/Tutorial_middle_category.csv`.
    ("Tutorial_middle_category.bin",  "Tutorial",68,   False, [("id",0,'i'), ("text",4,'s',64)]),
    # магазин / крафт / клан
    ("NPC/Shop/Bpremium.BIN",         "Shop",    80,   False, [("id",0,'i'), ("name",4,'s')]),
    ("NPC/Shop/Fpremium.BIN",         "Shop",    80,   False, [("id",0,'i'), ("name",4,'s')]),
    ("NPC/Shop/Spremium.bin",         "Shop",    76,   False, [("id",0,'i'), ("name",4,'s')]),
    # ★ 2026-09-02: `NPC/making_recipe.BIN` ОТСЮДА УБРАН (был `("NPC/making_recipe.BIN","Craft",128,
    #   False,[("id",0,'h'),("name",2,'s')])`). Здесь бралось только имя рецепта, а рабочие поля
    #   записи (код результата +72, шесть кодов сырья +76…+99, количества +100, под-рецепты +112)
    #   в общий текстовый USTRUCT `FAsdaTextRow` не помещаются. Таблица целиком переехала в ОБЩИЙ
    #   реестр форматов `@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\ItemDB\decode_item_db.py` (запись `making_recipe.bin`, `loose`) →
    #   `DT_making_recipe` в `/Game/AsdaAssets/Data/ItemDB`, имя рецепта там в колонке `RecipeName`.
    #   Два парсера одного игрового файла держать нельзя (закон `check-existing-before-creating`).
    #   Старый ассет `/Text/Craft/DT_making_recipe` числится орфаном в `import_text_tables.py`.
    ("Clan_Sign.BIN",                 "Clan",    76,   False, [("id",0,'i'), ("tex",4,'s')]),
    # ★ Дизайн флага клана (композитор эмблемы, AsdaClanFlag.cpp): рамка-фон и иконка рамки.
    ("Clan_BorderImage.BIN",          "Clan",    76,   False, [("id",0,'i'), ("tex",4,'s')]),
    ("Clan_BorderIcon.BIN",           "Clan",    76,   False, [("id",0,'i'), ("tex",4,'s')]),
]

def extract_bbz():
    """Текст-таблицы ВНУТРИ Data/Character/Bin.bbz (ZIP). Подтв. разбором hex (магии SKT/ETT, оффсеты строк):
    skilltooltip.bin (SKT,504,1255): name@+4, desc@+100 (тултипы дерева скиллов). emotiontip.bin (ETT,265,29):
    name@+0. soulmate/pettable = только asset-имена (display-текста нет) → пропуск. Кодировка cp949 (ASCII/кор)."""
    import zipfile
    bbz = os.path.join(DATA, "Character/Bin.bbz")
    if not os.path.isfile(bbz): return 0
    z = zipfile.ZipFile(bbz)
    specs = [
        ("skilltooltip.bin",  "Skill",   504, [("id",0,'i'), ("name",4,'s'), ("desc",100,'s')], "DT_SkillTreeTooltip"),
        ("emotiontip.bin",    "Emotion", 265, [("id",0,'i'), ("name",0,'s')],                   "DT_EmotionTip"),
    ]
    total = 0
    for fn, cat, stride, fields, asset in specs:
        d = z.read(fn)
        rows = []
        for i, rec in _BT.iter_records(d, stride, count=_BT.record_count(d, signed=False)):
            row = {f[0]: read_val(rec, f[1], f[2], "auto", f[3] if len(f) > 3 else 0) for f in fields}
            if any(isinstance(row[f[0]], str) and row[f[0]].strip() for f in fields if f[2]=='s'):
                rows.append(row)
        os.makedirs(os.path.join(OUT, cat), exist_ok=True)
        with open(os.path.join(OUT, cat, asset[3:] + ".csv"), "w", encoding="utf-8-sig", newline="") as f:
            w = csv.writer(f); w.writerow(["Name"] + [x[0] for x in fields])
            for r in rows: w.writerow([str(r.get("id"))] + [r[x[0]] for x in fields])
        print("  OK %-24s cat=%-9s записей=%d (Bin.bbz)" % (asset, cat, len(rows)))
        total += len(rows)
    return total

def extract_talk():
    """153 файла NPC/Episode/Talk/*NPC.BIN (TAK/208): реплика @+5 (cp949) → один Dialogue CSV.
    RowName = глобально-уникальный индекс (поле @+4 = не line-idx, а мусор-инкремент → коллизии/потеря строк).
    Столбец line = порядковый номер реплики ВНУТРИ файла (стабильная привязка npc→реплика)."""
    import glob
    tdir = os.path.join(DATA, "NPC/Episode/Talk")
    if not os.path.isdir(tdir): return 0
    rows = []  # (base, lineInFile, text)
    for path in sorted(glob.glob(os.path.join(tdir, "*.BIN")) + glob.glob(os.path.join(tdir, "*.bin"))):
        base = os.path.splitext(os.path.basename(path))[0]
        d = open(path, "rb").read()
        if len(d) < 20: continue
        stride = 208
        li = 0
        for i, rec in _BT.iter_records(d, stride, count=_BT.record_count(d, signed=False)):
            line = decode(rec[5:], "auto")   # реплики переведены -> кодировка по содержимому
            if line.strip():
                rows.append((base, li, line)); li += 1
    # ★ 14.10.2026 (names11, этап 1): ведомость значений реплик (строка `row` = «<файл>:<номер реплики в файле>», колонка text):
    #   перевод корейских реплик, оставшихся в RU-сборке. Формат row,column,was,now,source,reason; `was` не совпало - стоп.
    ovp = os.path.join(_OVERLAY_DIR, "Talk.csv")
    if os.path.exists(ovp):
        # ключ «<файл>:<реплика>» может встретиться не раз (glob по *.BIN и *.bin на Windows даёт файл дважды - прежнее поведение
        #   экстрактора, дубли строк в Talk.csv): правка ложится на ВСЕ строки с этим ключом
        idx = {}
        for i, (b, l, _t) in enumerate(rows):
            idx.setdefault("%s:%d" % (b, l), []).append(i)
        n_ov = 0
        with open(ovp, "r", encoding="utf-8-sig", newline="") as f:
            for o in csv.DictReader(f):
                ii = idx.get(o["row"])
                if not ii or any(rows[i][2] != o["was"] for i in ii):
                    raise SystemExit("Talk: ведомость не легла: %s (в .BIN %r)" % (o["row"], rows[ii[0]][2] if ii else None))
                for i in ii:
                    rows[i] = (rows[i][0], rows[i][1], o["now"]); n_ov += 1
        print("  ВЕДОМОСТЬ Talk                    наложено %d" % n_ov)
    os.makedirs(os.path.join(OUT, "Dialogue"), exist_ok=True)
    with open(os.path.join(OUT, "Dialogue", "Talk.csv"), "w", encoding="utf-8-sig", newline="") as f:
        w = csv.writer(f); w.writerow(["Name", "npc", "line", "text"])
        for idx, (base, li, tx) in enumerate(rows):
            w.writerow([str(idx), base, li, tx])   # RowName = глобальный idx (уникален, ничего не теряем)
    print("  OK Talk (диалоги)            cat=Dialogue  записей=%d" % len(rows))
    return len(rows)

# Русифицированные таблицы (cp1251); остальное — cp949 (англ ASCII + непереведённый корейский).
# Кодировку НЕЛЬЗЯ задавать списком файлов: после переноса русского текста почти все таблицы
# стали CP1251, но в них остались непереведённые корейские строки (CP949). Поэтому кодировка
# определяется ПО СОДЕРЖИМОМУ каждой строки (langdet), а не по имени файла.
# Определитель языка — общий `AstoryClient/Tools/Translate/tools/langdet.py`, подгружается лениво в `asda_bintable.pick_enc`.

def enc_for(fn):
    return "auto"

def pick_enc(raw):
    """'cp949' для корейских строк, иначе 'cp1251' (без langdet — всегда cp1251, как было)."""
    return _BT.pick_enc(raw, on_missing="cp1251")

def read_val(rec, off, typ, enc, maxlen=0):
    try:
        if typ == 'i': return struct.unpack_from('<i', rec, off)[0]
        if typ == 'h': return struct.unpack_from('<h', rec, off)[0]
        if typ == 'b': return rec[off]
        if typ == 's':
            # ★ maxlen: ФИКСИРОВАННАЯ длина char[N] в записи. Без него строка, занявшая поле ЦЕЛИКОМ (без
            #   завершающего NUL), утекала в соседние поля. Пример-баг: npc_list id=81 «Сломанный собиратель
            #   живых камне» = ровно 32 байта → в CSV попадало «...камне81NPC» (хвост следующих полей).
            return decode(rec[off:off + maxlen] if maxlen else rec[off:], enc)
    except Exception: return ""
    return ""

# ★ 09.10.2026: таблицы, чей файл в RU-клиенте подменён чужим — читаются из EN-оригинала той же сборки
#   (`@@ASTORY_DEV_ROOT@@\Donors\AsdaChroniclesInterlude`, id/раскладка 1:1); русский текст даёт ведомость значений (OVERLAYS).
SOURCE_OVERRIDE = {
    "Tutorial_middle_category.bin": r"@@ASTORY_DEV_ROOT_FWD@@/Donors/AsdaChroniclesInterlude/Data/Tutorial_middle_category.bin",
}

def extract(fn, cat, stride, layoutB, fields):
    path = SOURCE_OVERRIDE.get(fn) or os.path.join(DATA, fn)
    if not os.path.isfile(path): return None, "нет файла"
    d = open(path, "rb").read()
    if len(d) < 20: return None, "мал"
    enc = enc_for(fn)
    rows = []
    # count беззнаковый (u32@4, в раскладке B — u32@0x14), неполная запись обрывает обход — как было
    for i, rec in _BT.iter_records(d, stride, layout_b=layoutB,
                                   count=_BT.record_count(d, layoutB, signed=False)):
        row = {f[0]: read_val(rec, f[1], f[2], enc, f[3] if len(f) > 3 else 0) for f in fields}
        # пропускаем пустые (нет ни одной непустой строки)
        if any(isinstance(row[f[0]], str) and row[f[0]].strip() for f in fields if f[2]=='s'):
            rows.append(row)
    return rows, None

# ★ МЕЛК-3 ревизии six_items (09.10.2026): ВЕДОМОСТИ ЗНАЧЕНИЙ поверх разбора .BIN — те же файлы, что у сборщиков DT
#   (закон itemdb-ledger-overlays: правка живёт в ведомости и переживает перегенерацию; второй копии правок нет).
#   Имя таблицы (без расширения) → (ведомость, {колонка ведомости: поле разбора}). Строка ведомости `row` = поле `id` записи.
#   Формат ведомости: row,column,was,now,source,reason. `was` — сырое значение .BIN: не совпало (или нет записи/поля) →
#   ни одна таблица не записывается (гейт ДО записи, как у `asda_build_fishbook_dt.apply_overlay`).
_OVERLAY_DIR = os.path.join(os.path.dirname(OUT), "Data", "_value_overlay")   # RawAssets/Data/_value_overlay
OVERLAYS = {
    # Имена томов 5–8 книги рыбака: в клиенте MINE — байты 0x3F («????»); решение владельца 09.10.2026 п. 1.
    #   Та же ведомость, что у DT_FishBook (`asda_build_fishbook_dt.py`), колонка Name → поле разбора `name` (@+8).
    "Fish_collection_data": (os.path.join(_OVERLAY_DIR, "FishBook.csv"), {"Name": "name"}),
    # ★ 09.10.2026 (мультиязычность, этап 0): RU-имена/описания рейд-квестов (Фантазиум @+12/@+132, сверено с EN ORIG)
    #   и RU-названия разделов руководства (перевод корейского TRC оригинала).
    "Raid_quest_data": (os.path.join(_OVERLAY_DIR, "Raid_quest_data.csv"), {"name": "name", "desc": "desc"}),
    "Tutorial_middle_category": (os.path.join(_OVERLAY_DIR, "Tutorial_middle_category.csv"), {"text": "text"}),
    # ★ 14.10.2026 (names11, этап 1): перевод корейского текста, оставшегося в RU-сборке (решение владельца 14.10): названия товаров
    #   премиум-магазина, родов, имена NPC питомцев/рыбалки. `was` = корейское значение .BIN, `now` = русский перевод.
    "Spremium": (os.path.join(_OVERLAY_DIR, "Spremium.csv"), {"name": "name"}),
    "NFT": (os.path.join(_OVERLAY_DIR, "NFT.csv"), {"family": "family"}),
    "SNQInfo": (os.path.join(_OVERLAY_DIR, "SNQInfo.csv"), {"name": "name"}),
    # ★ 14.10.2026 (names11, этап 4): названия цепочек заданий, оставшиеся английскими в русской сборке (`was` = значение .BIN вместе с кавычками).
    "Episode_Table": (os.path.join(_OVERLAY_DIR, "Episode_Table.csv"), {"name": "name"}),
}

def apply_overlay(name, rows):
    """Наложить ведомость таблицы `name` на её разбор `rows` (список словарей) на месте.
    Возврат (наложено, [ошибки]); у таблицы без ведомости — (0, [])."""
    spec = OVERLAYS.get(name)
    if not spec:
        return 0, []
    path, colmap = spec
    if not os.path.exists(path):
        return 0, ["ведомость %s не найдена" % path]
    by_id = {}
    for r in rows:
        by_id.setdefault(str(r.get("id")), []).append(r)    # ★ 14.10.2026 (names11, этап 4): в SNQInfo.Bin есть записи с одинаковым id - правка ложится на ВСЕ
    n, bad = 0, []
    with open(path, "r", encoding="utf-8-sig", newline="") as f:
        for o in csv.DictReader(f):
            fld = colmap.get(o["column"])
            recs = by_id.get(str(int(o["row"])))
            if fld is None or not recs or any(fld not in rec for rec in recs):
                bad.append("%s строка %s колонка %s: нет в разборе" % (name, o["row"], o["column"]))
                continue
            for rec in recs:
                if str(rec[fld]) != o["was"]:
                    bad.append("%s строка %s %s: в .BIN «%s», ведомость ждёт «%s»" % (name, o["row"], fld, rec[fld], o["was"]))
                    continue
                rec[fld] = o["now"]
                n += 1
    return n, bad

def main():
    # ★ six_items (09.10.2026): `--only <Имя>` — перегенерация ОДНОЙ таблицы (точечная запись; прочие CSV не трогаются).
    only = None
    if "--only" in sys.argv:
        i = sys.argv.index("--only")
        only = sys.argv[i + 1] if i + 1 < len(sys.argv) else ""
    total = 0
    by_cat = {}
    # Проход 1: разбор и ведомости В ПАМЯТИ; при любой ошибке ведомости ничего не пишется.
    staged = []   # (имя, категория, поля, записи)
    bad_all = []
    for fn, cat, stride, lb, fields in TABLES:
        name = os.path.splitext(os.path.basename(fn))[0]
        if only is not None and name != only: continue
        rows, err = extract(fn, cat, stride, lb, fields)
        if err: print("  SKIP %-28s (%s)" % (name, err)); continue
        if not rows:  # пустая таблица в этой сборке (напр. Fpremium: файл 20б, count=0) — CSV/ассет не создаём
            print("  EMPTY %-26s (0 записей — пропуск)" % name); continue
        n_ov, bad = apply_overlay(name, rows)
        if n_ov or bad:
            print("  ВЕДОМОСТЬ %-22s наложено %d, ошибок %d%s" % (name, n_ov, len(bad), ("" if not bad else ": " + "; ".join(bad))))
        bad_all += bad
        staged.append((name, cat, fields, rows))
    if bad_all:
        print("=== СТОП: ведомость не легла (%d ошибок) — ни один CSV не записан" % len(bad_all))
        return
    for name, cat, fields, rows in staged:
        os.makedirs(os.path.join(OUT, cat), exist_ok=True)
        csvp = os.path.join(OUT, cat, name + ".csv")
        cols = ["Name"] + [f[0] for f in fields]   # UE DataTable требует 1й столбец = RowName
        with open(csvp, "w", encoding="utf-8-sig", newline="") as f:
            w = csv.writer(f); w.writerow(cols)
            for idx, r in enumerate(rows):
                w.writerow([str(r.get("id", idx))] + [r[fld[0]] for fld in fields])
        print("  OK %-28s cat=%-9s записей=%-5d -> %s" % (name, cat, len(rows), csvp))
        total += len(rows); by_cat[cat] = by_cat.get(cat, 0) + len(rows)
    if only is None:
        tk = extract_talk(); total += tk; by_cat["Dialogue"] = tk
    print("=== ИТОГО записей=%d по категориям=%s" % (total, by_cat))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
