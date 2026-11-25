# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_npc_tables.py — ЭТАПЫ Э3/Э4/Э5: таблицы, без которых окно разговора с NPC остаётся
#   «набором кнопок». Шесть источников, все — сырые `.BIN` оригинала.
#
# ══════════════════════════════════ ЧТО И ЗАЧЕМ ═════════════════════════════════════════════════
#  1) `Data\NPC\WarpListTable.BIN` → **DT_WarpList** (тег "WLT", count в шапке `+4` = 16, шаг 16).
#     Команда 30031: `netSendReqWord_op5477(this, *(u8*)(база + 16*listIndex + 4))` — в пакет 5477
#     уходит МЛАДШИЙ БАЙТ поля `+4` (mapId). Поля: `+0 warpIndex, +4 mapId, +8 cellX, +12 cellY`.
#     Контроль размером: 20 + 16×16 = 276 = размер файла.
#
#  2) `Data\NPC\SNQInfo.Bin` → **DT_NpcSnq** (тег "NPC"; ★ count лежит ПОСЛЕ шапки, по `0x14`).
#     Списки облачка `MULTYQUEST_WIN` (2406) строит `questNpc_CollectDeliverableItems@0xD89550`
#     именно по этой таблице. Запись 1796 б: `+0 i16 npcId`, `+2` имя (34 б), `+36` 10×88 «ВЫДАЁТ»,
#     `+916` 10×88 «ПРИНИМАЕТ»; под-запись 88 б: `+0 i16 questId`, `+68 + 4*j` пять КЛЮЧЕЙ SNQICT.
#     Контроль: 24 + 194×1796 = 348 448 = размер файла; запись 0 — npcId 108, выдаёт 150 (ключ 386),
#     принимает 149.
#
#  3) `Data\NPC\SNQICT.Bin` → **DT_SnqIct** (тег "CON", count по `0x14` = 839, шаг 156; было 774).
#     Запись, на которую ссылаются ключи из (2). `+0 i32 ключ`, `+8 u16 talkId` (страница, на которую
#     уходит диалог при выборе строки в облачке), дальше — ТРЕТИЙ вариант блока условия
#     (`itemTooltip_BuildStatFlagsTable2@0xD89E80` включает категории по БАЙТАМ-флагам,
#      `itemVerifyAttributeSum@0xD8A2A0` проверяет данные):
#        флаг +37  → уровень: min `u8 +38`, max `u8 +39`
#        флаг +40  → класс:  `i32 +44`, `i32 +48`
#        флаг +52  → квесты: `i32 +56/+60/+64/+68`, `u8 +72/+74/+75`, 5×`i32 +76`, 5×`u16 +96`
#        флаг +106 → предметы-цели: 5×`i32 +108`, 5×`u16 +128`
#        флаг +138 → надетый предмет `i32 +140`
#        флаг +144 → вкладка сумки `u16 +146` / `u16 +148`
#        флаг+данные `u16 +150` → слот сумки
#        `i32 +152` → прочее
#
#  4) `Data\NPC\Episode\Episode_Table.BIN` → **DT_EpisodeTable** (тег "JET", count `+4` = 486, шаг 288; было 466).
#     Названия эпизодов для строк облачка: `CQuest_Episode_FindByWord236@0xC60A30` ищет по `u16 +236`
#     и возвращает АДРЕС ЗАПИСИ — то есть строку с `+0`. Ключ `i32 +200` нужен второй функции,
#     `questEpisode_FindWordKeyById@0xC648F0` (в эталоне ошибочно названа `buffFindIconByBuffId`):
#     она переводит «код эпизода» из под-записи шага в ключ `+236`, по которому ищется слот выдачи
#     квеста для пакета 5043.
#
#  5) `Data\Mob\CallMobTable.BIN` — ТОЛЬКО СВЕРКА, ассет строит `asda_build_callmob_dt.py`
#     (`/Game/AsdaAssets/Data/DT_CallMob`, структура `FAsdaCallMobRow`). Разбор — ОБЩИЙ, из
#     `asda_effect_mob_tables.parse_call_mob` (тег "CMT", count `+4` = 8, шаг 32, 8 колонок i32).
#     Команда 30050 «призыв рейд-босса»: `objListFindByTwoKeys@0xF77000` ищет запись по ПАРЕ
#     (`+4` == npcId, `+20` == mapId) и берёт `+8` materialId, `+12` needCount.
#     ⚠ Прежняя догадка «это `Raid_npc_data.BIN`» НЕ ПОДТВЕРДИЛАСЬ: у той записи шаг 428, а не 32.
#     ⚠⚠ СВОЕГО парсера и СВОЕГО JSON у этой таблицы здесь быть не должно — см. read_callmob.
#
#  6) `Data\Character\Bin.bbz` → `event_item_list.bin` → **DT_EventItem** (тег "IMT", count `+4` = 66,
#     шаг 148). Это `g_pNpcExchangeTable` (= `CItemManager::eventItemTable`, загрузчик
#     `CItemManager_LoadEventItemList@0x9C6130`). `npcExchangeBuyRequest@0xDA3F60` перед отправкой
#     6098 проверяет пять пар: id `i32 +68 + 4*j`, нужное количество `i32 +88 + 4*j`.
#
# Запуск: Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_import_npc_tables.py
#   без UE:  python asda_import_npc_tables.py --csv-only
import json
import os
import os as _os
import struct
import sys as _sys
import zipfile

_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_bintable as _BT     # ★ дедуп №2 В1 Т5: общий выбор кодировки и C-строки (было своё тело)
import asda_ru_dt_overrides as _RO     # ★ 14.10.2026 (names11 этап 3): ведомость русских правок вложенных DT

OUT_DIR = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Data", "NPC")
PKG = "/Game/AsdaAssets/Data/NPC"

RS_WARP    = "/Script/AsdaReborn.AsdaWarpListRow"
RS_SNQ     = "/Script/AsdaReborn.AsdaNpcSnqRow"
RS_ICT     = "/Script/AsdaReborn.AsdaSnqIctRow"
RS_EPISODE = "/Script/AsdaReborn.AsdaEpisodeRow"
# RS_CALLMOB здесь НЕ используется намеренно: DT_CallMob заливает asda_build_callmob_dt.py.
# Оставлено как ссылка на структуру, которой соответствует read_callmob (сверка в main).
RS_CALLMOB = "/Script/AsdaReborn.AsdaCallMobRow"
RS_EVENT   = "/Script/AsdaReborn.AsdaEventItemRow"

HDR = 0x14   # общая шапка `.BIN`: +0 версия, +4 count, +8 полей, +0xC тег, +0x10 ноль

I  = lambda b, o: struct.unpack_from("<i", b, o)[0]
U  = lambda b, o: struct.unpack_from("<H", b, o)[0]
S  = lambda b, o: struct.unpack_from("<h", b, o)[0]


def log(msg):
    try:
        import unreal
        unreal.log_warning("[npc-tables] %s" % msg)
    except Exception:
        print("[npc-tables] %s" % msg)


# ★★★ КОДИРОВКА СТРОК ТАБЛИЦ — ПО СОДЕРЖИМОМУ, А НЕ «сначала cp949».
#   Прежний код пробовал cp949 и падал на cp1251 ТОЛЬКО по UnicodeDecodeError. Беда в том, что
#   русские байты cp1251 сплошь и рядом образуют ВАЛИДНЫЕ двухбайтовые пары cp949 — исключения нет,
#   и строка молча превращается в иероглифы. Замерено по нашему клиенту:
#     `NPC\Episode\Episode_Table.BIN` — 486 записей (466 до переноса Флэммио), из них 60 названий декодировались
#       cp949 «успешно» в CJK («Доставка отчета» → «쿡增設魏 剪特診»). Именно они и рисовались в окне
#       награды 540 пустыми прямоугольниками — глифов CJK в нашем шрифте нет.
#     `NPC\SNQInfo.Bin` — 194 записи, тем же способом портились 47 имён NPC.
#   Настоящих корейских строк в `Episode_Table.BIN` НЕТ ни одной (таблица переведена целиком),
#   в `SNQInfo.Bin` их 3 — поэтому «просто читать всё как cp1251» тоже нельзя.
#   Правильный метод в проекте уже есть и уже применён в `extract_text_tables.pick_enc`:
#   определитель языка `AstoryClient/Tools/Translate/tools/langdet.py` (он считает ОСМЫСЛЕННОСТЬ текста в кодировке,
#   а не наличие хангыля). Пользуемся ИМ, второго определителя не заводим.
_sys.path.insert(0, _P.TRANSLATE_TOOLS)
try:
    from langdet import lang as _lang
except Exception:                     # langdet недоступен — честно говорим и не молчим
    _lang = None


def pick_enc(raw):
    """'cp949' только для реально корейских строк, иначе 'cp1251' (см. блок выше).
    Тело — общий `asda_bintable.pick_enc` (тот же langdet). Без определителя — эвристика «декодируется ли
    как cp1251»; об отсутствии langdet сообщаем один раз в main(), чтобы прогон не выглядел успешным."""
    return _BT.pick_enc(raw, on_missing="heuristic")


def cstr(buf, off, maxlen):
    """C-строка поля maxlen байт (до первого NUL) в кодировке по содержимому, битые байты — замена."""
    raw = _BT.cstr_bytes(buf, off, maxlen)
    return raw.decode(pick_enc(raw), "replace")


def check(name, blob, tag, stride, count_after_hdr=False):
    """Проверка «тег + count + шаг» арифметикой РАЗМЕРА ФАЙЛА, а не на глаз."""
    got = blob[0x0C:0x0F].decode("ascii", "replace")
    count = I(blob, HDR) if count_after_hdr else I(blob, 4)
    base = HDR + 4 if count_after_hdr else HDR
    need = base + stride * count
    if got != tag or need != len(blob):
        raise ValueError("%s: тег «%s» (ждали «%s»), count %d, шаг %d ⇒ ожидалось %d б, в файле %d"
                         % (name, got, tag, count, stride, need, len(blob)))
    return base, count


# ── 1. WarpListTable.BIN ────────────────────────────────────────────────────────────────────────
def read_warp():
    b = open(os.path.join(_P.DATA, r"NPC\WarpListTable.BIN"), "rb").read()
    base, n = check("WarpListTable.BIN", b, "WLT", 16)
    return [dict(ListIndex=i, WarpIndex=I(b, base + 16 * i), MapId=I(b, base + 16 * i + 4),
                 CellX=I(b, base + 16 * i + 8), CellY=I(b, base + 16 * i + 12)) for i in range(n)]


# ── 2. SNQInfo.Bin ──────────────────────────────────────────────────────────────────────────────
def read_snq():
    b = open(os.path.join(_P.DATA, r"NPC\SNQInfo.Bin"), "rb").read()
    base, n = check("SNQInfo.Bin", b, "NPC", 1796, count_after_hdr=True)

    def sub(o):
        return dict(QuestId=S(b, o), Keys=[I(b, o + 68 + 4 * j) for j in range(5)])

    rows = []
    for i in range(n):
        r = base + 1796 * i
        rows.append(dict(
            RecIndex=i,
            NpcId=S(b, r), NpcName=cstr(b, r + 2, 34),
            Give=[sub(r + 36 + 88 * k) for k in range(10)],
            Take=[sub(r + 916 + 88 * k) for k in range(10)],
        ))
    # ★ В файле ЧЕТЫРЕ npcId встречаются ДВАЖДЫ (38, 39, 200, 201 — по две записи на каждый).
    #   Эталон `findCopyRecord1796ById16@0xDA1140` идёт по массиву сверху вниз и берёт ПЕРВОЕ
    #   совпадение, поэтому вторые копии не используются никогда. Ключ строки таблицы — npcId,
    #   значит дубли надо отбросить ЯВНО (иначе DataTable молча оставит ПОСЛЕДНЮЮ — то есть
    #   ровно ту, до которой оригинал не доходит).
    seen, out, dups = set(), [], []
    for r in rows:
        if r["NpcId"] in seen:
            dups.append(r["NpcId"])
            continue
        seen.add(r["NpcId"])
        out.append(r)
    if dups:
        log("SNQInfo: отброшено %d повторных записей npcId %s (эталон берёт ПЕРВУЮ)" % (len(dups), sorted(set(dups))))
    # ★ [ИСПРАВЛЕНИЕ] 31.10.2026 (bug-quest-follow): ячейки сверх 10 — таблица расширения `Data/NPC/QuestNpcTableExt.Bin` (тот же
    #   файл, что у NpcServer; патч N22 принимает слот ≥ 10). Ячейка встаёт в список строки NPC по индексу = слот (пропуски —
    #   пустые ячейки): облачко и 5041/5043 шлют номер ячейки как слот. Формат и проверки — `asda_quest_cells_ext.py`.
    import asda_quest_cells_ext as QX
    xp = os.path.join(_P.DATA, "NPC", QX.FILE_NAME)
    if os.path.exists(xp):
        by_npc = {r["NpcId"]: r for r in out}
        ext = QX.parse(open(xp, "rb").read())
        for x in ext:
            row = by_npc.get(x["npc"])
            if row is None:
                raise SystemExit("QuestNpcTableExt: NPC %d нет в SNQInfo.Bin — ячейку некуда поставить" % x["npc"])
            lst = row["Take" if x["kind"] == QX.TAKE else "Give"]
            while len(lst) <= x["slot"]:
                lst.append(dict(QuestId=0, Keys=[0] * 5))
            lst[x["slot"]] = dict(QuestId=QX.cell_quest(x), Keys=QX.cell_keys(x))
        log("QuestNpcTableExt: ячеек сверх 10 — %d %s" % (len(ext), [(x["npc"], x["kind"], x["slot"], QX.cell_quest(x)) for x in ext]))
    # ★ 14.10.2026 (names11, этап 3, ревизия С-3): ведомость русских правок вложенных DT (Tools/data/ru_dt_overrides.json,
    #   asda_ru_dt_overrides.py) - переимпорт из .BIN не возвращает корейский текст реплик/квестов/имён NPC, которых нет в русской сборке.
    _RO.apply_rows("DT_NpcSnq", out, lambda r: str(r["NpcId"]), log)
    return out


# ── 3. SNQICT.Bin ───────────────────────────────────────────────────────────────────────────────
def read_ict():
    b = open(os.path.join(_P.DATA, r"NPC\SNQICT.Bin"), "rb").read()
    base, n = check("SNQICT.Bin", b, "CON", 156, count_after_hdr=True)
    rows = []
    for i in range(n):
        o = base + 156 * i
        rows.append(dict(
            Key=I(b, o), TalkId=U(b, o + 8),
            HasLevel=b[o + 37], LevelMin=b[o + 38], LevelMax=b[o + 39],
            HasClass=b[o + 40], ClassId=I(b, o + 44), ClassMode=I(b, o + 48),
            HasQuest=b[o + 52],
            # Порядок аргументов `questCheckAllPrereqDone` у этого варианта: a1 = `+60`,
            #   a2 = `+56`, a3 = `+64`, a4 = `+68`, a5 = `u8 +72` — то есть «завершён» лежит на
            #   `+60`, а «не взят и не завершён» на `+56` (у диалоговой раскладки наоборот: +24/+20).
            QuestDone=I(b, o + 60), QuestFreeA=I(b, o + 56), QuestActive=I(b, o + 64),
            QuestFreeB=I(b, o + 68), QuestStage=b[o + 72],
            ObjFlagNone=b[o + 74], ObjFlagAny=b[o + 75],
            ObjIds=[I(b, o + 76 + 4 * j) for j in range(5)],
            ObjCounts=[U(b, o + 96 + 2 * j) for j in range(5)],
            HasReqItems=b[o + 106],
            ReqItemIds=[I(b, o + 108 + 4 * j) for j in range(5)],
            ReqItemCounts=[U(b, o + 128 + 2 * j) for j in range(5)],
            HasEquipped=b[o + 138], HaveItemId=I(b, o + 140),
            HasBagTab=b[o + 144], BagTabA=U(b, o + 146), BagTabB=U(b, o + 148),
            BagSlot=U(b, o + 150), Misc=I(b, o + 152),
        ))
    return rows


# ── 4. Episode_Table.BIN ────────────────────────────────────────────────────────────────────────
def read_episode():
    b = open(os.path.join(_P.DATA, r"NPC\Episode\Episode_Table.BIN"), "rb").read()
    base, n = check("Episode_Table.BIN", b, "JET", 288)
    # ★ 2026-08-22: добавлены три поля, без которых журнал заданий (окно 700) не рисуется —
    #   `+204` имя файла `.QST` (тексты квеста: название/описание/шаги; `questBook_LoadQstFile`
    #   строит из него путь `./Data/NPC/Episode/BOOK/<имя>.QST`), `+252` имя текстуры-иконки слота
    #   журнала (`CQuestLog_RefreshSlotIcons@0xC5D500`: `TexturePath_Format` — тусклая, для
    #   завершённого квеста `TexturePath_FormatCompressed` — яркая) и `+284` категория
    #   (`CQuest_LoadEpisodeTable@0xC60560` собирает по `== 2` отдельный список).
    out = [dict(EpisodeId=I(b, base + 288 * i + 200), QuestKey=U(b, base + 288 * i + 236),
                Title=cstr(b, base + 288 * i, 200), Index=i,
                QstFile=cstr(b, base + 288 * i + 204, 32),
                IconName=cstr(b, base + 288 * i + 252, 32),
                Category=U(b, base + 288 * i + 284)) for i in range(n)]
    # ★ 14.10.2026 (names11, этап 4): названия цепочек заданий, которых нет в русской сборке (там английский текст EN-клиента), -
    #   русский перевод из ведомости (Tools/data/ru_dt_overrides.json): переимпорт из .BIN не возвращает английский.
    _RO.apply_rows("DT_EpisodeTable", out, lambda r: str(r["EpisodeId"]), log)
    return out


# ── 5. CallMobTable.BIN ─────────────────────────────────────────────────────────────────────────
# ★ РЕГРЕССИЯ 2026-08-20: здесь был ВТОРОЙ, СВОЙ разбор этого же файла, дававший ПЯТЬ полей
#   (Index/NpcId/MapId/MaterialId/NeedCount). Его JSON лег в Content, редактор авто-импортировал его
#   в ассет-двойник `/Game/AsdaAssets/Data/NPC/CallMob` со структурой FAsdaCallMobRow — отсюда
#   «Property 'NpcId' … cannot be found in struct» + «Row '0' is missing an entry for …».
#   Теперь разбор ОДИН на проект: asda_effect_mob_tables.parse_call_mob — все 8 колонок + DataHex,
#   имена 1:1 с UPROPERTY в FAsdaCallMobRow. Второй копии парсера быть не должно.
# ⚠ JSON этой таблицы в Content НЕ ПИШЕМ: её ассет строит asda_build_callmob_dt.py.
def read_callmob():
    import asda_effect_mob_tables as _T
    return _T.parse_call_mob(os.path.join(_P.DATA, r"Mob\CallMobTable.BIN"))


# ── 6. event_item_list.bin (внутри Data\Character\Bin.bbz) ──────────────────────────────────────
def read_event_items():
    z = zipfile.ZipFile(_P.CHAR_BIN_BBZ)
    name = next(x for x in z.namelist() if x.lower().endswith("event_item_list.bin"))
    b = z.read(name)
    base, n = check("event_item_list.bin", b, "IMT", 148)
    rows = [dict(Index=i,
                 ItemIds=[I(b, base + 148 * i + 68 + 4 * j) for j in range(5)],
                 ItemCounts=[I(b, base + 148 * i + 88 + 4 * j) for j in range(5)])
            for i in range(n)]
    # ★ 2026-09-27 (`Reports/owner_0927/items_consistency.md`): хвост из ведомости объединения
    #   `RawAssets/ItemDB/_tail_append/event_item_list.csv` — ТА ЖЕ ведомость, что дописывает
    #   `DT_event_item_list` в `Tools/decode_item_db.py` (записи 66/67 «Коктейль-шейкер», есть в
    #   Chronicles и на сервере `EventItem_Table.txt`). Без неё DT_EventItem останавливался на 66
    #   записях, и обмен предметов 79408…79414 не находил строку. Дописываем только непрерывный хвост.
    tail = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "ItemDB", "_tail_append", "event_item_list.csv")
    if os.path.isfile(tail):
        import csv as _csv
        with open(tail, "r", encoding="utf-8-sig", newline="") as f:
            for r in sorted(_csv.DictReader(f), key=lambda r: int(r["_row"])):
                i = int(r["_row"])
                if i != len(rows):
                    raise RuntimeError("event_item_list: хвост ведомости с дырой (%d, ждали %d)" % (i, len(rows)))
                h = bytes.fromhex(r["DataHex"])
                if I(h, 0) != i:
                    raise RuntimeError("event_item_list: Idx записи ведомости %d != _row %d" % (I(h, 0), i))
                rows.append(dict(Index=i,
                                 ItemIds=[I(h, 68 + 4 * j) for j in range(5)],
                                 ItemCounts=[I(h, 88 + 4 * j) for j in range(5)]))
    # ★ 2026-09-27 (Q1(a), решение владельца: авторитет обмена = СЕРВЕР). Клиентская таблица обмена
    #   (линия Chronicles) в 22 из 68 строк требовала другое, чем серверная `EventItem_Table.txt`
    #   (`EventItemRequestReq@0x554330`). Сервер в 6098 берёт из тела только НОМЕР строки, а что списать —
    #   из СВОЕЙ таблицы, поэтому клиент обязан проверять то же. Ведомость значений
    #   `RawAssets/ItemDB/_value_overlay/event_item_list.csv` точечно приводит ItemIds/ItemCounts этих
    #   строк к серверным (значения взяты из снимка `_server_snapshot/EventItem_Table.txt`, разбор 1:1
    #   с загрузчиком сервера). СТРОГО: правка ложится, только если текущее значение равно колонке
    #   `was_*` (иначе источник .bin сменился — отказ, ведомость пересобрать). После этого сторож
    #   `IsEventExchangeServerSafe` пропускает все 68 строк, а обмены линии Arabic/XYT работают.
    ov = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "ItemDB", "_value_overlay", "event_item_list.csv")
    if os.path.isfile(ov):
        import csv as _csv
        by_row = {r["Index"]: r for r in rows}
        applied = 0
        with open(ov, "r", encoding="utf-8-sig", newline="") as f:
            for r in _csv.DictReader(f):
                i = int(r["_row"])
                row = by_row.get(i)
                if row is None:
                    raise RuntimeError("event_item_list overlay: строки %d нет в таблице" % i)
                want_ids = [int(x) for x in r["was_ItemIds"].split()]
                want_cnt = [int(x) for x in r["was_ItemCounts"].split()]
                if row["ItemIds"] != want_ids or row["ItemCounts"] != want_cnt:
                    raise RuntimeError("event_item_list overlay: строка %d — источник сменился "
                                       "(было %s/%s, ведомость ждёт %s/%s); пересобрать ведомость"
                                       % (i, row["ItemIds"], row["ItemCounts"], want_ids, want_cnt))
                row["ItemIds"] = [int(x) for x in r["ItemIds"].split()]
                row["ItemCounts"] = [int(x) for x in r["ItemCounts"].split()]
                applied += 1
        print("  event_item_list: наложена ведомость обмена (авторитет=сервер): %d строк" % applied)
    # ★ 01.11.2026 (item_dedup, этап 2): требуемый предмет-дубль -> основной код (общий модуль asda_item_dedup, карта
    #   Tools/data/item_dedup_map.json; тот же шаг делает декодер ItemDB для DataHex+68/+108). ПОСЛЕ ведомостей: их
    #   «was_*» сверяется с сырьём .bin. ASDA_NO_DEDUP_MAP=1 — коды как в архиве.
    import asda_item_dedup as _DD
    _dd = _DD.active_map()
    print("  event_item_list: item_dedup — перекодировано требуемых предметов-дублей %d"
          % sum(_DD.remap_keys(r, ("ItemIds",), _dd) for r in rows))
    return rows


# ── заливка ─────────────────────────────────────────────────────────────────────────────────────
# * DEDUP D10: telo pereyehalo v obshchuyu tochku `asda_dt_common.json_rows`
#   (ono zhilo v chetyryoh importyorah slovo v slovo). Zdes tonkiy psevdonim.
from asda_dt_common import json_rows  # noqa: E402


def fill_dt(name, row_struct_path, json_text):
    # * DEDUP D10: sam shag "RowStruct -> DataTable -> zalivka -> sohranenie" na port ODIN
    #   (`asda_dt_common.fill_dt`). Zdes ostayotsya tolko svoyo: paket, logger, podskazka pro
    #   zagolovok C++ i priznak uspeha.
    import unreal
    from asda_dt_common import fill_dt as _fill
    dt = _fill(unreal, PKG, name, row_struct_path, json_text, log,
               fmt="json", missing_hint="Public/Npc/AsdaNpcDialogTypes.h")
    return dt is not None


def main(csv_only=False):
    os.makedirs(OUT_DIR, exist_ok=True)
    warp, snq, ict, epi, cmb, evi = (read_warp(), read_snq(), read_ict(),
                                     read_episode(), read_callmob(), read_event_items())

    jobs = [
        ("DT_WarpList",      RS_WARP,    warp, lambda r: str(r["ListIndex"]), "WarpList.json"),
        ("DT_NpcSnq",        RS_SNQ,     snq,  lambda r: str(r["NpcId"]),     "NpcSnq.json"),
        ("DT_SnqIct",        RS_ICT,     ict,  lambda r: str(r["Key"]),       "SnqIct.json"),
        ("DT_EpisodeTable",  RS_EPISODE, epi,  lambda r: str(r["EpisodeId"]), "EpisodeTable.json"),
        ("DT_EventItem",     RS_EVENT,   evi,  lambda r: str(r["Index"]),     "EventItem.json"),
    ]
    # ★ 27.09.2026 (itemsdatafix, СРЕДН-4 ревизии items_data): точечный переимпорт. Раньше каждый прогон
    #   перезаливал ВСЕ пять DT (27.09 ради DT_EventItem молча пересохранились DT_WarpList/NpcSnq/SnqIct/
    #   EpisodeTable). ASDA_NPC_ONLY=DT_EventItem[,…] — писать JSON и DT только названных таблиц; пусто — все.
    only = {s.strip() for s in os.environ.get("ASDA_NPC_ONLY", "").split(",") if s.strip()}
    if only:
        unknown = only - {j[0] for j in jobs}
        if unknown:
            raise RuntimeError("ASDA_NPC_ONLY: неизвестные таблицы %s (есть %s)" % (sorted(unknown), [j[0] for j in jobs]))
        jobs = [j for j in jobs if j[0] in only]
        log("ASDA_NPC_ONLY: только %s" % [j[0] for j in jobs])
    texts = {}
    for name, rs, rows, key, fn in jobs:
        t = json_rows(rows, key)
        texts[name] = t
        with open(os.path.join(OUT_DIR, fn), "w", encoding="utf-8", newline="") as f:
            f.write(t)
        log("%s: %d строк" % (name, len(rows)))

    # ── контрольные значения (взяты из реверса, а не из этого же кода) ──
    bad = 0
    c108 = next((r for r in snq if r["NpcId"] == 108), None)
    if not c108 or c108["Give"][0]["QuestId"] != 150 or c108["Give"][0]["Keys"][0] != 386 \
            or c108["Take"][0]["QuestId"] != 149:
        log("!! контроль SNQInfo npcId 108 НЕ СОШЁЛСЯ: %s" % (c108 and c108["Give"][0]))
        bad += 1
    else:
        log("контроль SNQInfo npcId 108: выдаёт квест 150 (ключ 386), принимает 149 — OK")

    k386 = next((r for r in ict if r["Key"] == 386), None)
    # ★ 2026-09-11 (Reports/job4/JOB4_QUEST_REMAP.md): уровень квеста 150 «Ring of Brave Warrior»
    #   восстановлен до настоящего 49 (метка NSR 149 − 100) — поэтому контроль ждёт 49, а не 140.
    if not k386 or k386["TalkId"] != 12 or k386["LevelMin"] != 49 or k386["LevelMax"] != 147:
        log("!! контроль SNQICT ключ 386 НЕ СОШЁЛСЯ: %s" % k386)
        bad += 1
    else:
        log("контроль SNQICT ключ 386: talkId=12, уровень 49…147, «завершён» %d, «не взят» %d — OK"
            % (k386["QuestDone"], k386["QuestFreeA"]))

    # Контроль CallMobTable: значения взяты из СЕРВЕРНОГО `MonsterData_Info\CallMobTable.txt`
    # (строка 0 = «0 187 23522 6 1 15 0 0»), то есть из независимого от клиента источника.
    cm0 = cmb[0] if cmb else None
    cm_ref = dict(Index=0, NpcId=187, MaterialId=23522, NeedCount=6,
                  SpawnArgByte=1, MapId=15, SpawnArgWord=0, BossSlotIndex=0)
    if not cm0 or any(cm0.get(k) != v for k, v in cm_ref.items()):
        log("!! контроль CallMobTable запись 0 НЕ СОШЁЛСЯ: %s (ждали %s)" % (cm0, cm_ref))
        bad += 1
    else:
        log("контроль CallMobTable запись 0: npcId 187, карта 15, предмет 23522 ×6, "
            "SpawnArgByte 1, слот босса 0 — OK (сверено с серверным CallMobTable.txt)")

    if any(r["Title"] for r in epi[:1]):
        log("контроль Episode_Table запись 0: «%s» (ключ %d, id %d)"
            % (epi[0]["Title"], epi[0]["QuestKey"], epi[0]["EpisodeId"]))

    # ── контроль КОДИРОВКИ (дефект «название квеста прямоугольниками», окно 540) ──
    if _lang is None:
        log("!! langdet недоступен (%s) — кодировка выбрана грубой эвристикой, проверьте названия"
            % _P.TRANSLATE_TOOLS)
        bad += 1
    def _cjk(s):
        return sum(1 for c in s if "가" <= c <= "힣" or "一" <= c <= "鿿")
    # `Episode_Table.BIN` нашего клиента переведён ЦЕЛИКОМ (проверено побайтно: реально корейских
    #   строк 0 из 466) ⇒ любой иероглиф в названии эпизода = ошибка разбора, а не данные.
    cjk_epi = [r for r in epi if _cjk(r["Title"])]
    if cjk_epi:
        log("!! ИЕРОГЛИФЫ В НАЗВАНИЯХ ЭПИЗОДОВ: %d (напр. «%s») — разбор кодировки сломан"
            % (len(cjk_epi), cjk_epi[0]["Title"]))
        bad += 1
    else:
        log("контроль кодировки: иероглифов нет ни в одном из %d названий эпизодов "
            "(таблица переведена целиком, читается cp1251)" % len(epi))
    # `SNQInfo.Bin` переведён НЕ целиком: три имени NPC корейские и в самом клиенте — это ДАННЫЕ,
    #   а не ошибка. Печатаем их поимённо, чтобы «остаток» был виден, а не замолчан.
    cjk_snq = [r["NpcName"] for r in snq if _cjk(r["NpcName"])]
    log("остаток непереведённого в SNQInfo: %d имён NPC из %d — %s"
        % (len(cjk_snq), len(snq), ", ".join("«%s»" % n for n in cjk_snq) or "нет"))
    # Точечная сверка на записи, которую старый разбор портил (ключ 10 = «Доставка отчета»).
    e10 = next((r for r in epi if r["QuestKey"] == 10), None)
    if not e10 or e10["Title"] != '"Доставка отчета"':
        log("!! контроль Episode_Table ключ 10 НЕ СОШЁЛСЯ: %s" % (e10 and e10["Title"]))
        bad += 1
    else:
        log("контроль Episode_Table ключ 10: «%s» — OK (раньше читалось «\"쿡增設魏 剪特診\"»)" % e10["Title"])
    log("СВЕРКА: расхождений %d" % bad)

    if csv_only:
        return
    try:
        import unreal  # noqa: F401
    except Exception:
        log("UE недоступен — записаны только JSON в %s" % OUT_DIR)
        return
    for name, rs, rows, key, fn in jobs:
        fill_dt(name, rs, texts[name])


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main(csv_only=("--csv-only" in _sys.argv))
