# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_quest_tables.py — ТАБЛИЦЫ ЖУРНАЛА ЗАДАНИЙ (окно 700 QUEST_WIN).
#
# Импортирует ТРИ источника, которых в порту не было вовсе. Полный реверс форматов —
#   `Reports/quest_window_re.md` §3.3/§3.4/§7a и `Reports/quest_window_impl.md` §1.
#
#  1) `Data\QDT.bin` → **DT_QuestGuide** (тег «QDT», count `+4` = 804, шаг 36; было 766 до переноса Флэммио 12.09.2026).
#     Загрузчик `questGuideLoadQdtBin@0xC68B60`; читатели — `QuestGuide_BuildObjectiveText@0xC68E00`
#     (4 кнопки маршрута 705..708 окна 700) и `CQuestTracker_BuildWaypointStrings@0xC58A10`
#     (строки экранного трекера 1800 и стрелка-указатель).
#       +0  i32 questNo   — ключ, сверяется со слотом журнала `+8`
#       +4  u8  stage     — ключ, сверяется со слотом журнала `+10`
#       +5  u8  targetKind: 0 = NPC (имя из npc_list), 1 = моб (имя из таблицы мобов)
#       +8  i32 targetId
#       +12 i32 mapId
#       +16 u8  continueFlag: 0 ⇒ строка последняя, перебор прекращается
#       +20 i32 x (клетка; −1 = координат нет)
#       +24 i32 y
#       +28…+35 — у ВСЕХ 766 записей `FF…FF`, клиентом не читаются (в таблицу не кладём).
#
#  2) `Data\NPC\QCT.BIN` → **DT_QuestComplete** (тег «QCC» — НЕ «QCT», count `+4` = 491, шаг 140; было 471).
#     Загрузчик `questLoadQCTBin@0xC64CA0`; читатель — `charStateFindAndBuildRequest@0xC65080`
#     (линейный поиск `rec+0 == questNo`) + `netBuildCharStateRequest@0xC65140` (вычислитель).
#     ★ Вычислитель ПОБАЙТНО совпадает с `charEquip_VerifyStatChecksum@0xD850D0`, который в порту
#       уже есть как `UAsdaQuestMarkerSubsystem::EvalNsrCond`. Запись QCT = запись `.NSR` БЕЗ
#       первых 4 байт. Поэтому здесь поля раскладываются по ИМЕНАМ `FAsdaNsrCond`, и второй
#       вычислитель условий в порту НЕ появляется (закон «ничего не дублировать»).
#
#  3) `Data\NPC\Episode\BOOK\*.QST` (462 файла) → **DT_QuestBook**.
#     Загрузчик `questBook_LoadQstFile@0xC60C00`. Формат (смещения от начала файла):
#       +0    i32  версия (301 у всех 442)
#       +4    1484 фиксированный блок: +4 дубль версии, +8 номер книги (= префикс имени файла),
#             +12 ASCIIZ НАЗВАНИЕ квеста, +76 i32 вид квеста (нотис 3580+вид), далее заготовки «None»
#       +1488 i32  длина описания, затем само описание
#       +…    i32 ×10 длины текстовых блоков (шагов), затем блоки подряд (только ненулевые)
#
# Запуск: Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_import_quest_tables.py
#   без UE:  python asda_import_quest_tables.py --csv-only
#
# ★ ЗАКОН: промежуточные JSON пишем ТОЛЬКО в `RawAssets\Data\**` — в `Content` редактор завёл бы
#   ассет-двойник (эта мина стреляла дважды: CallMob и SnqIct).
import glob
import json
import os
import os as _os
import struct
import sys as _sys

_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_ru_dt_overrides as _RO     # ★ 14.10.2026 (names11 этап 3): ведомость русских правок вложенных DT

OUT_DIR = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Data", "Quest")

# ★★★ РУССКИЙ ДОНОР ТЕКСТОВ ЗАДАНИЙ — клиент «Фантазиум» (полностью русская сборка 2012).
#   Тексты книг в НАШЕЙ сборке НЕ переведены (английские), ровно как это было со страницами
#   диалогов NPC — разбор и то же решение в `Reports/npc_dialog_text_fix.md` §1.
#   ★ КЛЮЧ СЛИЯНИЯ — ИМЯ ФАЙЛА, а не порядковый номер: именно на имя ссылается `Episode_Table +204`,
#     и именно оно уникально. Номер книги (`+8`) ключом быть НЕ может — у донора он повторяется
#     у 12 значений (454 файла на 442 разных номера). Все 442 наших имени у донора есть.
#   Правило подстановки — как у `merge_translate.py` и у слияния `.NSR`: берём строку донора
#   ТОЛЬКО если она русская; иначе оставляем свою. Подставлять корейскую вместо английской нельзя.
QST_DIR_RU = os.path.join(_P.ROOT, "Donors", "Fantazium", "Data", "NPC", "Episode", "BOOK")
PKG = "/Game/AsdaAssets/Data/Quest"

RS_GUIDE    = "/Script/AsdaReborn.AsdaQuestGuideRow"
RS_COMPLETE = "/Script/AsdaReborn.AsdaQuestCompleteRow"
RS_BOOK     = "/Script/AsdaReborn.AsdaQuestBookRow"
RS_SMINFO   = "/Script/AsdaReborn.AsdaSmInfoRow"
RS_RAID     = "/Script/AsdaReborn.AsdaRaidQuestRow"

# ★★★ ПОД-ЗАПИСИ ШАГА в `.QST` (добавлено 2026-08-22 вместе с экранным трекером 1800).
#   БАЗА 796 (0x31C) от начала файла, ШАГ 68, ровно 10 штук; поле `+12` = `recId` в `SMInfo.BIN`.
#   База НЕ угадана: перебраны 788/792/796/800/804 по всем 442 файлам — только при 796 ВСЕ 250
#   ненулевых значений попадают в 0…268 (число записей SMInfo), при 788/792 — ни одного.
QST_SUB_BASE   = 796
QST_SUB_STRIDE = 68
QST_SUB_COUNT  = 10
QST_SUB_ID_OFF = 12

HDR = 0x14   # общая шапка `.BIN`: +0 версия, +4 count, +8 полей, +0xC тег, +0x10 ноль

I = lambda b, o: struct.unpack_from("<i", b, o)[0]
U = lambda b, o: struct.unpack_from("<H", b, o)[0]
B = lambda b, o: b[o]

# Размер записи .QST: 4 (версия) + 1484 (блок) = 1488 до длины описания.
QST_FIXED = 4 + 0x5CC


def log(msg):
    try:
        import unreal
        unreal.log_warning("[quest-tables] %s" % msg)
    except Exception:
        print("[quest-tables] %s" % msg)


def check(name, blob, tag, stride):
    """Проверка «тег + count + шаг» арифметикой РАЗМЕРА ФАЙЛА, а не на глаз."""
    got = blob[0x0C:0x0F].decode("ascii", "replace")
    count = I(blob, 4)
    need = HDR + stride * count
    if got != tag or need != len(blob):
        raise ValueError("%s: тег «%s» (ждали «%s»), count %d, шаг %d ⇒ ожидалось %d б, в файле %d"
                         % (name, got, tag, count, stride, need, len(blob)))
    return HDR, count


# ★ ЯЗЫК СТРОКИ ОПРЕДЕЛЯЕМ ТЕМ ЖЕ определителем, что и остальные таблицы клиента
#   (`AstoryClient/Tools/Translate/tools/langdet.py`, тот же экземпляр, что импортирует `asda_import_npc_tables`).
#   Своей эвристики НЕ заводим: русские байты cp1251 сплошь и рядом образуют валидные пары cp949,
#   и «сначала cp949» молча превращает текст в иероглифы — эта мина уже стреляла на
#   `Episode_Table.BIN` (60 названий эпизодов). Импорт модуля безопасен: его `main()` под защитой
#   закона `tools-no-import-side-effects`.
from asda_import_npc_tables import _lang as _detect_lang
import asda_bintable as _BT     # ★ дедуп №2 В1 Т5: общий разбор C-строк


def _is_russian(raw):
    """Русская ли строка донора.

    Первый фильтр — ОБЩИЙ определитель `AstoryClient/Tools/Translate/tools/langdet.py` (тот же, что решает кодировку
    у всех прочих таблиц; своей эвристики языка не заводим).

    ★ Второй фильтр обязателен. Определитель даёт ЛОЖНО-ПОЛОЖИТЕЛЬНЫЕ на двухбайтовых
    восточных кодировках: байты cp949 (корейский), Shift-JIS (японский) и Big5 (китайский) при
    чтении как cp1251 дают сплошную кириллицу, и `langdet` честно говорит «rus». Замер по донору:
    из 608 строк, которые он назвал русскими, **7 таковыми не являются** — `152_Qst_19`
    («№аЗфБцґВ №ЭБцАЗ БЦАО»), `382_Qst_115`, `396_Qst_181` (корейские), `160Qst_13`, `423_Qst_36`
    (японские), `90`, `106` (китайские). Без второго фильтра они уезжают в таблицу мусором.

    Проверка «строчных букв больше, чем прописных, и их не меньше двух» разделяет эти случаи
    начисто: у настоящего русского текста строчные преобладают (`"Доставка отчета"` — 13 против 1),
    у мозаики из двухбайтовых кодировок ведущие байты ложатся в диапазон ПРОПИСНЫХ А…Я.
    Замер: принято 601 строк, отсеяно ровно те самые 7.

    ⚠ Проверять `raw.decode("cp949")` на хангыль НЕЛЬЗЯ: русские cp1251-байты сплошь и рядом
    образуют валидные пары cp949 (например, то же `"Доставка отчета"`), и такой фильтр выбросил бы
    24 настоящих перевода."""
    if not raw or _detect_lang is None or _detect_lang(raw) != "rus":
        return False
    t = raw.decode("cp1251", "replace")
    lower = sum(1 for c in t if "а" <= c <= "я" or c == "ё")
    upper = sum(1 for c in t if "А" <= c <= "Я" or c == "Ё")
    return lower > upper and lower >= 2


def qst_text(raw):
    """Текст .QST в игровой кодировке.

    ★ ЗАМЕР ПО ДАННЫМ (определитель `langdet` на описаниях всех 442 файлов):
        425 «lat» (английский), 15 «kor», 1 «rus», 1 пустой; cp949 не декодирует РОВНО ОДИН файл.
    То есть тексты книг в этой сборке НЕ переведены — они английские, и внутри них встречаются
    ДВУХБАЙТОВЫЕ знаки препинания cp949 (0xA1 0xAF = «’»). Читать такое как cp1251 нельзя:
    получается «RavenЎЇs letter». Поэтому правило такое:
        · определитель сказал «rus» → cp1251 (единственный русский файл);
        · иначе пробуем cp949 (он же покрывает ASCII и корейский);
        · не декодировалось → cp1251.
    Определитель — ОБЩИЙ (`AstoryClient/Tools/Translate/tools/langdet.py`), своей эвристики языка здесь нет."""
    if _is_russian(raw):
        try:
            return raw.decode("cp1251")
        except UnicodeDecodeError:
            pass
    # ★ Один файл НАШЕГО клиента (`160Qst_13.QST`) записан в Shift-JIS (японский), и определитель
    #   языка называет его «rus» — то есть без этой ветки он уезжал в таблицу мозаикой
    #   «[ѓEѓFѓ“ѓfѓB‚М‚ЁЉи‚ў]». Ветка узкая и проверяемая: берём cp932 ТОЛЬКО если байты
    #   декодируются без ошибок И в результате есть кана (0x3040…0x30FF) — на корейских и
    #   китайских строках это условие не выполняется.
    try:
        jp = raw.decode("cp932")
        if any(0x3040 <= ord(c) <= 0x30FF for c in jp):
            return jp
    except UnicodeDecodeError:
        pass
    try:
        return raw.decode("cp949")
    except UnicodeDecodeError:
        return raw.decode("cp1251", "replace")


def cstr(buf, off, maxlen):
    """C-строка поля maxlen байт (до первого NUL, общий `asda_bintable.cstr_bytes`) → текст по `qst_text`."""
    return qst_text(_BT.cstr_bytes(buf, off, maxlen))


# ── 1. QDT.bin ──────────────────────────────────────────────────────────────────────────────────
def read_qdt():
    b = open(os.path.join(_P.DATA, "QDT.bin"), "rb").read()
    base, n = check("QDT.bin", b, "QDT", 36)
    rows = []
    for i in range(n):
        o = base + 36 * i
        rows.append(dict(Index=i, QuestNo=I(b, o), Stage=B(b, o + 4), TargetKind=B(b, o + 5),
                         TargetId=I(b, o + 8), MapId=I(b, o + 12), ContinueFlag=B(b, o + 16),
                         X=I(b, o + 20), Y=I(b, o + 24)))
    return rows


# ★ 25.10.2026 (баг игроков «квест засчитывает не тех мобов», `Reports/quest_mobs/QUEST_MOB_AUDIT.md`):
#   ведомость точечных исправлений гида `Tools/data/quest_guide_fixes.json` — цели-мобы переноса Флэммио (остались в
#   нумерации Global) и 5 записей-дефектов оригинала (кв. 221, 20042, 310). Бинарь клиента не трогаем: ведомость
#   переживает повторный импорт. Запись применяется по `index`, ТОЛЬКО если `expect` совпал с сырой записью
#   (иначе — пропуск и счётчик ошибок: данные под ведомостью изменились, её надо пересмотреть).
QDT_FIXES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "quest_guide_fixes.json")


def apply_qdt_fixes(rows):
    """Применить ведомость к записям QDT на месте. Возврат: (применено, не сошлось)."""
    if not os.path.isfile(QDT_FIXES):
        log("!! ведомость гида %s не найдена" % QDT_FIXES)
        return 0, 1
    fixes = json.load(open(QDT_FIXES, encoding="utf-8"))["fixes"]
    done = bad = 0
    for f in fixes:
        i = f["index"]
        r = rows[i] if 0 <= i < len(rows) else None
        if r is None or any(r.get(k) != v for k, v in f["expect"].items()):
            log("!! ведомость гида: запись %d не совпала с ожиданием %s (сейчас %s) — НЕ применена" % (i, f["expect"], r))
            bad += 1
            continue
        r.update(f["set"])
        done += 1
    log("ведомость гида: применено %d из %d" % (done, len(fixes)))
    # ★ 06.11.2026 (решение владельца: игрок видит, кого убить и где): новые строки гида для стадий без цели в QDT.
    #   Индекс — следующий за концом таблицы; клиент ищет строки по заданию и стадии и сортирует по Index.
    adds = json.load(open(QDT_FIXES, encoding="utf-8")).get("adds", [])
    for a in adds:
        rows.append(dict({k: a[k] for k in ("QuestNo", "Stage", "TargetKind", "TargetId", "MapId", "ContinueFlag", "X", "Y")}, Index=len(rows)))
    log("ведомость гида: добавлено строк %d" % len(adds))
    return done, bad


# ── 2. QCT.BIN ──────────────────────────────────────────────────────────────────────────────────
# Раскладка = `.NSR` со сдвигом −4; имена полей 1:1 с `FAsdaNsrCond`, чтобы условие считал
#   существующий `EvalNsrCond` (раскладка `Script`: слот сумки u16 +132, прочее i32 +136).
def read_qct():
    b = open(os.path.join(_P.DATA, r"NPC\QCT.BIN"), "rb").read()
    base, n = check("QCT.BIN", b, "QCC", 140)
    rows = []
    for i in range(n):
        o = base + 140 * i
        rows.append(dict(
            Index=i, QuestNo=I(b, o),
            LevelMin=U(b, o + 4), LevelMax=U(b, o + 6),
            ClassId=I(b, o + 8), ClassMode=I(b, o + 12),
            QuestFreeA=I(b, o + 16), QuestDone=I(b, o + 20),
            QuestActive=I(b, o + 24), QuestFreeB=I(b, o + 28), QuestStage=I(b, o + 32),
            ObjIds=[I(b, o + 36 + 8 * k) for k in range(5)],
            ObjCounts=[U(b, o + 40 + 8 * k) for k in range(5)],
            ObjFlagNone=U(b, o + 74), ObjFlagAny=U(b, o + 76),
            ReqItemIds=[I(b, o + 80 + 8 * k) for k in range(5)],
            ReqItemCounts=[U(b, o + 84 + 8 * k) for k in range(5)],
            HaveItemId=I(b, o + 120),
            BagTabA=I(b, o + 124), BagTabB=I(b, o + 128),
            BagSlot=U(b, o + 132), Misc=I(b, o + 136),
        ))
    return rows


# ── 2b. SMInfo.BIN ──────────────────────────────────────────────────────────────────────────────
# ★★★ Таблица «сколько предметов надо» — то, чего не хватало экранному трекеру 1800.
#   Шапка у файла НЕСТАНДАРТНАЯ: `count` лежит НЕ в `+4` (там 0), а ОТДЕЛЬНЫМ `i32` сразу после
#   20-байтовой шапки — так его и читает загрузчик `questData_loadSMInfo@0xDA0820`
#   (`fread(Buffer, 0x14, 1)`, затем `fread(&count, 4, 1)`). Поэтому общий `check()` не годится,
#   проверяем арифметикой размера здесь же.
def read_sminfo():
    p = os.path.join(_P.DATA, r"NPC\SMInfo.BIN")
    b = open(p, "rb").read()
    tag = b[0x0C:0x0F].decode("ascii", "replace")
    n = I(b, 0x14)
    base = 0x18
    if tag != "QUE" or base + 72 * n != len(b):
        raise ValueError("SMInfo.BIN: тег «%s» (ждали «QUE»), count %d ⇒ ожидалось %d б, в файле %d"
                         % (tag, n, base + 72 * n, len(b)))
    rows = []
    # ★ 01.11.2026 (item_dedup, этап 2): требуемый предмет-дубль -> основной код (общий модуль asda_item_dedup,
    #   карта Tools/data/item_dedup_map.json; ASDA_NO_DEDUP_MAP=1 — значения как в SMInfo.BIN).
    import asda_item_dedup as _DD
    dmap = _DD.active_map()
    for i in range(n):
        o = base + 72 * i
        rows.append(dict(RecId=I(b, o),
                         ReqItemIds=_DD.remap_list([I(b, o + 32 + 4 * k) for k in range(5)], dmap),
                         ReqItemCounts=[U(b, o + 52 + 2 * k) for k in range(5)]))
    return rows


# ── 2c. Raid_quest_data.BIN ─────────────────────────────────────────────────────────────────────
# Числовая часть крафт/рейд-заданий (20000…30000) — ветка «B» трекера. Текстовая таблица
#   `Text\Quest\DT_Raid_quest_data` несёт только `id`+`name`, числовых полей в ней нет.
#   Здесь `count` — в ШАПКЕ (`+4`), как у остальных `.BIN`, поэтому годится общий `check()`.
def read_raid():
    # ★ 28.10.2026 (доска объявлений 75000): ОДИН разборщик записи 388 б на порт — `asda_import_board_quests.read_quests`
    #   (все поля: тип, max, зона, награды; источник чисел — Interlude, решение владельца). Прежний разбор здесь брал только
    #   уровни/режим/требования — переимпорт этой цепочкой затёр бы поля доски.
    import asda_import_board_quests as _BQ
    return _BQ.read_quests()


# ── 3. BOOK\*.QST ───────────────────────────────────────────────────────────────────────────────
def read_qst_substeps(d):
    """Под-записи шагов из фиксированного блока `.QST`: 10 значений `recId` для `SMInfo.BIN`.
    Отсутствие требований у шага в данных записано и нулём, и −1 — приводим к −1, чтобы у
    потребителя была ОДНА проверка, а не две."""
    out = []
    for s in range(QST_SUB_COUNT):
        o = QST_SUB_BASE + QST_SUB_STRIDE * s + QST_SUB_ID_OFF
        v = I(d, o) if o + 4 <= len(d) else -1
        out.append(v if v > 0 else -1)
    return out


def parse_qst_raw(path):
    """Разбор файла .QST в СЫРЫЕ БАЙТЫ полей. Один разборщик на оба клиента — второй копии
    формата не заводим (та же схема, что у слияния `.NSR`)."""
    d = open(path, "rb").read()
    name = os.path.splitext(os.path.basename(path))[0]
    if len(d) < QST_FIXED + 4:
        raise ValueError("%s: файл короче фиксированной части (%d б)" % (name, len(d)))
    end = d.find(bytes(1), 12, 12 + 60)   # позиция NUL-терминатора названия
    title = d[12:end if end >= 0 else 12 + 60]
    o = QST_FIXED
    desc_len = I(d, o); o += 4
    if desc_len < 0 or o + desc_len + 40 > len(d):
        # Единственный такой файл — `373_Qst_124.QST` (пустая заготовка, descLen = 0 и хвост нулей).
        #   Импортируем с пустыми текстами, а не выбрасываем (закон «не резать охват»).
        return dict(FileName=name, Version=I(d, 0), BookNo=I(d, 8), Kind=I(d, 76),
                    TitleB=title, DescB=b"", StepsB=[b""] * 10,
                    StepSmInfoIds=read_qst_substeps(d), bTruncated=True)
    desc = d[o:o + desc_len]; o += desc_len
    lens = struct.unpack_from("<10i", d, o); o += 40
    steps = []
    for L in lens:
        if L > 0:
            steps.append(d[o:o + L]); o += L
        else:
            steps.append(b"")
    if o != len(d):
        raise ValueError("%s: хвост не сошёлся (разобрано %d, в файле %d)" % (name, o, len(d)))
    return dict(FileName=name, Version=I(d, 0), BookNo=I(d, 8), Kind=I(d, 76),
                TitleB=title, DescB=desc, StepsB=steps,
                StepSmInfoIds=read_qst_substeps(d), bTruncated=False)


def parse_qst(path):
    """Готовая строка таблицы (тексты уже декодированы). Слияние с русским донором делает
    `read_books` — здесь только наш клиент."""
    r = parse_qst_raw(path)
    return dict(FileName=r["FileName"], Version=r["Version"], BookNo=r["BookNo"],
                Title=qst_text(r["TitleB"]), Kind=r["Kind"],
                Desc=qst_text(r["DescB"]), Steps=[qst_text(x) for x in r["StepsB"]],
                StepSmInfoIds=r["StepSmInfoIds"], bTruncated=r["bTruncated"])


# Счётчики слияния — заполняются в `read_books`, печатаются контролем в `main`.
RU_STAT = dict(title_tot=0, title_ru=0, desc_tot=0, desc_ru=0, step_tot=0, step_ru=0,
               nopair_files=0, nopair_steps=0)


def read_books():
    root = os.path.join(_P.DATA, r"NPC\Episode\BOOK")
    files = sorted(glob.glob(os.path.join(root, "*.QST")))
    mine = [parse_qst_raw(f) for f in files]

    # ── русский донор: ключ = ИМЯ ФАЙЛА (см. блок про QST_DIR_RU) ──
    ru = {}
    if os.path.isdir(QST_DIR_RU):
        for f in sorted(glob.glob(os.path.join(QST_DIR_RU, "*.QST"))):
            try:
                r = parse_qst_raw(f)
            except ValueError:
                continue          # у донора есть файлы, которые не разбираются, — просто пропускаем
            ru[r["FileName"]] = r
    else:
        log("!! русский донор не найден: %s — тексты заданий останутся английскими" % QST_DIR_RU)

    rows = []
    # ★ 12.09.2026 (Флэммио 50–54): у новых книг нет пары у донора «Фантазиум» — русский текст
    #   берётся из клиента AsdaFableClassic и лежит в QuestBookRuExtra.json (ключ — имя файла).
    #   ОТКАТ: удалить этот блок и файл. Числа по-прежнему только из нашего клиента.
    extra_p = os.path.join(OUT_DIR, "QuestBookRuExtra.json")
    if os.path.isfile(extra_p):
        ex = json.load(open(extra_p, encoding="utf-8"))
        for name, v in ex.items():
            ru.setdefault(name, dict(TitleB=v.get("title", "").encode("cp1251", "replace"),
                                     DescB=v.get("desc", "").encode("cp1251", "replace"),
                                     StepsB=[(x or "").encode("cp1251", "replace") for x in v.get("steps", [])] + [b""] * 10))
        log("РУС: книг из QuestBookRuExtra.json: %d" % len(ex))
    for m in mine:
        d = ru.get(m["FileName"])
        if d is None:
            RU_STAT["nopair_files"] += 1

        # Заголовок
        RU_STAT["title_tot"] += 1 if m["TitleB"] else 0
        title_b = m["TitleB"]
        if d is not None and _is_russian(d["TitleB"]):
            title_b = d["TitleB"]; RU_STAT["title_ru"] += 1

        # Описание
        RU_STAT["desc_tot"] += 1 if m["DescB"] else 0
        desc_b = m["DescB"]
        if d is not None and _is_russian(d["DescB"]):
            desc_b = d["DescB"]; RU_STAT["desc_ru"] += 1

        # Тексты шагов — ключ НОМЕР СЛОТА 0…9 (он же стадия квеста), а не порядок непустых
        steps_b = list(m["StepsB"])
        for i in range(10):
            if not m["StepsB"][i]:
                continue
            RU_STAT["step_tot"] += 1
            if d is None:
                continue
            if not d["StepsB"][i]:
                RU_STAT["nopair_steps"] += 1
                continue
            if _is_russian(d["StepsB"][i]):
                steps_b[i] = d["StepsB"][i]; RU_STAT["step_ru"] += 1

        # ★ Под-записи шагов берём ТОЛЬКО из СВОЕГО файла: у донора другая сборка данных, и
        #   ссылки на `SMInfo.BIN` там могут указывать на другие записи. Переводим тексты, а не числа.
        rows.append(dict(FileName=m["FileName"], Version=m["Version"], BookNo=m["BookNo"],
                         Title=qst_text(title_b), Kind=m["Kind"],
                         Desc=qst_text(desc_b), Steps=[qst_text(x) for x in steps_b],
                         StepSmInfoIds=m["StepSmInfoIds"], bTruncated=m["bTruncated"]))
    # ★ 14.10.2026 (names11, этап 3, ревизия С-3): ведомость русских правок вложенных DT (Tools/data/ru_dt_overrides.json,
    #   asda_ru_dt_overrides.py) - переимпорт из .BIN не возвращает корейский текст реплик/квестов/имён NPC, которых нет в русской сборке.
    _RO.apply_rows("DT_QuestBook", rows, lambda r: r["FileName"], log)
    return rows


# ── заливка (та же схема, что у asda_import_npc_tables.py) ──────────────────────────────────────
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
               fmt="json", missing_hint="Public/Quest/AsdaQuestTypes.h")
    return dt is not None


def main(csv_only=False):
    os.makedirs(OUT_DIR, exist_ok=True)
    qdt, qct, books = read_qdt(), read_qct(), read_books()
    sminfo, raid = read_sminfo(), read_raid()
    # ★ 25.10.2026 (ревизия Н4): ведомость гида применяется ДО записи JSON/DT; контроли ниже считаются по СЫРЫМ записям
    #   (`qdt_raw`). Хоть одна запись ведомости не совпала с `expect` — `QuestGuide.json` и `DT_QuestGuide` НЕ пишутся
    #   вовсе (частично исправленный гид в ассет не уходит), прочие таблицы — как обычно.
    qdt_raw = [dict(r) for r in qdt]
    _done, _bad = apply_qdt_fixes(qdt)
    guide_blocked = _bad > 0
    if guide_blocked:
        log("!! ведомость гида: %d записей не совпало — QuestGuide.json и DT_QuestGuide НЕ записываются" % _bad)

    jobs = [
        ("DT_QuestGuide",    RS_GUIDE,    qdt,   lambda r: str(r["Index"]),    "QuestGuide.json"),
        # ★ Ключ строки — сквозной `Index`, а НЕ `QuestNo`: в файле 471 запись, но только 466 разных
        #   номеров квестов (у пяти номеров условие задано ДВУМЯ строками). Эталон
        #   `charStateFindAndBuildRequest@0xC65080` перебирает ВСЕ записи с этим номером и выходит
        #   по первой сошедшейся, поэтому терять дубли нельзя — с ключом `QuestNo` пять записей
        #   молча схлопывались (в таблицу попадало 466 строк вместо 471).
        ("DT_QuestComplete", RS_COMPLETE, qct,   lambda r: str(r["Index"]),    "QuestComplete.json"),
        ("DT_QuestBook",     RS_BOOK,     books, lambda r: r["FileName"],      "QuestBook.json"),
        # ★★★ Ключ строки — сам `RecId`: поиск эталона (`findRecordById_C10@0xDA0C10`) идёт
        #   именно по нему, и он же лежит в под-записи шага `.QST` (+12).
        ("DT_SmInfo",        RS_SMINFO,   sminfo, lambda r: str(r["RecId"]),   "SmInfo.json"),
        # Ключ — номер квеста: `mapObjFindEntryById@0xEECC90` ищет по `rec+0`, дублей нет
        #   (проверяется контролем ниже).
        ("DT_RaidQuestData", RS_RAID,     raid,  lambda r: str(r["QuestId"]),  "RaidQuestData.json"),
    ]
    texts = {}
    for name, rs, rows, key, fn in jobs:
        t = json_rows(rows, key)
        texts[name] = t
        if name == "DT_QuestGuide" and guide_blocked:
            continue                       # ★ Н4: прежний QuestGuide.json остаётся на диске
        with open(os.path.join(OUT_DIR, fn), "w", encoding="utf-8", newline="") as f:
            f.write(t)
        log("%s: %d строк" % (name, len(rows)))

    # ── КОНТРОЛЬ по числам, взятым из реверса (а не из этого же кода) ──
    bad = 0
    # ★ 12.09.2026 (Флэммио 50–54): QDT вырос 766 -> 804
    if len(qdt_raw) != 804:
        log("!! QDT: строк %d, ожидалось 804" % len(qdt_raw)); bad += 1
    r0 = qdt_raw[0]
    ref0 = dict(QuestNo=1, Stage=0, TargetKind=0, TargetId=1, MapId=3, ContinueFlag=0, X=180, Y=327)
    if any(r0[k] != v for k, v in ref0.items()):
        log("!! QDT запись 0 НЕ СОШЛАСЬ: %s (ждали %s)" % (r0, ref0)); bad += 1
    else:
        log("контроль QDT запись 0: квест 1, стадия 0, цель-NPC 1, карта 3, (180,327) — OK")
    cont = sum(1 for r in qdt_raw if r["ContinueFlag"])
    mobs = sum(1 for r in qdt_raw if r["TargetKind"] == 1)
    noxy = sum(1 for r in qdt_raw if r["X"] == -1)
    # ★ 12.09.2026: +3 продолжения, +18 целей-мобов, +2 без координат (20 квестов Флэммио)
    if (cont, mobs, noxy) != (178, 319, 35):
        log("!! QDT статистика НЕ СОШЛАСЬ: continue=%d моб=%d без координат=%d (ждали 178/319/35)"
            % (cont, mobs, noxy)); bad += 1
    else:
        log("контроль QDT: 178 продолжений, 319 целей-мобов, 35 записей без координат — OK")
    bad += _bad                            # ★ 25.10.2026: несовпадения ведомости гида — в общий счёт сверки

    uniq = len({r["QuestNo"] for r in qct})
    if uniq != len(qct):
        log("контроль QCT: %d записей на %d разных номеров квеста — ключ строки ДОЛЖЕН быть Index" % (len(qct), uniq))
    # ★ 12.09.2026 (Флэммио 50–54): QCT вырос 471 -> 491 (20 новых квестов Global)
    if len(qct) != 491:
        log("!! QCT: строк %d, ожидалось 491" % len(qct)); bad += 1
    q0 = qct[0]
    if (q0["QuestNo"], q0["LevelMin"], q0["LevelMax"], q0["QuestActive"], q0["QuestStage"]) != (0, 1, 150, 0, 1):
        log("!! QCT запись 0 НЕ СОШЛАСЬ: %s" % q0); bad += 1
    else:
        log("контроль QCT запись 0: квест 0, уровень 1…150, «квест 0 в работе, стадия 1» — OK")
    # Предметные категории (3…7) в данных не заполнены НИ РАЗУ — вычислитель обязан их не «включать».
    if any(x > 0 for r in qct for x in r["ReqItemIds"]) or any(r["HaveItemId"] > 0 for r in qct):
        log("!! QCT: появились предметные условия — проверить категорию 3/4 вычислителя"); bad += 1
    else:
        log("контроль QCT: предметных условий (категории 3…7) нет ни в одной из 491 записи — OK")

    # ★ 12.09.2026: книг 442 -> 462 (20 новых книг Флэммио)
    if len(books) != 462:
        log("!! BOOK: файлов %d, ожидалось 462" % len(books)); bad += 1
    trunc = [b["FileName"] for b in books if b["bTruncated"]]
    if trunc != ["373_Qst_124"]:
        log("!! BOOK: неразобранные файлы %s (ожидался ровно один — 373_Qst_124)" % trunc); bad += 1
    else:
        log("контроль BOOK: 461 файл разобран побайтно, 1 пустая заготовка (373_Qst_124) — OK")
    b0 = next((x for x in books if x["FileName"] == "0_Qst_90"), None)
    if not b0 or b0["Title"] != "Sowel Instruction" or b0["BookNo"] != 0 or b0["Kind"] != 0:
        log("!! BOOK 0_Qst_90 НЕ СОШЁЛСЯ: %s" % (b0 and {k: b0[k] for k in ("BookNo", "Title", "Kind")}))
        bad += 1
    else:
        log("контроль BOOK 0_Qst_90: книга 0, «Sowel Instruction», вид 0 (нотис 3580) — OK")
    # ── СЛИЯНИЕ С РУССКИМ ДОНОРОМ: числа, а не «вроде перевелось» ──
    log("РУС: названия заданий  переведено %d из %d (осталось не по-русски %d)"
        % (RU_STAT["title_ru"], RU_STAT["title_tot"], RU_STAT["title_tot"] - RU_STAT["title_ru"]))
    log("РУС: описания заданий  переведено %d из %d (осталось не по-русски %d)"
        % (RU_STAT["desc_ru"], RU_STAT["desc_tot"], RU_STAT["desc_tot"] - RU_STAT["desc_ru"]))
    log("РУС: тексты шагов      переведено %d из %d (у донора слот пуст %d, прочее не по-русски %d)"
        % (RU_STAT["step_ru"], RU_STAT["step_tot"], RU_STAT["nopair_steps"],
           RU_STAT["step_tot"] - RU_STAT["step_ru"] - RU_STAT["nopair_steps"]))
    if RU_STAT["nopair_files"]:
        log("!! файлов без русской пары: %d" % RU_STAT["nopair_files"])
    if RU_STAT["title_ru"] == 0:
        log("!! ни одна строка не переведена — проверить путь донора %s" % QST_DIR_RU)

    # ── SMInfo.BIN: «сколько предметов надо» ──
    # ★ 12.09.2026: SMInfo 269 -> 285 (16 новых записей целей)
    if len(sminfo) != 285:
        log("!! SMInfo: строк %d, ожидалось 285" % len(sminfo)); bad += 1
    if [r["RecId"] for r in sminfo] != list(range(len(sminfo))):
        log("!! SMInfo: RecId НЕ совпадает с порядковым номером — ключ строки под вопросом"); bad += 1
    else:
        log("контроль SMInfo: 285 записей, RecId = порядковый номер — OK")
    s47 = next((r for r in sminfo if r["RecId"] == 47), None)
    if not s47 or s47["ReqItemIds"][:2] != [21299, 21300] or s47["ReqItemCounts"][:2] != [1, 1]:
        log("!! SMInfo[47] НЕ СОШЛАСЬ: %s" % s47); bad += 1
    else:
        log("контроль SMInfo[47]: предметы 21299/21300 по 1 шт. — OK (сквозная сверка с 104_Qst_5.QST)")

    # ── Под-записи шагов книги: связка `.QST +796+68*step+12` → `SMInfo.RecId` ──
    subs = [v for bk in books for v in bk["StepSmInfoIds"] if v >= 0]
    known = {r["RecId"] for r in sminfo}
    lost = [v for v in subs if v not in known]
    if lost:
        log("!! BOOK: %d ссылок на несуществующие записи SMInfo (например %s)" % (len(lost), lost[:5])); bad += 1
    else:
        log("контроль BOOK: %d ссылок на SMInfo, все разрешаются — OK" % len(subs))
    bk5 = next((x for x in books if x["FileName"] == "104_Qst_5"), None)
    if not bk5 or bk5["StepSmInfoIds"][0] != 47:
        log("!! BOOK 104_Qst_5: под-запись шага 0 = %s, ждали 47" % (bk5 and bk5["StepSmInfoIds"][0])); bad += 1
    else:
        log("контроль BOOK 104_Qst_5: шаг 0 ссылается на SMInfo[47] — OK")

    # ── Raid_quest_data.BIN: числовая часть крафт-заданий ──
    if len(raid) != 57:
        log("!! Raid: строк %d, в шапке обещано 57" % len(raid)); bad += 1
    if len({r["QuestId"] for r in raid}) != len(raid):
        log("!! Raid: номера квестов повторяются — ключ строки не может быть QuestId"); bad += 1
    r0 = raid[0] if raid else None
    if not r0 or r0["QuestId"] != 20000 or r0["ReqItemIds"][0] != 31184 or r0["ReqItemCounts"][0] != 30:
        log("!! Raid запись 0 НЕ СОШЛАСЬ: %s" % r0); bad += 1
    else:
        log("контроль Raid запись 0: квест 20000, предмет 31184 в количестве 30 — OK")
    if any(not (20000 <= r["QuestId"] <= 30000) for r in raid):
        log("!! Raid: есть номера вне диапазона 20000…30000 — ветка «B» трекера выбирается по нему"); bad += 1

    kinds = sorted({b["Kind"] for b in books})
    if kinds != [0]:
        log("ВНИМАНИЕ: вид квеста принимает значения %s — нотисы 3580+вид" % kinds)

    log("СВЕРКА: расхождений %d" % bad)

    if csv_only:
        return
    try:
        import unreal  # noqa: F401
    except Exception:
        log("UE недоступен — записаны только JSON в %s" % OUT_DIR)
        return
    # ★ 25.10.2026: `ASDA_QUEST_TABLES_ONLY=DT_QuestGuide[,…]` — залить только перечисленные таблицы (точечный
    #   переимпорт без записи остальных ассетов; JSON выше пишутся все — они промежуточные).
    only = {s.strip() for s in os.environ.get("ASDA_QUEST_TABLES_ONLY", "").split(",") if s.strip()}
    for name, rs, rows, key, fn in jobs:
        if only and name not in only:
            log("%s: пропуск (ASDA_QUEST_TABLES_ONLY)" % name)
            continue
        if name == "DT_QuestGuide" and guide_blocked:
            log("!! DT_QuestGuide: НЕ записан — ведомость гида не сошлась (ревизия Н4)")
            continue
        fill_dt(name, rs, texts[name])


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main(csv_only=("--csv-only" in _sys.argv))
