# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
extract_text_lang.py — ОФЛАЙН-генератор языковых оверлеев данных (мультиязычность RU+EN, этап 2 плана
`Reports/owner_1009/multilang_plan.md` §2.2-А, §3, §8). [ПОРТ] 09.10.2026 (мультиязычность).

Запуск (из каталога AstoryClient):
    set PYTHONIOENCODING=utf-8 && python Tools\extract_text_lang.py --lang en --out RawAssets\Text\L10N\en
    ... --only GameText,item_list      — только перечисленные таблицы оверлея (отладка)

Что делает:
  * для КАЖДОЙ таблицы оверлея из сводки ключей `Reports/owner_1009/multilang/stage1_keys.md` (+ GameText,
    loose-таблицы `FAsdaText::Get`, Talk) берёт RU-строки из ИСХОДНИКОВ RU-ассетов (CSV/JSON в RawAssets), строит
    ключ оверлея по правилам сводки и ищет EN по цепочке источников;
  * источники EN и допуски — ГОТОВЫЕ читатели `multilang/scripts/build_coverage.py` (B()/L()/P(), SECONDARY_OK,
    same_entity, src_*) и `ml_common.py` — импортируются, логика не копируется;
  * после извлечения накладывает ведомости правок `Tools/data/loc/<lang>/<Таблица>.json` (сверка `was`; не совпало —
    СТОП, ничего не пишется);
  * пишет ТОЛЬКО в --out: `DT_Loc_<Таблица>_<lang>.csv` (UTF-8 BOM, CRLF, `Name,Text`), `_sources_<Таблица>.tsv`,
    `_gaps_<Таблица>.tsv`, `_report.md`. Боевые каталоги RawAssets/Text, RawAssets/Data, Content НЕ трогает
    (урок itemdb-full-decode-into-live-dir). Ведомости создаёт только при первом запуске (если файла нет).

Импорт модуля без побочных эффектов (закон tools-no-import-side-effects): читатели multilang подгружаются лениво
в `_libs()`, всё действие — в `main()` под `if __name__ == "__main__"`.
"""
import os
import sys
import io
import re
import csv
import json
import struct
import argparse
import collections
import contextlib

# Каталог этого инструмента (AstoryClient\Tools).
TOOLS = os.path.dirname(os.path.abspath(__file__))
# Корень проекта UE (AstoryClient).
PORT = os.path.dirname(TOOLS)
# Исходники RU-ассетов (только чтение).
RAW = os.path.join(PORT, "RawAssets")
# Исходники ItemDB (RowName = колонка `_row`).
ITEMDB = os.path.join(RAW, "Data", "ItemDB")
# Исходники loose-таблиц `FAsdaText::Get` и GameText.
TEXT = os.path.join(RAW, "Text")
# Готовые читатели клиентов и цепочки источников (этап исследования multilang).
ML_SCRIPTS = r"@@ASTORY_DEV_ROOT@@\Reports\owner_1009\multilang\scripts"
# EN для Tutorial_middle_category (этап 0; оригинал корейский во всех клиентах).
STAGE0_TUTORIAL_MC = r"@@ASTORY_DEV_ROOT@@\Reports\owner_1009\multilang\stage0\tutorial_middle_category.tsv"
# Генерированный исходник DT_NpcNames / DT_MobNames (RowName: NpcId / строка Mob list).
GEN_NPCNAMES = os.path.join(TOOLS, "generated", "DT_NpcNames.csv")
GEN_MOBNAMES = os.path.join(TOOLS, "generated", "DT_MobNames.csv")

csv.field_size_limit(10 ** 9)

# ---------------------------------------------------------------------------------------------
# Ленивые импорты (без побочных эффектов при импорте этого модуля)
# ---------------------------------------------------------------------------------------------
_L = {}


def _libs():
    """Подгрузить build_coverage (BC), ml_common (M), decode_item_db (D) и разбор званий (WS) один раз."""
    if not _L:
        for p in (ML_SCRIPTS, TOOLS):
            if p not in sys.path:
                sys.path.insert(0, p)
        with contextlib.redirect_stdout(io.StringIO()):
            import ml_common as M                 # noqa: E402
            import build_coverage as BC           # noqa: E402
            import decode_item_db as D            # noqa: E402  (подтянут ml_common из @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\ItemDB)
            import asda_import_war_shop as WS     # noqa: E402  (parse_rank — CampWar_HonorTable.BIN)
            import items_common as IC             # noqa: E402  (load_tooltip — полная расшифровка описаний)
        _L.update(M=M, BC=BC, D=D, WS=WS, IC=IC)
    return _L["M"], _L["BC"], _L["D"], _L["WS"]


def _quiet(fn):
    """Вызов загрузчика источника с подавлением его печати; исключение -> ('ОШИБКА …', {})."""
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            return fn()
    except Exception as e:          # загрузчик сломан/нет файла — источник просто пуст
        return "ОШИБКА %s" % e, {}


# ---------------------------------------------------------------------------------------------
# Строки и проверки
# ---------------------------------------------------------------------------------------------
# Литерал «\n» (два символа) — так перевод строки хранится в CSV-источниках GameText/ItemDB.
BSN = "\\n"

# printf-спецификатор (как в форматтере порта): %% и %[флаги][ширина][.точность][модификатор]тип. Флаг «пробел»
#   не допускается: в текстах «70% capacity», «% of» — это знак процента, а не спецификатор.
_PF = re.compile(r"%(%|[-+#0]*\d*(?:\.\d+)?(?:hh|h|ll|l|I64|I32)?([diouxXfeEgGcs]))")
# Маркеры общей подстановки `[ssN]`/`[sdN]` (memory loc-string-placeholders).
_MK = re.compile(r"\[(?:ss|sd|sf)\d+\]")
# Таблицы, где строка идёт в printf-форматтер: конфликт спецификаторов = строка НЕ пишется.
PRINTF_TABLES = {"GameText", "itemoptiontable", "petoptiontable"}


def printf_sig(s):
    """Последовательность типов printf-спецификаторов строки (без `%%`, он аргумент не потребляет)."""
    return [m.group(2) for m in _PF.finditer(s or "") if m.group(1) != "%"]


def marker_sig(s):
    """Мультимножество маркеров [ssN]/[sdN] строки (порядок у именованных маркеров не важен)."""
    return sorted(_MK.findall(s or ""))


def spec_conflict(ru, en):
    """Текст конфликта спецификаторов RU↔EN или '' (число/порядок %-типов, набор маркеров)."""
    a, b = printf_sig(ru), printf_sig(en)
    if a != b:
        return "printf RU=%s EN=%s" % ("".join(a) or "-", "".join(b) or "-")
    a, b = marker_sig(ru), marker_sig(en)
    if a != b:
        return "маркеры RU=%s EN=%s" % (",".join(a) or "-", ",".join(b) or "-")
    return ""


def nl_mode(values):
    """Как RU-источник таблицы хранит перевод строки: 'lit' (`\\n` двумя символами), 'real', 'mixed', 'none'."""
    real = any(("\n" in v) or ("\r" in v) for v in values)
    lit = any(BSN in v for v in values)
    if real and lit:
        return "mixed"
    return "real" if real else ("lit" if lit else "none")


def norm_nl(en, mode):
    """Привести переводы строк EN к виду RU-источника: при 'lit' и 'none' настоящий CR/LF -> литерал `\\n`."""
    if mode in ("lit", "none") and ("\n" in en or "\r" in en):
        return en.replace("\r\n", BSN).replace("\n", BSN).replace("\r", BSN)
    return en


def tsv(s):
    """Строка для TSV-отчёта: табы и переводы строк — видимыми последовательностями."""
    return (s or "").replace("\t", "\\t").replace("\r", "\\r").replace("\n", "\\n")


# ---------------------------------------------------------------------------------------------
# Описание таблицы оверлея
# ---------------------------------------------------------------------------------------------
class Row(object):
    """Строка RU-ассета, требующая EN: ключ оверлея, RU, ключ поиска в источниках и поле источника."""
    __slots__ = ("key", "ru", "lk", "fld", "pos")

    def __init__(self, key, ru, lk, fld, pos=None):
        self.key = key      # RowName строки оверлея (`<RowName>.<Поле>` / `<id>` / `<npc>:<line>`)
        self.ru = ru        # RU-строка ассета (для сравнения и проверок)
        self.lk = lk        # ключ записи в картах источников (id / код / (файл, №) / 'P|nsr|tid')
        self.fld = fld      # имя поля в картах источников
        self.pos = pos      # позиция записи (ItemDB `_row`) — для позиционного ORIG


class Table(object):
    """Таблица оверлея: имя, загрузчик RU-строк, цепочка EN-источников и правила допуска."""

    def __init__(self, name, rows, chain, bc_name=None, entity=False, write_empty_ru=False, note="",
                 secondary_filter=None):
        self.name = name                    # имя таблицы оверлея (DT_Loc_<name>_<lang>)
        self.rows = rows                    # () -> [Row]
        self.chain = chain                  # () -> [(код, getter(Row) -> bytes|str|None)] по приоритету
        self.bc_name = bc_name              # имя таблицы в build_coverage (SECONDARY_OK / same_entity)
        self.entity = entity                # проверять паспорт предмета (same_entity) у вторичных сборок
        self.write_empty_ru = write_empty_ru  # писать EN и при пустом RU (soulmateskilltable, §4 сводки)
        self.note = note                    # пояснение для отчёта
        self.secondary_filter = secondary_filter  # (Row) -> bool: можно ли вторичные источники для этой строки


# ---------------------------------------------------------------------------------------------
# Источники
# ---------------------------------------------------------------------------------------------
def map_getter(m):
    """getter по карте источника {lk: {fld: bytes}}."""
    return lambda r: m.get(r.lk, {}).get(r.fld)


def chain_from(bc_chain, skip=()):
    """Цепочка getter-ов из EN-цепочки build_coverage [(код, loader)] (loader -> (status, карта))."""
    out = []
    for code, fn in bc_chain:
        if code in skip:
            continue
        st, m = _quiet(fn)
        out.append((code, map_getter(m)))
    return out


def bbz_keys_by_pos(entry, code, keycol):
    """[ключ записи по позиции] таблицы Bin.bbz сборки code — по реестру decode_item_db (тот же разбор, что
    `ml_common.decode_bbz_raw`, но без схлопывания дублей ключа: нужен для позиционного сопоставления `_row`)."""
    M, BC, D, _ = _libs()
    e = BC.bbz(code)
    data = e.get(entry) if e else None
    if data is None:
        return []
    spec = D.REGISTRY[entry]
    stride, md5, dec = spec["stride"], spec.get("md5", 0), spec.get("dec")
    off, typ = M.reg_cols(entry)[keycol]
    n = struct.unpack_from("<i", data, 4)[0]
    body = data[20:]
    if n <= 0 or n * stride > len(body):
        return []
    out = []
    for i in range(n):
        b = bytearray(body[i * stride + md5:(i + 1) * stride])
        if dec:
            try:
                dec(b, i)
            except Exception:
                pass
        if typ.startswith("s"):
            out.append(bytes(M.BT.cstr_bytes(b, off, int(typ[1:]))))
        else:
            out.append(D.rd(b, off, typ))
    return out


def itemdb_orig_getter(entry, keycol, fields, keyconv):
    """ORIG для ItemDB: запись С ТОЙ ЖЕ ПОЗИЦИЕЙ (`_row`), если её ключ = ключ строки порта; иначе первая запись ORIG
    с тем же ключом (сборка та же, id 1:1). Так дубли ключа получают EN своей записи, а не первой."""
    M, BC, D, _ = _libs()
    keys = bbz_keys_by_pos(entry, "ORIG", keycol) if keycol != "_row" else None
    st, bypos = _quiet(lambda: BC.src_bbz(entry, "_row", fields, "ORIG"))
    if keycol == "_row":
        return lambda r: bypos.get(r.pos, {}).get(r.fld)
    st, bykey = _quiet(lambda: BC.src_bbz(entry, keycol, fields, "ORIG"))

    def g(r):
        if r.pos is not None and keys and r.pos < len(keys) and keys[r.pos] == r.lk:
            return bypos.get(r.pos, {}).get(r.fld)
        return bykey.get(r.lk, {}).get(r.fld)
    return g


# ---------------------------------------------------------------------------------------------
# RU-строки (исходники RU-ассетов)
# ---------------------------------------------------------------------------------------------
def read_csv(path):
    """CSV исходника RU-ассета -> список dict (UTF-8 с BOM или без)."""
    with open(path, encoding="utf-8-sig", newline="") as f:
        return list(csv.DictReader(f))


def read_json(path):
    """JSON исходника RU-ассета."""
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def keyconv_for(keycol):
    """Преобразование значения ключевой колонки CSV в ключ карт источников (как build_coverage._keyconv_for)."""
    if keycol == "Tag":
        return lambda v: v.encode("cp1251", "replace")
    if keycol == "_row":
        return int
    return int


def itemdb_rows(csvn, keycol, fields):
    """Строки ItemDB: ключ оверлея `<_row>.<Поле>`, поиск по ключевой колонке, позиция = `_row`."""
    conv = keyconv_for(keycol)

    def f():
        out = []
        for r in read_csv(os.path.join(ITEMDB, csvn)):
            p = int(r["_row"])
            lk = p if keycol == "_row" else conv(r[keycol])
            for fl in fields:
                out.append(Row("%d.%s" % (p, fl), r.get(fl, ""), lk, fl, p))
        return out
    return f


# ---------------------------------------------------------------------------------------------
# Реестр таблиц оверлея
# ---------------------------------------------------------------------------------------------
# ItemDB (§1 сводки): (оверлей, запись Bin.bbz, CSV, ключевая колонка, поля, XYT-двойник, имя в build_coverage)
ITEMDB_SPECS = [
    ("item_list", "item_list.bin", "item_list.csv", "ItemCode", ["ItemName"], None),
    ("item_tooltip", "item_tooltip.bin", "item_tooltip.csv", "ItemCode", ["Description"], None),
    ("sowell_list", "sowell_list.bin", "sowell_list.csv", "Id", ["Name"], None),
    ("sowell_tooltip", "sowell_tooltip.bin", "sowell_tooltip.csv", "Id", ["Text"], None),
    ("special_itemlist", "special_itemlist.bin", "special_itemlist.csv", "Code", ["Name"], None),
    ("specialitem_tooltiplist", "specialitem_tooltiplist.bin", "specialitem_tooltiplist.csv", "Id", ["Name", "Tooltip"], None),
    ("title_list", "title_list.bin", "title_list.csv", "Id", ["Name", "Desc"], "Data/NPC/title_list.BIN.en"),
    ("title_option", "title_option.bin", "title_option.csv", "Id", ["OptionStatName"], None),
    ("pettable", "pettable.bin", "pettable.csv", "Id", ["Name"], None),
    ("petexptable", "petexptable.bin", "petexptable.csv", "Level", ["StageName"], None),
    ("petoptiontable", "petoptiontable.bin", "petoptiontable.csv", "Id", ["Format"], None),
    ("skilltooltip", "skilltooltip.bin", "skilltooltip.csv", "Id", ["Name", "Text"], "Data/NPC/Skilltooltip.BIN.en"),
    ("skill_info", "skill_info.sif", "skill_info.csv", "SkillCode", ["Name"], None),
    ("jobtype", "jobtype.bin", "jobtype.csv", "_row", ["JobName", "ParentJob"], None),
    ("stateinfotip", "stateinfotip.bin", "stateinfotip.csv", "Id", ["Name", "Text"], None),
    ("soulstateinfotip", "soulstateinfotip.bin", "soulstateinfotip.csv", "Id", ["Name", "Text"], None),
    ("emotiontip", "emotiontip.bin", "emotiontip.csv", "Tag", ["Name"], None),
    ("itemoptiontable", "itemoptiontable.bin", "itemoptiontable.csv", "OptionId", ["FmtString"], None),
    ("hairtable", "hairtable.bin", "hairtable.csv", "Id", ["Name", "Description"], None),
    ("soulmateskilltable", "soulmateskilltable.bin", "soulmateskilltable.csv", "Id", ["DisplayName", "Text"], None),
    ("soulmateskill", "soulmateskill.ssi", "soulmateskill.csv", "Id", ["Name"], None),
    ("soulsoulmatetext", "soulsoulmatetext.bin", "soulsoulmatetext.csv", "_row", ["Text"], None),
    ("loginhelpertext", "loginhelpertext.bin", "loginhelpertext.csv", "_row", ["Text"], None),
]

# Loose-таблицы `FAsdaText::Get` (имя оверлея = имя CSV): (оверлей, путь CSV от RawAssets/Text, поля).
LOOSE_SPECS = [
    ("npc_list", "NPC/npc_list.csv", ["name"]),
    ("Episode_Table", "Quest/Episode_Table.csv", ["name"]),
    ("Raid_quest_data", "Quest/Raid_quest_data.csv", ["name", "desc"]),
    ("SNQInfo", "Quest/SNQInfo.csv", ["name", "subtitle"]),
    ("NFT", "NPC/NFT.csv", ["family"]),
    ("TIM_List", "World/TIM_List.csv", ["village"]),
    ("Constellation", "World/Constellation.csv", ["name"]),
    ("SoulGuarderTable", "Skill/SoulGuarderTable.csv", ["name"]),
    ("Fish_collection_data", "Fishing/Fish_collection_data.csv", ["name"]),
    ("Tutorial_text", "Tutorial/Tutorial_text.csv", ["text"]),
    ("Tutorial_text_answer", "Tutorial/Tutorial_text_answer.csv", ["text"]),
    ("Tutorial_middle_category", "Tutorial/Tutorial_middle_category.csv", ["text"]),
    ("Bpremium", "Shop/Bpremium.csv", ["name"]),
    ("Spremium", "Shop/Spremium.csv", ["name"]),
]

# Ключи, EN которых найден, но по смыслу ЧУЖОЙ (проверено вручную 09.10.2026): (таблица, ключ) -> причина (в пропуски).
EXCLUDE_KEYS = {
    ("GameText", "7061"): "ORIG «.»; TXT (старый error list.txt) — другое сообщение (памятный день ↔ RU новогоднее "
                          "поздравление Фантазиума)",
}

# Таблицы, которые генератор СОЗНАТЕЛЬНО не строит (для отчёта): имя -> причина.
NOT_BUILT = {
    "item_upgrade_cost": "нет точки чтения в коде (сводка §1, «оверлей пока не нужен»)",
    "event_item_list": "нет точки чтения в коде (сводка §1)",
    "specialitem_buftooltiplist": "нет точки чтения в коде (сводка §1)",
    "RaidQuestData": "в RU-ассете поле Name = QuestId (\"20000\"), текста нет; имя рейда идёт через loose `Raid_quest_data`",
    "tex / Clan_*": "не текст для игрока (решение ведущего)",
    "kor_ru_ui (подписи UI)": "этап 4 (ST_UI), не оверлей данных",
}


def build_tables():
    """Список всех таблиц оверлея (порядок = порядок отчёта)."""
    M, BC, D, WS = _libs()
    T = []

    # --- GameText: ключ `<id>`, EN — error list ORIG + допущенные вторичные ---
    gt_bc = next(t for t in BC.TABLES if t["name"] == "GameText (error list)")

    def gt_rows():
        return [Row(r["Key"], r["SourceString"], int(r["Key"]), "text") for r in read_csv(os.path.join(TEXT, "GameText.csv"))]
    # ★ вторичные (XYT.en/TXT/GLB/WOD) — только для id, которые есть в error list нашей сборки (MINE). Свои id порта
    #   (990xxx, 200000…, 900001…, 115300…) в чужих сборках означают ДРУГОЕ (200000: RU «Удалить» ↔ GLB «Novice»).
    _mine_ids = {}

    def gt_secondary_ok(r):
        if not _mine_ids:
            st, m = _quiet(lambda: BC.src_errlist("MINE"))
            _mine_ids.update({k: True for k in m})
        return r.lk in _mine_ids
    T.append(Table("GameText", gt_rows, lambda: chain_from(gt_bc["chains"][0]), bc_name=gt_bc["name"],
                   secondary_filter=gt_secondary_ok,
                   note="вторичные источники — только для id error list нашей сборки (свои id порта — только ORIG)"))

    # --- loose-таблицы FAsdaText::Get: ключ `<Name>.<поле>` ---
    for tn, rel, flds in LOOSE_SPECS:
        def lrows(rel=rel, flds=flds, tn=tn):
            out = []
            for r in read_csv(os.path.join(TEXT, rel)):
                lk = (int(r["id"]) & 0xFFFF) if tn == "SNQInfo" else int(r["id"])
                for f in flds:
                    out.append(Row("%s.%s" % (r["Name"], f), r.get(f, ""), lk, f))
            return out
        T.append(Table(tn, lrows, (lambda tn=tn: chain_from(BC.L(tn)[0])), bc_name="Text/" + rel))

    # --- Map_info (loose) и MapTable.DisplayName: показываемое имя карты из GameText/TIM ORIG ---
    def mi_rows():
        return [Row("%s.name" % r["Name"], r["name"], int(r["id"]), "name") for r in read_csv(os.path.join(TEXT, "World", "Map_info.csv"))]
    T.append(Table("Map_info", mi_rows, lambda: [("ORIG", map_name_getter())], bc_name="Text/World/Map_info.csv",
                   note="EN-оригинал Map_info — внутренние имена (LostValley), не берутся; имя карты — из GameText ORIG "
                        "по совпадению RU-текста (наименьший id), затем «[тег] имя»"))

    def mt_rows():
        # RU MapTable.DisplayName = поле `display` реестра карт (внутреннее имя каталога); показ — через Map_info.
        reg = read_json(os.path.join(TOOLS, "data", "asda_map_registry.json"))
        mi = {int(r["id"]): r["name"] for r in read_csv(os.path.join(TEXT, "World", "Map_info.csv"))}
        out = []
        for m in reg["maps"]:
            out.append(Row("%d.DisplayName" % m["id"], m["display"], m["id"], "name"))
            out[-1].pos = mi.get(m["id"], "")        # RU-имя карты (Map_info) для сопоставления
        return out
    T.append(Table("MapTable", mt_rows, lambda: [("ORIG", map_name_getter(by_pos_ru=True))], bc_name="Text/World/Map_info.csv",
                   note="RU DisplayName — внутреннее имя каталога (латиница); EN = показываемое имя карты, как у Map_info"))

    # --- Talk: ключ `<npc>:<line>`, line -> номер ЗАПИСИ файла через непустые записи MINE (сводка §4) ---
    talk_bc = next(t for t in BC.TABLES if t["name"] == "Text/Dialogue/Talk.csv")

    def talk_rows():
        mine = M.read_talk("MINE")
        byfile = collections.defaultdict(list)
        for (base, i), v in sorted(mine.items()):
            if v.strip():
                byfile[base].append(i)
        out = []
        for r in read_csv(os.path.join(TEXT, "Dialogue", "Talk.csv")):
            base = r["npc"].lower()
            li = int(r["line"])
            lst = byfile.get(base, [])
            lk = (base, lst[li]) if li < len(lst) else None
            out.append(Row("%s:%s" % (r["npc"], r["line"]), r["text"], lk, "text"))
        return out
    T.append(Table("Talk", talk_rows, lambda: chain_from(talk_bc["chains"][0]), bc_name=talk_bc["name"]))

    # --- ItemDB (§1) ---
    for ov, entry, csvn, keycol, flds, xl in ITEMDB_SPECS:
        bcn = "ItemDB/" + csvn

        def ichain(entry=entry, keycol=keycol, flds=flds, xl=xl, csvn=csvn):
            en, ar, ru = BC.B(entry, keycol, flds, xl)
            if entry == "item_tooltip.bin":
                # ★ описание предмета — читателем items_common.load_tooltip (CHRON = ORIG): реестровый dec
                #   decode_item_db сдвигает «байт−1» не на всю длину, и у 3764 описаний ORIG хвост остаётся
                #   зашифрованным («…this must be sometijoh!wfsz!qpxfsgvm/»). load_tooltip режет по терминатору 0x01
                #   и расшифровывает строку целиком. Ключ — ItemCode (дубли кода — хвостовые строки порта, их в ORIG нет).
                tt, _st = _quiet(lambda: _L["IC"].load_tooltip("CHRON"))
                tt = tt if isinstance(tt, dict) else {}
                ch = [("ORIG", (lambda r, tt=tt: tt.get(r.lk)))]
            else:
                ch = [("ORIG", itemdb_orig_getter(entry, keycol, flds, keyconv_for(keycol)))]
            # ★ предметы (item_list/item_tooltip): только ORIG и TOR (решение владельца «EN по коду из ORIG»). WOD
            #   (ветка Asda 2) build_coverage допускал при совпадении паспорта-меша, но выборка показала чужой смысл
            #   (55504: RU «костюм снеговика» ↔ WOD «Event Package 3005») — такие строки уходят в пропуски.
            ch += chain_from(en, skip=("ORIG", "WOD") if "ItemDB/" + csvn in BC.ITEM_TABLES else ("ORIG",))
            if csvn == "soulmateskill.csv":     # отображаемое имя того же Id — из soulmateskilltable ORIG (как в BC)
                st, m = _quiet(lambda: BC._derived("soulmateskilltable.bin", "Id", "DisplayName", "Name", "ORIG"))
                ch.append(("ORIG.soulmateskilltable", map_getter(m)))
            return ch
        T.append(Table(ov, itemdb_rows(csvn, keycol, flds), ichain, bc_name=bcn,
                       entity=bcn in BC.ITEM_TABLES, write_empty_ru=(ov == "soulmateskilltable")))

    # making_recipe — в ItemDB-CSV, источник loose `making_recipe.BIN` (ключ Id) — как в build_coverage
    def mr_rows():
        return [Row("%s.RecipeName" % r["_row"], r["RecipeName"], int(r["Id"]), "name", int(r["_row"]))
                for r in read_csv(os.path.join(ITEMDB, "making_recipe.csv"))]
    T.append(Table("making_recipe", mr_rows, lambda: chain_from(BC.L("making_recipe")[0]), bc_name="ItemDB/making_recipe.csv"))

    # --- §2: прочие DataTable ---
    # NpcNames: RowName = NpcId, EN = npc_list ORIG по id
    def npcn_rows():
        return [Row("%s.Name" % r["---"], r["Name"], int(r["---"]), "name") for r in read_csv(GEN_NPCNAMES)]
    T.append(Table("NpcNames", npcn_rows, lambda: chain_from(BC.L("npc_list")[0]), bc_name="Text/NPC/npc_list.csv",
                   note="RowName — Tools/generated/DT_NpcNames.csv (= NpcId); EN — npc_list ORIG по id"))

    # MobNames: RowName = строка (MobTypeId − 1) = позиция записи Mob list; EN — Mob list ORIG по id записи
    def mobn_rows():
        data = open(os.path.join(RAW, "Data", "Mob", "Mob_list.bin"), "rb").read()
        recs, cnt, st, status = M.parse_loose(data, 272, False)
        ids = [struct.unpack_from("<i", r, 0)[0] for r in recs]
        out = []
        for r in read_csv(GEN_MOBNAMES):
            p = int(r["---"])
            out.append(Row("%d.Name" % p, r["Name"], ids[p] if p < len(ids) else None, "name", p))
        return out
    T.append(Table("MobNames", mobn_rows, lambda: chain_from(BC.L("Mob_list")[0]),
                   bc_name="Data/Mob/Mob_list.bin (имена мобов)",
                   note="RowName — Tools/generated/DT_MobNames.csv (позиция записи Mob list); EN — Mob list ORIG по id записи"))

    # EpisodeTable: RowName = EpisodeId, Title; EN — Episode_Table ORIG по Index (= id loose-таблицы)
    def ep_rows():
        # ★ поиск по QuestKey (u16 +236 записи = ключ loose-таблицы), НЕ по Index: после переноса Флэммио (466→486
        #   записей) позиция и ключ расходятся с 411-й записи
        return [Row("%s.Title" % r["Name"], r["Title"], int(r["QuestKey"]), "name")
                for r in read_json(os.path.join(RAW, "Data", "NPC", "EpisodeTable.json"))]
    T.append(Table("EpisodeTable", ep_rows, lambda: chain_from(BC.L("Episode_Table")[0]), bc_name="Text/Quest/Episode_Table.csv",
                   note="EN — Episode_Table ORIG по QuestKey (+236) строки"))

    # TimList: RowName = MapId, VillageName; EN — TIM_List ORIG по MapId
    def tim_rows():
        return [Row("%s.VillageName" % r["Name"], r["VillageName"], int(r["MapId"]), "village")
                for r in read_json(os.path.join(RAW, "Data", "NPC", "TimList.json"))]
    T.append(Table("TimList", tim_rows, lambda: chain_from(BC.L("TIM_List")[0]), bc_name="Text/World/TIM_List.csv"))

    # ExtraMenu: RowName = _row, Name; EN — Extra_menu_table ORIG по Cmd (+68)
    def em_rows():
        return [Row("%s.Name" % r["_row"], r["Name"], int(r["Cmd"]), "name", int(r["_row"]))
                for r in read_csv(os.path.join(RAW, "Data", "ExtraMenu.csv"))]
    T.append(Table("ExtraMenu", em_rows, lambda: chain_from(BC.L("Extra_menu_table")[0]),
                   bc_name="Data/ExtraMenu.csv (Extra_menu_table)", note="EN — Extra_menu_table ORIG по Cmd (+68)"))

    # FishBook: RowName = колонка `---`, Name (том); EN — Fish_collection_data ORIG по Tome
    def fb_rows():
        return [Row("%s.Name" % r["---"], r["Name"], int(r["Tome"]), "name")
                for r in read_csv(os.path.join(RAW, "Data", "FishBook.csv"))]
    T.append(Table("FishBook", fb_rows, lambda: chain_from(BC.L("Fish_collection_data")[0]),
                   bc_name="Text/Fishing/Fish_collection_data.csv", note="EN — Fish_collection_data ORIG по Tome"))

    # SoulGuarder: RowName = _row, Name; EN — SoulGuarderTable ORIG по SoulGuardIndex
    def sg_rows():
        return [Row("%s.Name" % r["_row"], r["Name"], int(r["SoulGuardIndex"]), "name", int(r["_row"]))
                for r in read_csv(os.path.join(RAW, "Data", "SoulGuarder.csv"))]
    T.append(Table("SoulGuarder", sg_rows, lambda: chain_from(BC.L("SoulGuarderTable")[0]),
                   bc_name="Text/Skill/SoulGuarderTable.csv", note="EN — SoulGuarderTable ORIG по SoulGuardIndex"))

    # WarRank: RowName = Name (RankIndex), Title; EN — CampWar_HonorTable.BIN ORIG (parse_rank импортёра)
    def wr_rows():
        return [Row("%s.Title" % r["Name"], r["Title"], int(r["RankIndex"]), "Title")
                for r in read_json(os.path.join(RAW, "Data", "Character", "WarRank.json"))]

    def wr_chain():
        def ld():
            data, p = M.read_file("ORIG", r"Data\CampWar_HonorTable.BIN")
            rows = WS.parse_rank(data) or []
            return "n=%d" % len(rows), {r["RankIndex"]: {"Title": r["Title"].encode("cp1251")} for r in rows}
        st, m = _quiet(ld)
        return [("ORIG", map_getter(m))]
    T.append(Table("WarRank", wr_rows, wr_chain, note="RU-ассет уже английский (файл MINE = ORIG побайтно)"))

    # --- .NSR (NpcNsrHeader/Button/Page) и .QST (QuestBook): EN-первоисточник — MINE (не русифицирован) ---
    nsr_bc = next(t for t in BC.TABLES if t["name"].startswith("Data/NPC/NpcNsr"))
    qst_bc = next(t for t in BC.TABLES if t["name"].startswith("Data/Quest/QuestBook"))

    def nh_rows():
        return [Row("%s.Title" % r["Name"], r["Title"], "H|%s" % r["NsrName"].lower(), "text")
                for r in read_json(os.path.join(RAW, "Data", "NPC", "NpcNsrHeader.json"))]

    def nb_rows():
        return [Row("%s.Caption" % r["Name"], r["Caption"], "B|%s|%d" % (r["NsrName"].lower(), r["ButtonIndex"]), "text")
                for r in read_json(os.path.join(RAW, "Data", "NPC", "NpcNsrButton.json"))]

    def np_rows():
        out = []
        for r in read_json(os.path.join(RAW, "Data", "NPC", "NpcNsrPage.json")):
            n, t = r["NsrName"].lower(), r["TalkId"]
            out.append(Row("%s.Text" % r["Name"], r["Text"], "P|%s|%d" % (n, t), "text"))
            for i, s in enumerate(r.get("Steps") or []):
                out.append(Row("%s.Steps.%d.Caption" % (r["Name"], i), s.get("Caption", ""), "S|%s|%d|%d" % (n, t, i), "text"))
        return out

    def qb_rows():
        out = []
        for r in read_json(os.path.join(RAW, "Data", "Quest", "QuestBook.json")):
            fn = r["FileName"].lower()
            out.append(Row("%s.Title" % r["Name"], r["Title"], "T|%s" % fn, "text"))
            out.append(Row("%s.Desc" % r["Name"], r["Desc"], "D|%s" % fn, "text"))
            for i, s in enumerate(r.get("Steps") or []):
                out.append(Row("%s.Steps.%d" % (r["Name"], i), s if isinstance(s, str) else "", "S|%s|%d" % (fn, i), "text"))
        return out

    _nsr_cache = {}

    def nsr_chain():
        if "c" not in _nsr_cache:
            _nsr_cache["c"] = chain_from(nsr_bc["chains"][0])
        return _nsr_cache["c"]
    T.append(Table("NpcNsrHeader", nh_rows, nsr_chain, bc_name=nsr_bc["name"]))
    T.append(Table("NpcNsrButton", nb_rows, nsr_chain, bc_name=nsr_bc["name"]))
    T.append(Table("NpcNsrPage", np_rows, nsr_chain, bc_name=nsr_bc["name"]))
    T.append(Table("QuestBook", qb_rows, lambda: chain_from(qst_bc["chains"][0]), bc_name=qst_bc["name"]))
    return T


# ---------------------------------------------------------------------------------------------
# Паспорт предмета ПОРТА (меши item_img_table) против ORIG
# ---------------------------------------------------------------------------------------------
_PASS = {}


def port_passport_ok():
    """{ItemCode: True/False} — совпадает ли паспорт предмета порта (MeshMale, MeshFemale строки ImgIndex
    `RawAssets/Data/ItemDB/item_img_table.csv`) с паспортом ORIG в той же позиции. В порту item_img_table правлен
    ведомостью `_value_overlay` (335 строк: блок 75000…76185 и др.) — там под кодом ДРУГОЙ предмет, и EN из ORIG
    был бы чужим («Шлем ловкача» ↔ «Pan's Rod»)."""
    if _PASS:
        return _PASS
    _libs()
    img = _L["IC"].load_img("CHRON") or []
    pimg = read_csv(os.path.join(ITEMDB, "item_img_table.csv"))
    for r in read_csv(os.path.join(ITEMDB, "item_list.csv")):
        p = int(r["ImgIndex"])
        ok = p < len(img) and p < len(pimg) and \
            (pimg[p]["MeshMale"].lower(), pimg[p]["MeshFemale"].lower()) == (img[p]["mm"], img[p]["mf"])
        code = int(r["ItemCode"])
        _PASS[code] = _PASS.get(code, True) and ok
    return _PASS


# ---------------------------------------------------------------------------------------------
# Имена карт (Map_info / MapTable)
# ---------------------------------------------------------------------------------------------
_TAG = re.compile(r"^\s*\[[^\]]+\]\s*")


def _nkey(s):
    """Нормализация RU-имени для сопоставления: регистр, ё→е, пробелы."""
    return re.sub(r"\s+", " ", (s or "").replace("ё", "е").replace("Ё", "Е")).strip().casefold()


_MAPNAMES = {}


def map_name_index():
    """{норм. RU-имя карты: (EN, 'GameText#id' | 'GameText#id[тег]')} по GameText порта (RU) и error list ORIG (EN).
    Правило: точное совпадение RU-строки GameText с именем карты → EN ORIG того же id (наименьший id с EN);
    иначе строка вида «[Тег] имя» → EN той же строки без «[Tag] »."""
    if _MAPNAMES:
        return _MAPNAMES
    M, BC, D, _ = _libs()
    gt = BC.port_gametext()
    st, orig = _quiet(lambda: BC.src_errlist("ORIG"))
    exact, tagged = {}, {}
    for k in sorted(gt):
        ru = gt[k]["text"]
        b = orig.get(k, {}).get("text")
        if b is None or M.lang(b) != "en" or BC.is_placeholder_b(b):
            continue
        en = dec_en(b)
        n = _nkey(ru)
        if n and n not in exact:
            exact[n] = (en.strip(), "GameText#%d" % k)
        if _TAG.match(ru) and _TAG.match(en):
            n2 = _nkey(_TAG.sub("", ru))
            if n2 and n2 not in tagged:
                tagged[n2] = (_TAG.sub("", en).strip(), "GameText#%d[тег]" % k)
    for n, v in tagged.items():
        exact.setdefault(n, v)
    _MAPNAMES.update(exact)
    return _MAPNAMES


def map_name_getter(by_pos_ru=False):
    """getter EN-имени карты: RU-имя строки (у MapTable — RU-имя из Map_info, лежит в Row.pos)."""
    idx = map_name_index()

    def g(r):
        ru = r.pos if by_pos_ru else r.ru
        hit = idx.get(_nkey(ru))
        if not hit:
            return None
        g.last_src = hit[1]
        return hit[0]
    g.last_src = ""
    return g


# ---------------------------------------------------------------------------------------------
# Извлечение
# ---------------------------------------------------------------------------------------------
def dec_en(b):
    """Байты EN-строки -> str. Основа — cp1252, НО пары cp949 из блока символов (ведущий 0xA1…0xAC, второй 0xA1…0xFE)
    декодируются как cp949: корейские сборки пишут в английском тексте полноширинную пунктуацию (`A1 AF` = ’,
    `A1 A6` = …, `A1 B0/B1` = “ ”, `A2 BA` = ▶). RU-исходники порта хранят эти знаки в Unicode (’ … “ ”), значит и
    EN — так же; иначе выходит мохибейк «Don¡¯t». Хангыль-слоги (ведущий ≥ 0xB0) сюда не попадают."""
    b = bytes(b)
    out = []
    i = 0
    n = len(b)
    while i < n:
        c = b[i]
        if 0xA1 <= c <= 0xAC and i + 1 < n and 0xA1 <= b[i + 1] <= 0xFE:
            try:
                out.append(b[i:i + 2].decode("cp949"))
                i += 2
                continue
            except UnicodeDecodeError:
                pass
        out.append(bytes([c]).decode("cp1252", "replace"))
        i += 1
    return "".join(out)


# Признак строки под шифром «байт+1»: пробел между словами стал «!» (`sometijoh!wfsz!qpxfsgvm/`).
_SHIFT = re.compile(r"[a-z]![a-z]")


def unshift(s):
    """Снять сдвиг «байт+1» у строки, зашифрованной целиком (печатный ASCII −1; прочее как есть)."""
    return "".join(chr(ord(c) - 1) if 0x21 <= ord(c) <= 0x7F else c for c in s)


def to_text(v):
    """Значение источника -> str (байты EN-сборок: cp1252 + символьные пары cp949, см. dec_en)."""
    if v is None:
        return None
    if isinstance(v, str):
        return v
    return dec_en(bytes(v).rstrip(b"\x00"))


def is_en(v):
    """EN-строка годится: не пусто, не заглушка (`-`, `нет`, `none`…), язык = английский (не ko/ru/мохибейк)."""
    M, BC, _, _ = _libs()
    if v is None:
        return False
    b = v if isinstance(v, (bytes, bytearray)) else v.encode("cp1252", "replace")
    if BC.is_placeholder_b(b) or not bytes(b).strip():
        return False
    return M.lang(b) == "en"


def extract(t):
    """Извлечь EN для таблицы t.  -> dict(out={key: (en, src, ru)}, gaps=[(key, ru, why)], stats, excl=Counter, ...)."""
    M, BC, D, _ = _libs()
    rows = t.rows()
    chain = t.chain()
    primary = chain[0][0] if chain else ""
    tinfo = {"name": t.bc_name or ""}
    mode_all = nl_mode([r.ru or "" for r in rows])
    out = collections.OrderedDict()
    gaps, conflicts, warns = [], [], []
    excl = collections.Counter()
    srcc = collections.Counter()
    need = 0
    en_nl = collections.Counter()
    seen = set()
    for r in rows:
        if r.key in seen:           # тот же ключ второй раз (Talk.csv: каждый файл дважды; SNQInfo: дубль RowName)
            excl["дубль ключа в RU-источнике (берётся первая строка)"] += 1
            continue
        seen.add(r.key)
        lg = BC.lang_str(r.ru)
        if lg == "empty" and not t.write_empty_ru:
            excl["RU пусто"] += 1
            continue
        if lg == "placeholder":
            excl["RU-заглушка (-, нет)"] += 1
            continue
        if lg == "neutral":
            excl["RU без букв (числа/знаки)"] += 1
            continue
        if (t.name, r.key) in EXCLUDE_KEYS:
            need += 1
            gaps.append((r.key, r.ru, EXCLUDE_KEYS[(t.name, r.key)]))
            continue
        if r.lk is None:
            need += 1
            gaps.append((r.key, r.ru, "нет записи-источника (номер строки вне файла)"))
            continue
        need += 1
        if t.entity and not port_passport_ok().get(r.lk, False):
            if lg == "en":
                need -= 1
                excl["RU уже английский, EN-источника нет"] += 1
                continue
            gaps.append((r.key, r.ru, "меш порта ≠ меш ORIG (item_img_table правлен _value_overlay): под кодом "
                                      "другой предмет, EN оригинала чужой"))
            continue
        en = src = None
        for code, g in chain:
            v = g(r)
            if v is None:
                continue
            if code != primary and t.bc_name and not BC.secondary_ok(tinfo, code, primary) \
                    and code not in ("ORIG.soulmateskilltable",):
                continue
            if code != primary and t.secondary_filter and not t.secondary_filter(r):
                continue
            if not is_en(v):
                continue
            if t.entity and not BC.same_entity(tinfo, code, r.lk):   # вторичная сборка: под кодом другой предмет
                continue
            en, src = to_text(v), code
            if getattr(g, "last_src", ""):
                src = "%s(%s)" % (code, g.last_src)
            break
        if en is None and lg == "empty":     # write_empty_ru: RU пуст и EN нет — переводить нечего
            need -= 1
            excl["RU пусто"] += 1
            continue
        if en is None and getattr(chain[0][1], "last_src", None) is not None:
            gaps.append((r.key, r.ru, "имя карты не найдено в GameText ORIG (ни точно, ни как «[тег] имя»)"))
            continue
        if en is None and lg == "en":         # RU-ассет уже английский, а EN-источника нет: цепочка вернёт его же
            need -= 1
            excl["RU уже английский, EN-источника нет"] += 1
            continue
        if en is None:
            b0 = chain[0][1](r) if chain else None
            why = ("нет id в %s" % primary) if b0 is None else ("в %s: %s" % (primary, "заглушка" if isinstance(b0, (bytes, bytearray)) and BC.is_placeholder_b(b0) else (M.lang(b0) if isinstance(b0, (bytes, bytearray)) else "не EN")))
            gaps.append((r.key, r.ru, why))
            continue
        if len(_SHIFT.findall(en)) >= 2:       # строка ORIG под шифром «байт+1» (слова через «!»)
            if " " not in en:                  # зашифрована ЦЕЛИКОМ: снимаем сдвиг (детерминированно, не перевод)
                en = unshift(en)
                src += "(−1)"
                excl["снят сдвиг шифра +1 (строка ORIG целиком под шифром)"] += 1
            else:                              # частично (хвост) — не пишем
                gaps.append((r.key, r.ru, "в %s хвост строки под шифром «байт+1»" % src))
                continue
        if "\n" in en or "\r" in en:
            en_nl["real"] += 1
        if BSN in en:
            en_nl["lit"] += 1
        en = norm_nl(en, mode_all)
        if en == r.ru:
            excl["EN = RU (цепочка вернёт RU)"] += 1
            continue
        c = spec_conflict(r.ru, en)
        if c:
            if t.name in PRINTF_TABLES:
                conflicts.append((r.key, r.ru, en, c))
                excl["конфликт спецификаторов"] += 1
                continue
            warns.append((r.key, r.ru, en, c))
        out[r.key] = (en, src, r.ru)
        srcc[src.split("(")[0]] += 1
    return dict(rows=len(rows), need=need, out=out, gaps=gaps, conflicts=conflicts, warns=warns, excl=excl,
                srcc=srcc, nl_mode=mode_all, en_nl=en_nl, status="")


# ---------------------------------------------------------------------------------------------
# Ведомости правок
# ---------------------------------------------------------------------------------------------
LEDGER_WHY_LANG = "09.10.2026 мультиязычность: окно выбора языка/строка Язык"

# Строки ведомости GameText при первом создании (окно выбора языка, решение ведущего 09.10.2026).
LEDGER_GAMETEXT_INIT = [
    ("990350", "Choose your language / Выберите язык"),
    ("990351", "English"),
    ("990352", "Русский"),
    ("990353", "العربية"),
    ("990354", "Language"),
    ("990355", "The language will change after the game restarts."),
    ("990356", "The language will be applied after the game restarts. Restart now?"),
]


def _ledger():
    """Общий модуль ведомостей `asda_ledger` (★ дедуп №2 В2 Т8); импорт при первом обращении, не при импорте модуля."""
    if TOOLS not in sys.path:
        sys.path.insert(0, TOOLS)
    import asda_ledger
    return asda_ledger


def ledger_dir(lang):
    """Каталог ведомостей правок языка: Tools/data/loc/<lang>/."""
    return os.path.join(TOOLS, "data", "loc", lang)


def ensure_ledgers(lang, results):
    """Создать ведомости GameText.json и Tutorial_middle_category.json, если их ещё НЕТ (существующие не трогаются).
    `was` = значение из извлечения (results) или null для новой строки."""
    d = ledger_dir(lang)
    made = []
    if lang != "en":
        return made
    os.makedirs(d, exist_ok=True)

    def was_of(tn, key):
        r = results.get(tn)
        if not r:
            return None
        v = r["out"].get(key)
        return v[0] if v else None
    p = os.path.join(d, "GameText.json")
    if not os.path.exists(p):
        rows = [dict(key=k, value=v, was=was_of("GameText", k), why=LEDGER_WHY_LANG, source="manual")
                for k, v in LEDGER_GAMETEXT_INIT]
        _write_ledger(p, "Ведомость правок EN-оверлея DT_Loc_GameText_en (ключ = id GameText). Применяет "
                         "Tools/extract_text_lang.py ПОСЛЕ извлечения: was = значение извлечения или null для новой "
                         "строки; не совпало — стоп. Перевод строки — `\\n` двумя символами, как в GameText.csv.", rows)
        made.append(p)
    p = os.path.join(d, "Tutorial_middle_category.json")
    if not os.path.exists(p):
        rows = []
        with open(STAGE0_TUTORIAL_MC, encoding="utf-8-sig") as f:
            for r in csv.DictReader(f, delimiter="\t"):
                k = "%s.text" % r["id"]
                rows.append(dict(key=k, value=r["en"], was=was_of("Tutorial_middle_category", k),
                                 why="09.10.2026 мультиязычность, этап 0: в ORIG названия разделов корейские (%s), "
                                     "EN-источника нет; перевод этапа 0" % r["ko"],
                                 source="manual (stage0/tutorial_middle_category.tsv)"))
        _write_ledger(p, "Ведомость правок EN-оверлея DT_Loc_Tutorial_middle_category_en (ключ `<id>.text`). "
                         "Применяет Tools/extract_text_lang.py после извлечения; was = значение извлечения или null.", rows)
        made.append(p)
    return made


def _write_ledger(path, comment, rows):
    """Записать НОВУЮ ведомость (формат gametext_overrides.json + поле source)."""
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump({"_comment": comment, "rows": rows}, f, ensure_ascii=False, indent=2)
        f.write("\n")


def apply_ledgers(lang, results, only=False):
    """Наложить все ведомости Tools/data/loc/<lang>/*.json. Несовпадение `was` -> RuntimeError (СТОП)."""
    d = ledger_dir(lang)
    applied = collections.Counter()
    if not os.path.isdir(d):
        return applied
    for fn in sorted(os.listdir(d)):
        if not fn.lower().endswith(".json"):
            continue
        tn = os.path.splitext(fn)[0]
        # ★ ИСПРАВЛЕНИЕ дедуп №2 В2 Т8: UiLiteral.json — ведомость ДРУГОГО формата ({asset,node,ru,en}), её применяет
        #   Tools/asda_ui_literals_lang.py, а не этот генератор. С 10.10.2026 (она легла в тот же каталог) полный прогон
        #   без --only падал здесь «таблицы оверлея UiLiteral нет в генераторе» (найдено сверкой 11.10.2026).
        if tn == "UiLiteral":
            continue
        if tn not in results:
            if only:                       # --only: ведомость чужой таблицы просто не нужна в этом прогоне
                continue
            raise RuntimeError("ведомость %s: таблицы оверлея «%s» нет в генераторе — стоп" % (fn, tn))
        res = results[tn]
        for r in read_json(os.path.join(d, fn)).get("rows", []):
            k = str(r["key"])
            cur = res["out"].get(k)
            curv = cur[0] if cur else None
            # ★ ИСПРАВЛЕНИЕ дедуп №2 В2 Т8: решение — общее `asda_ledger.classify` (как у GameText/званий): «было» или
            #   прежнее значение ведомости (`prev`) → применяем; ключа нет и was = null → новая строка; извлечено УЖЕ
            #   значение ведомости → тоже годится (раньше это был СТОП «извлечено ≠ was», хотя итог тот же). Иначе — СТОП.
            if _ledger().classify_row(r, curv, exists=cur is not None) == _ledger().CONFLICT:
                raise RuntimeError("ведомость %s, ключ %s: извлечено «%s» ≠ was «%s» — стоп, ничего не записано"
                                   % (fn, k, curv, r.get("was")))
            ru = cur[2] if cur else _ru_of(res, k)
            res["out"][k] = (r["value"], "ledger", ru)
            res["gaps"] = [g for g in res["gaps"] if g[0] != k]
            res["srcc"]["ledger"] += 1
            if cur:
                res["srcc"][cur[1].split("(")[0]] -= 1
            applied[tn] += 1
    return applied


def _ru_of(res, key):
    """RU-строка ключа из пропусков таблицы (для отчёта по строкам ведомости)."""
    for g in res["gaps"]:
        if g[0] == key:
            return g[1]
    return res.get("ru_index", {}).get(key, "")


# ---------------------------------------------------------------------------------------------
# Запись
# ---------------------------------------------------------------------------------------------
def write_outputs(outdir, lang, tables, results, applied, made):
    """Записать CSV оверлеев, _sources/_gaps TSV и _report.md в outdir (только туда)."""
    os.makedirs(outdir, exist_ok=True)
    for t in tables:
        res = results[t.name]
        p = os.path.join(outdir, "DT_Loc_%s_%s.csv" % (t.name, lang))
        with open(p, "w", encoding="utf-8-sig", newline="") as f:
            w = csv.writer(f, lineterminator="\r\n")
            w.writerow(["Name", "Text"])
            for k, (en, src, ru) in res["out"].items():
                w.writerow([k, en])
        with open(os.path.join(outdir, "_sources_%s.tsv" % t.name), "w", encoding="utf-8-sig", newline="") as f:
            f.write("key\tsource\tRU\t%s\r\n" % lang.upper())
            for k, (en, src, ru) in res["out"].items():
                f.write("%s\t%s\t%s\t%s\r\n" % (k, src, tsv(ru), tsv(en)))
        gp = os.path.join(outdir, "_gaps_%s.tsv" % t.name)
        if res["gaps"] or res["conflicts"]:
            with open(gp, "w", encoding="utf-8-sig", newline="") as f:
                f.write("key\tRU\tпричина\r\n")
                for k, ru, why in res["gaps"]:
                    f.write("%s\t%s\t%s\r\n" % (k, tsv(ru), why))
                for k, ru, en, c in res["conflicts"]:
                    f.write("%s\t%s\tконфликт спецификаторов: %s; EN=%s\r\n" % (k, tsv(ru), c, tsv(en)))
        elif os.path.exists(gp):
            os.remove(gp)      # файл прошлого прогона в нашем же --out
    write_report(outdir, lang, tables, results, applied, made)


def write_report(outdir, lang, tables, results, applied, made):
    """_report.md: сводная таблица по таблицам оверлея, исключения, конфликты, предупреждения."""
    L = []
    a = L.append
    a("# EN-оверлеи данных: отчёт генератора")
    a("")
    a("Генератор: `Tools/extract_text_lang.py --lang %s`. Ключи — `Reports/owner_1009/multilang/stage1_keys.md`." % lang)
    a("Источники EN и допуски — `multilang/scripts/build_coverage.py` (цепочки B/L/P, SECONDARY_OK, same_entity).")
    a("")
    a("Колонки: **ключей** — строк/полей RU-ассета; **с текстом** — RU не пустой, не заглушка, не только цифры;")
    a("**EN записано** — строк в CSV оверлея; **= RU** — EN совпал с RU-строкой (не пишется, цепочка вернёт RU);")
    a("**нет EN** — ключи в `_gaps_<Таблица>.tsv`; **конфл.** — конфликт printf/маркеров (не пишется, тоже в gaps).")
    a("")
    a("| Таблица | ключей | с текстом | EN записано | = RU | нет EN | конфл. | исключено (RU пусто/заглушка/цифры) | источники |")
    a("|---|---:|---:|---:|---:|---:|---:|---:|---|")
    tot = collections.Counter()
    for t in tables:
        r = results[t.name]
        ex = r["excl"]
        same = ex.get("EN = RU (цепочка вернёт RU)", 0)
        ex_ru = ex.get("RU пусто", 0) + ex.get("RU-заглушка (-, нет)", 0) + ex.get("RU без букв (числа/знаки)", 0)
        srcs = ", ".join("%s %d" % (k, v) for k, v in r["srcc"].most_common() if v)
        a("| %s | %d | %d | %d | %d | %d | %d | %d | %s |" % (t.name, r["rows"], r["need"], len(r["out"]), same,
                                                            len(r["gaps"]), len(r["conflicts"]), ex_ru, srcs))
        tot["rows"] += r["rows"]; tot["need"] += r["need"]; tot["out"] += len(r["out"]); tot["same"] += same
        tot["gaps"] += len(r["gaps"]); tot["conf"] += len(r["conflicts"]); tot["ex"] += ex_ru
    a("| **Итого** | %d | %d | %d | %d | %d | %d | %d | |" % (tot["rows"], tot["need"], tot["out"], tot["same"],
                                                           tot["gaps"], tot["conf"], tot["ex"]))
    a("")
    a("## Не строятся")
    a("")
    for k, v in NOT_BUILT.items():
        a("- `%s` — %s." % (k, v))
    a("")
    a("## Ведомости правок (`Tools/data/loc/%s/`)" % lang)
    a("")
    for k, v in sorted(applied.items()):
        a("- `%s.json` — наложено строк: %d." % (k, v))
    for p in made:
        a("- создана при этом запуске: `%s`." % os.path.relpath(p, PORT))
    a("")
    a("## Пояснения по таблицам")
    a("")
    for t in tables:
        r = results[t.name]
        bits = []
        if t.note:
            bits.append(t.note)
        bits.append("переводы строк RU: %s; в EN-источнике: %s" % (
            r["nl_mode"], ", ".join("%s %d" % kv for kv in r["en_nl"].items()) or "нет"))
        base = ("RU пусто", "RU-заглушка (-, нет)", "RU без букв (числа/знаки)", "EN = RU (цепочка вернёт RU)",
                "конфликт спецификаторов")
        other = {k: v for k, v in r["excl"].items() if v and k not in base}
        if other:
            bits.append("; ".join("%s: %d" % kv for kv in other.items()))
        if r["warns"]:
            bits.append("предупреждений спецификаторов (таблица не printf, строка записана): %d — %s" % (
                len(r["warns"]), "; ".join("%s (%s)" % (w[0], w[3]) for w in r["warns"][:5])))
        a("- **%s**: %s." % (t.name, ". ".join(bits)))
    a("")
    if any(results[t.name]["conflicts"] for t in tables):
        a("## Конфликты спецификаторов (не записаны)")
        a("")
        a("| Таблица | ключ | конфликт | RU | EN |")
        a("|---|---|---|---|---|")
        for t in tables:
            for k, ru, en, c in results[t.name]["conflicts"]:
                a("| %s | %s | %s | %s | %s |" % (t.name, k, c, tsv(ru)[:120].replace("|", "\\|"), tsv(en)[:120].replace("|", "\\|")))
        a("")
    with open(os.path.join(outdir, "_report.md"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(L) + "\n")


# ---------------------------------------------------------------------------------------------
def main(argv=None):
    """Точка входа: извлечь, наложить ведомости, проверить, записать в --out."""
    ap = argparse.ArgumentParser(description="Генератор языковых оверлеев данных (EN).")
    ap.add_argument("--lang", required=True, choices=["en"], help="язык оверлея (пока только en)")
    ap.add_argument("--out", required=True, help="каталог вывода (пишется только туда)")
    ap.add_argument("--only", default="", help="через запятую: только эти таблицы оверлея")
    a = ap.parse_args(argv)
    outdir = os.path.abspath(a.out)
    for bad in (os.path.join(RAW, "Data"), os.path.join(PORT, "Content")):
        if os.path.normcase(outdir).startswith(os.path.normcase(os.path.abspath(bad))):
            raise SystemExit("--out внутри боевого каталога %s — запрещено" % bad)
    if os.path.normcase(outdir) == os.path.normcase(TEXT):
        raise SystemExit("--out = RawAssets/Text — запрещено (только подкаталог L10N/<lang>)")
    tables = build_tables()
    only = [s.strip() for s in a.only.split(",") if s.strip()]
    if only:
        tables = [t for t in tables if t.name in only]
    results = {}
    for t in tables:
        res = extract(t)
        results[t.name] = res
        print("%-26s ключей=%6d с текстом=%6d EN=%6d нет=%5d конфл=%3d" % (
            t.name, res["rows"], res["need"], len(res["out"]), len(res["gaps"]), len(res["conflicts"])), flush=True)
    made = ensure_ledgers(a.lang, results) if not only else []
    applied = apply_ledgers(a.lang, results, only=bool(only))
    write_outputs(outdir, a.lang, tables, results, applied, made)
    print("готово: %s" % outdir)


if __name__ == "__main__":
    main()
