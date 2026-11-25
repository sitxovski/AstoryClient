# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_nsr_dialog.py — ЭТАП Э0: ДАННЫЕ ОКНА ДИАЛОГА NPC из `Nsr.bsz`.
#
# ══════════════════════════════ ЧТО ИМПОРТИРУЕТСЯ И ЗАЧЕМ ═══════════════════════════════════════
# Окно диалога NPC (JUI `NPC_LINE NPC`, IDNum 570) наполняется ЦЕЛИКОМ из файла-сценария
# `<nsrName>.NSR` внутри ZIP-контейнера `./Data/NPC/Episode/NPC/Nsr.bsz` (187 файлов). До этого
# захода в порт был перенесён ТОЛЬКО блок 64×144 (записи-команды, → `DT_NpcQuestScript`,
# импортёр `asda_import_npc_quest_data.py`) — его хватает квестовым меткам над головой, но не
# окну: ни подписей кнопок, ни кодов команд, ни самих реплик там нет.
#
# Здесь достаются ТРИ недостающих куска (границы блоков — из эталонного загрузчика
# `CNpcEpisodeData_LoadNSR_Bank0@0xD9A990`, npc_mob_pet.c:2841; подробная раскладка со ссылками на
# конкретные строки эталона — в шапке `Source/AsdaReborn/Public/Npc/AsdaNpcDialogTypes.h`):
#
#   1) DT_NpcNsrHeader — шапка блока NSR: npcId (+0), ЗАГОЛОВОК окна (+8, идёт в `NPC_TNAME` 573),
#      число страниц (+0x28), число кнопок меню (+0x2C).
#   2) DT_NpcNsrButton — блок 6×848 по смещению +0x2430: ПОДПИСЬ кнопки (+2) и до 5 под-записей
#      по 156 байт с КОДАМИ КОМАНД 30001…30088 / 31000 (+12), параметром (+44) и состоянием (+148).
#   3) DT_NpcNsrPage  — ростер talk-записей (страниц) по 31452 байта, лежащий сразу за блоком:
#      talkId (+2), число шагов (+14), подписи и состояния шагов, ТЕКСТ реплики (хвост файла).
#
# ═══════════════════════════════ ПОЧЕМУ ЗАЛИВКА ЧЕРЕЗ JSON ══════════════════════════════════════
# `fill_data_table_from_csv_string` разбирает вложенные структуры («массив структур внутри
# структуры») только текстом ImportText со скобками — форма хрупкая и молча теряет поля. У строк
# страниц вложенность двойная (страница → шаги → под-записи шагов), поэтому используется
# `fill_data_table_from_json_string`: у него вложенность родная. CSV дублируем рядом ТОЛЬКО для
# страниц/кнопок в «плоском» виде — как читаемый глазами артефакт сверки, в DataTable он не идёт.
#
# ═══════════════════════════ РУССКИЙ ТЕКСТ: ВТОРОЙ КЛИЕНТ, СЛИЯНИЕ ПО `talkId` ══════════════════
# `Nsr.bsz` нашего клиента (`AsdaChroniclesInterlude_MINE`) — АНГЛИЙСКИЙ: строка с живого кадра
# «Oh, [NAME]! Hmmm. Do you have some time? …» лежит РОВНО в нём (`clark.nsr`, `shuren.nsr`).
# Русские реплики — в `Nsr.bsz` клиента «Фантазиум» (@@ASTORY_DEV_ROOT@@\Donors\Fantazium, полностью русская
# сборка 2012): тот же формат, строки CP1251. Тем же способом (слияние строковых полей по ID,
# числа не трогаем) уже русифицированы предметы/таблицы/`error list.bin` — см.
# @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\Translate\README.md; каталог Episode\NPC\*.nsr в то слияние НЕ попал — дыра
# закрывается здесь.
#   · сопоставление — по (имя сценария, `talkId`), НЕ по индексу страницы: у 4 файлов из 184 общих
#     число страниц у клиентов разное (`142npc` 13↔3, `144npc` 12↔3, `177npc` 8↔9, `tomb1` 1↔8);
#   · переносятся ТОЛЬКО строки: заголовок сценария, подписи кнопок меню, текст страницы, подписи
#     шагов. Коды команд, talkId, условия, награды, состояния — из нашего клиента, байт в байт;
#   · пары нет либо она пустая → остаётся строка нашего клиента (правило `merge_translate.py`:
#     `[FANTA-ПУСТО]` не заменяем);
#   · покрытие печатается числами в конце прогона (`РУС:` строки лога).
#
# ═══════════════════════════════════════ КОДИРОВКА ══════════════════════════════════════════════
# Строки в `.NSR` — CP949 (корейский клиент): проверено байтами `A1 AF` = «’» в текстах страниц и
# `B7CE C0CC B5BF C7D1 B4D9` = «로 이동한다.» в подписи кнопки `55npc.nsr`. Русский текст живёт НЕ
# здесь, а в `./Data/NPC/Episode/Talk/<nsrName>.bin` (CP1251) — это ОТДЕЛЬНЫЙ источник, который
# эталон подставляет только для страниц, вызванных командой NSR 122
# (`npcTalkGetEpisodeRecordOff@0xD94D40` → случайная из 30 записей по 208 байт). Он уже перенесён
# в `/Game/AsdaAssets/Text/Dialogue/DT_Talk` и здесь НЕ трогается.
#
# ═══════════════════════════════════════ СВЕРКА ═════════════════════════════════════════════════
# После записи скрипт заново разбирает `Nsr.bsz` и сравнивает поле-в-поле с тем, что попало в JSON,
# печатая «расхождений 0». Плюс отдельная контрольная проверка из плана реверса:
#   ian    → «Sell Items»→30002, «Buy Items»→30001, «Item Repair»→30069
#   rachel → «Store Items»→30011, «Retrieve Items»→30012
#
# Запуск: Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_import_nsr_dialog.py
#   без UE:  python asda_import_nsr_dialog.py --csv-only   (только JSON/CSV на диск + сверка)
import json
import os
import os as _os
import struct
import sys as _sys
import zipfile

_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_bintable as _BT     # ★ дедуп №2 В1 Т5: общий разбор C-строк
import asda_ru_dt_overrides as _RO     # ★ 14.10.2026 (names11 этап 3): ведомость русских правок вложенных DT

NSR_ZIP = os.path.join(_P.DATA, r"NPC\Episode\NPC\Nsr.bsz")
# Русский донор: тот же файл у клиента «Фантазиум». Числовые поля отсюда НЕ берутся (см. шапку).
NSR_ZIP_RU = os.path.join(_P.ROOT, "Donors", "Fantazium", "Data", "NPC", "Episode", "NPC", "Nsr.bsz")

OUT_DIR = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Data", "NPC")
# ★ 27.09.2026 (clan_btn_6099_fix, МЕЛК-1 ревизии): ВХОДНОЙ файл русских страниц JOB4 — своя константа, НЕ от `OUT_DIR`.
#   Прежде `read_ru` искал его в каталоге вывода: подмена `OUT_DIR` на scratch молча выбрасывала 138 страниц JOB4 и дала
#   ложную тревогу «276 ручных реплик» (опровергнуто ревизией). Нет файла — строка `!!` в логе, не тишина.
NSR_RU_EXTRA = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Data", "NPC", "NsrRuExtra.json")
PKG = "/Game/AsdaAssets/Data/NPC"

RS_HEADER = "/Script/AsdaReborn.AsdaNsrHeaderRow"
RS_BUTTON = "/Script/AsdaReborn.AsdaNsrButtonRow"
RS_PAGE   = "/Script/AsdaReborn.AsdaNsrPageRow"

# ── эталонные константы раскладки (см. шапку) ───────────────────────────────────────────────────
BLK_OFF, BLK_LEN = 4, 14352          # блок NSR: файл[4 … 4+14352)
OFF_NPCID, OFF_TITLE, OFF_ENTRY, OFF_MENU = 0x00, 0x08, 0x28, 0x2C
OFF_CMDS, CMD_STRIDE, CMD_COUNT = 0x30, 144, 64
OFF_BTNS, BTN_STRIDE, BTN_COUNT = 0x2430, 848, 6
# Предел ВИДИМЫХ кнопок меню: `uiQuestList_populateSteps@0xDA2600` — `menuCount < 6`, контролы case 0…4 (27.09, МЕЛК-2).
MENU_VISIBLE_MAX = 5
BTN_SUB_BASE, BTN_SUB_STRIDE, BTN_SUB_COUNT = 68, 156, 5
PAGE_STRIDE = 31452
PAGE_TEXT_PTR = 31448
STEP_BASE, STEP_STRIDE, STEP_COUNT = 16, 5228, 6
STEP_SUB_BASE, STEP_SUB_STRIDE, STEP_SUB_COUNT = 68, 1032, 5
# ── КВЕСТОВАЯ ЧАСТЬ ПОД-ЗАПИСИ ШАГА (1032 б) — раскладка закрывает запись байт-в-байт:
#     +0…+3    не читается ни одним эталонным читателем
#     +4       i32 State
#     +8…+155  блок условия (148 б, `parse_cond`)
#     +156/158/160 u16 Res0/Res1/Res2 (talkId результата)
#     +164 + 4*j (j = 0…5)  i32 «цель j активна»  (`quest_GetEntryProgressState@0xD892D0` → `> 0`,
#                            `gridScanAnyActiveCell@0xD7E2E0` → `> -1`)
#     +188 + 140*j (j = 0…5) БЛОК ЦЕЛИ, 140 байт   (188 + 6*140 = 1028 — сходится)
#     +1028    i32 questId «этот шаг ВЫДАЁТ квест» (`quest_GetEntryProgressState` → возврат 1;
#                            он же уходит в `buffFindIconByBuffId` при отправке 5043 режима 1)
OBJ_BASE, OBJ_STRIDE, OBJ_COUNT, OBJ_ACTIVE = 188, 140, 6, 164
OBJ_ACCEPT = 1028


def log(msg):
    try:
        import unreal
        unreal.log_warning("[nsr-dlg] %s" % msg)
    except Exception:
        print("[nsr-dlg] %s" % msg)


# ── строки ──────────────────────────────────────────────────────────────────────────────────────
def cstr(buf, off, maxlen, enc="cp949"):
    """C-строка из буфера. Кодировка нашего клиента — CP949, русского донора — CP1251 (см. шапку);
    битые байты не роняют импорт. Тело — общий `asda_bintable` (дедуп №2 В1 Т5)."""
    raw = _BT.cstr_bytes(buf, off, maxlen)
    # У 4 файлов (185/186/198/199npc) в слоте кнопки лежит НЕ текст, а бинарный мусор
    #   (`D7 74 CC 31 D7 74 01`). Данные не выбрасываем — кладём «как читается» (cp1251 с заменой),
    #   чтобы расхождение было видно глазами, а не пропало.
    return _BT.decode_text(raw, enc, fallback="cp1251")


# ── условие (144-байтовый блок, общий для кнопок и шагов) ───────────────────────────────────────
def parse_cond(buf, o):
    """`o` — НАЧАЛО блока условия = начало под-записи + 8.

    ★★★ ПРОВЕРЕНО НА БАЙТАХ (зонд `_probe_nsr_cond.py`, все 187 файлов):
      · сам сдвиг +8 подтверждён — при нём диапазон уровня осмыслен у 812 под-записей из 812
        (`min <= max <= 200`), при любом другом — ни у одной;
      · НО ВНУТРИ блока пара «уровень» стоит на +6/+8, а НЕ на +8/+10, как у 144-байтовой
        записи сценария. Эталон диалога читает ровно так:
        `playerClassRangeMatch(u16 cond+8 /*max*/, u16 cond+6 /*min*/)`
        (`netSerializeCharStatPacketA@0xD871C0` от под-записи+16/+14,
         `netSerializeCharStatPacketB@0xD88F00` от cond+8/+6 — одно и то же место).
        Пример из `108npc.nsr` p3 s0 m0: cond+6 = 140, cond+8 = 150, cond+10 = 0 — «уровень 140…150».
      ⇒ Поля названы по ФАКТУ: LevelMin = cond+6, LevelMax = cond+8, Spare10 = cond+10.
        Прежние имена (`Mark`/`LevelMin`/`LevelMax`) были перенесены из записи сценария и
        сдвигали диапазон уровня на 2 байта."""
    I = lambda k: struct.unpack_from("<i", buf, o + k)[0]
    U = lambda k: struct.unpack_from("<H", buf, o + k)[0]
    S = lambda k: struct.unpack_from("<h", buf, o + k)[0]
    return dict(
        Opcode=I(0), Spare4=U(4),
        LevelMin=U(6), LevelMax=U(8), Spare10=U(10),
        ClassId=I(12), ClassMode=I(16),
        QuestFreeA=I(20), QuestDone=I(24), QuestActive=I(28), QuestFreeB=I(32), QuestStage=I(36),
        ObjIds=[I(40), I(48), I(56), I(64), I(72)],
        ObjCounts=[U(44), U(52), U(60), U(68), U(76)],
        ObjFlagNone=U(78), ObjFlagAny=U(80),
        ReqItemIds=[I(84), I(92), I(100), I(108), I(116)],
        ReqItemCounts=[U(88), U(96), U(104), U(112), U(120)],
        HaveItemId=I(124), BagTabA=I(128), BagTabB=I(132),
        BagSlot136=U(136), Misc140=I(140), BagSlot142=U(142), Misc144=I(144),
    )


# ── блок ЦЕЛИ шага (140 байт) ───────────────────────────────────────────────────────────────────
def parse_obj(buf, o):
    """Поля, которые РЕАЛЬНО читают эталонные функции (остальные 140 байт ни один читатель среза
       не трогает — в комментарии к каждому полю указан читатель):
         +4   i32  questId  — `uiQuestRewardPanelPopulate@0xD93F90` (название эпизода) и
                              отправка 5043 режима 2 (`sendStartQuestBookProcess`, `removeBuffId`)
         +24  u16  вид цели — `questNpc_IsTargetAvailable@0xD894C0` (0 или 1)
         +52  i32 / +60 i32  — там же (пара «набрано / нужно»); они же категории наград
         +90  u16            — там же
         +136 i32            — там же (порог)
         +16/+20/+28/+32/+36/+40/+44/+48(u16)/+52/+60/+68/+76/+84 — `questStageBuildStatFlags@0xD7E950`
                              (сколько слотов награды занято; > 6 ⇒ панель наград не строится)"""
    I = lambda k: struct.unpack_from("<i", buf, o + k)[0]
    U = lambda k: struct.unpack_from("<H", buf, o + k)[0]
    return dict(
        QuestId=I(4), Kind24=U(24), Flag90=U(90), Need136=I(136),
        A16=I(16), A20=I(20), A28=I(28), A32=I(32), A36=I(36), A40=I(40), A44=I(44),
        A48=U(48), A52=I(52), A60=I(60), A68=I(68), A76=I(76), A84=I(84),
        # ★ ДОИМПОРТ 2026-08-20 (попап наград EPISODEPOP_WIN 540).
        #   КОЛИЧЕСТВА предметных наград: `itemSlotSetupEquipSowel@0xD7F130` case 8…12 читает пару
        #   «id (i32) + количество (u16 через +4)»: 240/244, 248/252, 256/260, 264/268, 272/276
        #   в смещениях эталона = +52/+56, +60/+64, +68/+72, +76/+80, +84/+88 в смещениях блока цели.
        C56=U(56), C64=U(64), C72=U(72), C80=U(80), C88=U(88),
        # ★ ВТОРОЙ блок награды — ВЫБИРАЕМАЯ (`questRewardSlotParse@0xD81460`), смещения эталона
        #   280/284/288/292+296/300+304/308+312/316+320 = +92/+96/+100/+104+108/+112+116/
        #   +120+124/+128+132. Виды: 2, 3, 5, 6, 10, 11, 12.
        S92=I(92), S96=I(96), S100=I(100), S104=I(104), S108=U(108),
        S112=I(112), S116=U(116), S120=I(120), S124=U(124), S128=I(128), S132=U(132),
    )


def parse_objs(buf, sb):
    """Шесть блоков цели. Пишем их ТОЛЬКО у под-записей, где они вообще заполнены (446 из 13150) —
       иначе таблица распухает на 78 тысяч пустых структур, ничего не добавляя."""
    active = [struct.unpack_from("<i", buf, sb + OBJ_ACTIVE + 4 * j)[0] for j in range(OBJ_COUNT)]
    accept = struct.unpack_from("<i", buf, sb + OBJ_ACCEPT)[0]
    raw = buf[sb + OBJ_BASE: sb + OBJ_BASE + OBJ_STRIDE * OBJ_COUNT]
    if not any(v > 0 for v in active) and accept <= 0 and not any(raw):
        return []
    return [parse_obj(buf, sb + OBJ_BASE + OBJ_STRIDE * j) for j in range(OBJ_COUNT)]


# ── разбор одного .NSR ──────────────────────────────────────────────────────────────────────────
def parse_nsr(nsr_name, blob, enc="cp949"):
    """Возвращает (header, buttons, pages). Размеры проверяются арифметикой, а не «на глаз»."""
    if len(blob) < BLK_OFF + BLK_LEN:
        raise ValueError("%s: файл короче блока NSR (%d < %d)" % (nsr_name, len(blob), BLK_OFF + BLK_LEN))
    blk = blob[BLK_OFF:BLK_OFF + BLK_LEN]

    entry_count = struct.unpack_from("<I", blk, OFF_ENTRY)[0]
    menu_count = struct.unpack_from("<H", blk, OFF_MENU)[0]
    header = dict(
        NsrName=nsr_name,
        NpcId=struct.unpack_from("<I", blk, OFF_NPCID)[0],
        Title=cstr(blk, OFF_TITLE, 32, enc),
        EntryCount=entry_count,
        MenuCount=menu_count,
    )

    # ── кнопки меню ──
    buttons = []
    for i in range(BTN_COUNT):
        o = OFF_BTNS + BTN_STRIDE * i
        caption = cstr(blk, o + 2, 66, enc)
        subs = []
        for j in range(BTN_SUB_COUNT):
            b = o + BTN_SUB_BASE + BTN_SUB_STRIDE * j
            subs.append(dict(
                State=struct.unpack_from("<H", blk, b + 148)[0],
                TalkId=struct.unpack_from("<h", blk, b + 4)[0],
                Flag=struct.unpack_from("<H", blk, b + 6)[0],
                CmdCode=struct.unpack_from("<H", blk, b + 12)[0],
                Param=struct.unpack_from("<i", blk, b + 44)[0],
                Cond=parse_cond(blk, b + 8),   # блок условия начинается на +8 от под-записи
            ))
        # Пустой слот (ни подписи, ни живой под-записи, и он вне menuCount) в таблицу не кладём:
        #   эталон показывает только i < menuCount, а 187×6 пустых строк лишь мешают читать.
        if not caption and all(s["State"] == 0 for s in subs) and i >= menu_count:
            continue
        buttons.append(dict(
            NsrName=nsr_name, ButtonIndex=i,
            SlotIndex=struct.unpack_from("<H", blk, o)[0],
            Caption=caption, Subs=subs,
        ))

    # ── страницы (talk-записи) + хвост со строками ──
    roster = BLK_OFF + BLK_LEN
    lens_off = roster + PAGE_STRIDE * entry_count
    need = lens_off + 4 * entry_count
    if len(blob) < need:
        raise ValueError("%s: файл короче ростера страниц (%d < %d)" % (nsr_name, len(blob), need))
    lens = [struct.unpack_from("<I", blob, lens_off + 4 * k)[0] for k in range(entry_count)]
    text_at = lens_off + 4 * entry_count
    # ★ ДЕФЕКТ ДАННЫХ ОРИГИНАЛА (D10, найден этим импортёром): у `2npc.nsr` хвост строк ОБРЕЗАН —
    #   объявлено 37 + 53 = 90 байт, в файле после ростера осталось 37. Эталонный загрузчик читает
    #   строки подряд `fread(new char[len+1], len, 1)`, поэтому второй странице достаётся МУСОР из
    #   неинициализированной памяти. Импорт не роняем: недостающее отдаём пустой строкой и пишем
    #   предупреждение — иначе одна битая запись сносит все 187 файлов.
    if text_at + sum(lens) != len(blob):
        log("!! %s: хвост строк обрезан — объявлено %d байт, доступно %d (дефект данных оригинала)"
            % (nsr_name, sum(lens), len(blob) - text_at))

    pages = []
    cur = text_at
    for i in range(entry_count):
        r = roster + PAGE_STRIDE * i
        talk_id = struct.unpack_from("<h", blob, r + 2)[0]
        step_count = struct.unpack_from("<h", blob, r + 14)[0]
        raw = blob[cur:min(cur + lens[i], len(blob))]
        cur += lens[i]
        try:
            text = raw.rstrip(b"\0").decode(enc)
        except UnicodeDecodeError:
            text = raw.rstrip(b"\0").decode("cp1251", "replace")

        steps = []
        for k in range(max(0, min(step_count, STEP_COUNT))):
            base = r + STEP_BASE + STEP_STRIDE * k
            ssubs = []
            for m in range(STEP_SUB_COUNT):
                sb = base + STEP_SUB_BASE + STEP_SUB_STRIDE * m
                ssubs.append(dict(
                    State=struct.unpack_from("<i", blob, sb + 4)[0],
                    BranchTalkId=struct.unpack_from("<H", blob, sb + 12)[0],
                    Res0=struct.unpack_from("<H", blob, sb + 156)[0],
                    Res1=struct.unpack_from("<H", blob, sb + 158)[0],
                    Res2=struct.unpack_from("<H", blob, sb + 160)[0],
                    Cond=parse_cond(blob, sb + 8),
                    AcceptQuestId=struct.unpack_from("<i", blob, sb + 1028)[0],
                    ObjActive=[struct.unpack_from("<i", blob, sb + 164 + 4 * jj)[0] for jj in range(OBJ_COUNT)],
                    Objs=parse_objs(blob, sb),
                ))
            steps.append(dict(Caption=cstr(blob, base + 2, 66, enc), Subs=ssubs))

        pages.append(dict(NsrName=nsr_name, PageIndex=i, TalkId=talk_id,
                          StepCount=step_count, Text=text, Steps=steps))
    return header, buttons, pages


# ── ПОДПИСЬ КНОПКИ ↔ ЕЁ ДЕЙСТВИЕ: ключ сопоставления с русским донором ──────────────────────────
def btn_sig(button):
    """«Что кнопка ДЕЛАЕТ» — кортеж живых под-записей (State, CmdCode, Param).

    ★ ИСПРАВЛЕНИЕ ДЕФЕКТА ИМПОРТА (2026-08-25, D-CRYSTAL-1): русские подписи раньше подставлялись
      по `ButtonIndex`. Это неверно: у донора «Фантазиум» ПОРЯДОК кнопок в меню местами свой.
      Сплошная сверка 184 общих сценариев показала РОВНО ОДИН такой файл — `50npc` (кристалл
      перемещения в Силарисе, 4 кнопки): наш клиент Flammio(7)/Alpen(5)/SunnyCoast(14)/Conquest(13),
      донор Alpen(5)/SunnyCoast(14)/Conquest(13)/Flammio(7) — циклический сдвиг на 1. Оба файла
      САМИ ПО СЕБЕ верны (подпись = свой Param, и цена в подписи сходится с серверным
      `GameServer\\MapData_Info\\MapWarpCost.txt`), ломала их именно склейка по индексу: игрок
      видел «Альпия», а улетал в Пламению.
      Теперь ключ — ДЕЙСТВИЕ кнопки; индекс остаётся запасным вариантом (см. `apply_ru`).
      ОТКАТ: вернуть в `apply_ru` строку `newv = _ru_pick((r["btn"].get(b["ButtonIndex"], "") …`
      и убрать словарь `btn_sig` из `read_ru` — этот файл единственный источник правки.
    """
    return tuple((s["State"], s["CmdCode"], s["Param"])
                 for s in button["Subs"] if s["State"] or s["CmdCode"])


# ── РУССКИЙ ДОНОР: строковые словари из `Nsr.bsz` клиента «Фантазиум» ───────────────────────────
def read_ru():
    """{nsr: {"title": str, "btn": {i: caption}, "btn_sig": {sig: caption},
              "page": {talkId: {"text": str, "cap": {k: caption}}}}}

    Числа отсюда НЕ берутся — только строки. Файл разбирается ТЕМ ЖЕ `parse_nsr`, что и наш
    клиент: раскладка у обоих клиентов совпадает (проверено — 187 и 195 файлов разобраны без
    единой ошибки, хвост строк сходится)."""
    if not os.path.isfile(NSR_ZIP_RU):
        log("!! русский донор не найден: %s — текст останется английским" % NSR_ZIP_RU)
        return {}
    out = {}
    z = zipfile.ZipFile(NSR_ZIP_RU)
    for n in sorted(x for x in z.namelist() if x.lower().endswith(".nsr")):
        nsr = os.path.basename(n)[:-4].lower()
        try:
            h, b, p = parse_nsr(nsr, z.read(n), "cp1251")
        except Exception as e:
            log("!! донор %s не разобран: %s" % (nsr, e))
            continue
        rec = dict(title=h["Title"], btn={}, btn_sig={}, page={})
        dup = set()
        for r in b:
            rec["btn"][r["ButtonIndex"]] = r["Caption"]
            # Ключ-действие. Пустое действие (кнопка без под-записей) ключом быть не может;
            #   неуникальное — тоже (две кнопки с одинаковым действием различимы только индексом).
            sig = btn_sig(r)
            if not sig or sig in dup:
                continue
            if sig in rec["btn_sig"]:
                dup.add(sig)
                del rec["btn_sig"][sig]
                continue
            rec["btn_sig"][sig] = r["Caption"]
        for r in p:
            # у одного talkId в доноре может быть несколько страниц — берём ПЕРВУЮ (как эталонный
            #   поиск `for (…) if (talkId == rec.talkId) break;`).
            if r["TalkId"] in rec["page"]:
                continue
            rec["page"][r["TalkId"]] = dict(
                text=r["Text"], cap={k: st["Caption"] for k, st in enumerate(r["Steps"])})
        out[nsr] = rec
    # ★ JOB4 (2026-09-11): русские тексты НОВЫХ страниц 4-й профессии (136/138/140/141npc), которых
    #   у донора Фантазиум нет — источник EN и перевод: Reports/job4/JOB4_CHAIN_TEXTS_RU.md.
    #   Дополняют словарь донора ТОЛЬКО там, где у донора такой (сценарий, talkId) нет; числа —
    #   по-прежнему из нашего клиента. ОТКАТ: удалить этот блок (и файл NsrRuExtra.json).
    #   ★ 27.09.2026 (МЕЛК-1): путь — константа `NSR_RU_EXTRA`, не `OUT_DIR`; пропажа файла и «добавлено меньше, чем в файле» видны в логе.
    extra_path = NSR_RU_EXTRA            # входной файл русских страниц JOB4
    if not os.path.isfile(extra_path):
        log("!! NsrRuExtra.json не найден (%s) — страницы JOB4 будут английскими" % extra_path)
        return out
    with open(extra_path, encoding="utf-8") as f:
        extra = json.load(f)
    added = 0                            # страниц реально добавлено в словарь
    total = 0                            # записей-страниц в файле
    skipped = []                         # (сценарий, talkId), которые уже есть у донора — файл их не перекрывает
    for nsr, pages in extra.items():
        if nsr.startswith("_"):
            continue
        rec = out.setdefault(nsr, dict(title="", btn={}, btn_sig={}, page={}))
        for talk, v in pages.items():
            total += 1
            if int(talk) in rec["page"]:
                skipped.append((nsr, int(talk)))
                continue
            rec["page"][int(talk)] = dict(text=v["text"], cap={int(k): c for k, c in v.get("cap", {}).items()})
            added += 1
    log("РУС: доп. страниц из NsrRuExtra.json: %d из %d записей файла" % (added, total))
    if added != total:
        log("!! NsrRuExtra.json: %d записей не добавлены — такие (сценарий, talkId) уже есть у донора: %s"
            % (total - added, skipped[:10]))
    return out


# Строка «без единого знака письма»: только «?», пробелы и пунктуация. Кириллица, латиница, цифры,
#   корейский (хангыль и чосон-иероглифы), кана — считаются знаками письма.
_RE_LETTER = __import__("re").compile(
    "[0-9A-Za-zЀ-ӿԀ-ԯ가-힣一-鿿぀-ヿ]")


def _ru_dead(s):
    """★ ИСПРАВЛЕНИЕ ДЕФЕКТА ИМПОРТА (2026-08-25, D-CRYSTAL-2): у донора «Фантазиум» часть строк,
    которые в оригинале КОРЕЙСКИЕ, сохранена как ряд «?» — при переводе клиента корейские байты
    были потеряны безвозвратно (CP1251 их не кодирует). Пример: `55npc` (NPC 55, у него кнопка
    телепорта 30031) — наш клиент `'추석 이벤트 NPC'`, донор `'?? ??? NPC'`.
    Старое правило считало «??» полноценным текстом и ЗАТИРАЛО им оригинал: данные терялись
    там, где перевода не было вовсе. Такую строку донора приравниваем к пустой — остаётся
    оригинал клиента. Затронуто 86 строк: 6 заголовков, 1 подпись кнопки, 40 текстов страниц,
    39 подписей шагов. Под «?» лежал не только корейский: у 175/177/198/199npc и tomb1 там были
    нормальные английские имена (Dispatched Broker, Chaser Manny, Mercenary Mikamion,
    Mercenary Karas, Grave Of the Prideless). Строки с ЧАСТИЧНЫМ переводом не трогаются.
    Критерий — «знаков “?” больше, чем знаков письма»: `'?? ??? NPC'` = 5 против 3 → мусор,
    а честный русский вопрос `'Кто ты??'` = 2 против 6 → текст. Порог именно такой, потому что
    иначе латинский хвост вроде «NPC» спасал бы строку, у которой всё остальное потеряно.
    ОТКАТ: убрать вызов `_ru_dead` из `_ru_pick`."""
    if not s or "?" not in s:
        return False
    return s.count("?") > len(_RE_LETTER.findall(s))


def _ru_pick(ru_text, own_text):
    """Правило замены (то же, что у `merge_translate.py`): берём донора, только если у него ТЕКСТ
    ЕСТЬ. Пустая строка донора наш текст не затирает; строка из одних «?» — тоже (см. `_ru_dead`)."""
    if _ru_dead(ru_text):
        return own_text
    return ru_text if (ru_text and ru_text.strip()) else own_text


def apply_ru(headers, buttons, pages, ru):
    """Подставляет русские строки по (сценарий, talkId/индекс). Возвращает статистику покрытия."""
    st = dict(title=[0, 0], btn=[0, 0], page=[0, 0], step=[0, 0])   # [заменено, всего непустых]

    for h in headers:
        r = ru.get(h["NsrName"])
        if h["Title"].strip():
            st["title"][1] += 1
            newv = _ru_pick(r["title"] if r else "", h["Title"])
            if newv != h["Title"]:
                h["Title"] = newv
                st["title"][0] += 1

    for b in buttons:
        r = ru.get(b["NsrName"])
        if b["Caption"].strip():
            st["btn"][1] += 1
            # ★ Ключ — ДЕЙСТВИЕ кнопки, а не её номер (см. `btn_sig`: порядок кнопок у донора
            #   местами свой, склейка по номеру давала чужую подпись — дефект D-CRYSTAL-1).
            #   Запасной вариант по номеру нужен там, где действия нет или оно неуникально.
            sig = btn_sig(b)
            if not r:
                ru_cap = ""
            elif sig:
                # Есть действие → берём подпись донора ТОЛЬКО у кнопки с ТЕМ ЖЕ действием.
                #   Нет такой у донора → русской подписи нет, остаётся наша (английская).
                ru_cap = r["btn_sig"].get(sig, "")
            else:
                # Кнопка без под-записей (декоративная/пустая) — сопоставить нечем, идём по номеру.
                ru_cap = r["btn"].get(b["ButtonIndex"], "")
            newv = _ru_pick(ru_cap, b["Caption"])
            if newv != b["Caption"]:
                b["Caption"] = newv
                st["btn"][0] += 1

    for pg in pages:
        r = ru.get(pg["NsrName"])
        q = r["page"].get(pg["TalkId"]) if r else None
        if pg["Text"].strip():
            st["page"][1] += 1
            newv = _ru_pick(q["text"] if q else "", pg["Text"])
            if newv != pg["Text"]:
                pg["Text"] = newv
                st["page"][0] += 1
        for k, step in enumerate(pg["Steps"]):
            if not step["Caption"].strip():
                continue
            st["step"][1] += 1
            newv = _ru_pick((q["cap"].get(k, "") if q else ""), step["Caption"])
            if newv != step["Caption"]:
                step["Caption"] = newv
                st["step"][0] += 1
    return st


def read_all():
    z = zipfile.ZipFile(NSR_ZIP)
    names = sorted(n for n in z.namelist() if n.lower().endswith(".nsr"))
    headers, buttons, pages = [], [], []
    for n in names:
        nsr = os.path.basename(n)[:-4].lower()
        h, b, p = parse_nsr(nsr, z.read(n))
        headers.append(h)
        buttons.extend(b)
        pages.extend(p)
    # ★ 04.11.2026 (questmark-chmella): страницы донорских сценариев (tomb1) ДО русификации — русская ведомость
    #   (`ru_dt_overrides.json`) переводит их корейский текст. Ведомость строит `Tools/asda_nsr_donor_restore.py`.
    apply_donor_pages(headers, pages)
    # ★ РУСИФИКАЦИЯ — часть чтения, а не отдельный проход: тогда контрольная сверка `verify()`
    #   (она зовёт `read_all` повторно) сравнивает ОДИНАКОВО подготовленные данные.
    ru_stats = apply_ru(headers, buttons, pages, read_ru())
    # ★ 14.10.2026 (names11, этап 3, ревизия С-3): ведомость русских правок вложенных DT (Tools/data/ru_dt_overrides.json,
    #   asda_ru_dt_overrides.py) - переимпорт из .BIN не возвращает корейский текст реплик/квестов/имён NPC, которых нет в русской сборке.
    _RO.apply_rows("DT_NpcNsrHeader", headers, lambda r: r["NsrName"], log)
    _RO.apply_rows("DT_NpcNsrButton", buttons, lambda r: "%s_%d" % (r["NsrName"], r["ButtonIndex"]), log)
    _RO.apply_rows("DT_NpcNsrPage", pages, lambda r: "%s_%d" % (r["NsrName"], r["TalkId"]), log)
    apply_step_cond_fixes(pages)
    apply_turnin_pages(headers, pages)
    # ★ 01.11.2026 (item_dedup, этап 2): ссылки на предметы-дубли -> основной код (общий модуль asda_item_dedup, карта
    #   Tools/data/item_dedup_map.json): Cond.ObjIds/ReqItemIds/HaveItemId и Objs.A52…A84/S112…S128/Need136. ПОСЛЕ всех
    #   ведомостей (их «was» сверяется с сырьём .NSR). Часть чтения — сверка `verify()` видит то же. ASDA_NO_DEDUP_MAP=1 — выкл.
    import asda_item_dedup as _DD
    _dd = _DD.active_map()
    log("item_dedup: перекодировано ссылок на предметы-дубли — шапки %d, кнопки %d, страницы %d"
        % (_DD.remap_nsr(headers, _dd), _DD.remap_nsr(buttons, _dd), _DD.remap_nsr(pages, _dd)))
    return names, headers, buttons, pages, ru_stats


# ★ 31.10.2026 (bug-quest-follow, решение владельца «Б»): страницы сдачи 16 заданий, которых нет ни в одной версии данных
#   оригинала ([ИСПРАВЛЕНИЕ]; реплики утверждены владельцем). Ведомость строит `Tools/asda_quest_turnin_restore.py`; здесь
#   страницы ДОПИСЫВАЮТСЯ в конец своего NPC (номер страницы — следующий). Занятый TalkId — СТОП (ничего не пишется молча).
TURNIN_PAGES = _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "data", "nsr_turnin_pages.json")

# ★ 04.11.2026 (questmark-chmella): страницы сценариев, которых нет в нашей сборке, но есть у доноров (корейский оригинал,
#   см. `Tools/asda_nsr_donor_restore.py`). Дописываются в конец своего NPC; занятый TalkId — СТОП.
DONOR_SCRIPTS = _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "data", "nsr_donor_scripts.json")


def apply_donor_pages(headers, pages):
    if not _os.path.isfile(DONOR_SCRIPTS):
        return
    import json
    have = {(p["NsrName"], p["TalkId"]) for p in pages}
    n = 0
    for d in json.load(open(DONOR_SCRIPTS, encoding="utf-8"))["rows"]:
        for src in d["pages"]:
            pg = json.loads(json.dumps(src))
            if (pg["NsrName"], pg["TalkId"]) in have:
                raise RuntimeError("донорская страница %s_%d: TalkId уже занят в .NSR — ведомость устарела" % (pg["NsrName"], pg["TalkId"]))
            pg["PageIndex"] = 1 + max([p["PageIndex"] for p in pages if p["NsrName"] == pg["NsrName"]] or [-1])
            pages.append(pg)
            have.add((pg["NsrName"], pg["TalkId"]))
            n += 1
            for h in headers:
                if h["NsrName"] == pg["NsrName"]:
                    h["EntryCount"] += 1
    log("донорские страницы (ведомость nsr_donor_scripts): дописано %d" % n)


def apply_turnin_pages(headers, pages):
    if not _os.path.isfile(TURNIN_PAGES):
        log("ведомость страниц сдачи %s не найдена — страницы как в .NSR" % TURNIN_PAGES)
        return
    import json
    rows = json.load(open(TURNIN_PAGES, encoding="utf-8"))["rows"]
    have = {(p["NsrName"], p["TalkId"]) for p in pages}
    for r in rows:
        pg = dict(r["page"])
        if (pg["NsrName"], pg["TalkId"]) in have:
            raise RuntimeError("страница сдачи %s_%d: TalkId уже занят в .NSR — ведомость устарела" % (pg["NsrName"], pg["TalkId"]))
        pg["PageIndex"] = 1 + max([p["PageIndex"] for p in pages if p["NsrName"] == pg["NsrName"]] or [-1])
        pages.append(pg)
        have.add((pg["NsrName"], pg["TalkId"]))
        for h in headers:
            if h["NsrName"] == pg["NsrName"]:
                h["EntryCount"] += 1
    log("страницы сдачи заданий (ведомость): дописано %d" % len(rows))


# ★★★ 29.10.2026 (решение владельца «выровняй»): условия шагов «Принять задание», которые расходятся с СЕРВЕРНЫМ условием
#   квеста (NpcServer `Start_Quest()`), приводятся к нему. Ведомость `Tools/data/nsr_step_cond_fixes.json` строит только
#   генератор `Tools/asda_nsr_script_align.py`; здесь она применяется (часть чтения — сверка `verify()` видит то же).
#   `was` — значения ДО правки: изменился источник — правка не применяется (строка «!!»).
STEP_FIXES = _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "data", "nsr_step_cond_fixes.json")


def apply_step_cond_fixes(pages):
    if not _os.path.isfile(STEP_FIXES):
        log("ведомость условий шагов %s не найдена — шаги как в .NSR" % STEP_FIXES)
        return
    fixes = json.load(open(STEP_FIXES, encoding="utf-8"))["rows"]
    by_page = {(p["NsrName"], p["TalkId"]): p for p in pages}
    applied = stale = 0
    for f in fixes:
        p = by_page.get((f["nsr"], f["talk"]))
        try:
            cond = p["Steps"][f["step"]]["Subs"][f["sub"]]["Cond"]
        except (TypeError, IndexError, KeyError):
            cond = None
        if cond is None or any(cond.get(k) != v for k, v in f["was"].items()):
            stale += 1
            log("!! шаг %s/%d [%d/%d] изменился в .NSR после построения ведомости — правка не применена"
                % (f["nsr"], f["talk"], f["step"], f["sub"]))
            continue
        cond.update(f["set"])
        applied += 1
    log("условия шагов «Принять» по серверу: выровнено %d, устарело %d (ведомость %d)" % (applied, stale, len(fixes)))


# ── JSON для DataTable (ключ строки — поле "Name") ──────────────────────────────────────────────
# * DEDUP D10: telo pereyehalo v obshchuyu tochku `asda_dt_common.json_rows`
#   (ono zhilo v chetyryoh importyorah slovo v slovo). Zdes tonkiy psevdonim.
from asda_dt_common import json_rows  # noqa: E402


KEY_HEADER = lambda r: r["NsrName"]
KEY_BUTTON = lambda r: "%s_%d" % (r["NsrName"], r["ButtonIndex"])
KEY_PAGE   = lambda r: "%s_%d" % (r["NsrName"], r["TalkId"])


# ── сверка: повторный разбор источника против того, что уходит в таблицу ────────────────────────
def verify(headers, buttons, pages):
    _, h2, b2, p2, _ru = read_all()
    # ★ 27.09.2026: повторный разбор проходит ТЕ ЖЕ добавки порта, что и основной (`main`). Раньше их не было, и сверка
    #   всегда кончалась «!! кнопки: число строк разошлось» / «!! шапки: расхождение» — ложная тревога добавок.
    apply_extra_buttons(h2, b2)
    bad = 0
    for tag, a, b in (("шапки", h2, headers), ("кнопки", b2, buttons), ("страницы", p2, pages)):
        if len(a) != len(b):
            log("!! %s: число строк разошлось %d != %d" % (tag, len(a), len(b)))
            bad += 1
            continue
        for x, y in zip(a, b):
            if x != y:
                bad += 1
                if bad < 6:
                    log("!! %s: расхождение в %s" % (tag, x.get("NsrName")))
        log("%s: %d строк, расхождений %d" % (tag, len(b), bad))

    # Контрольные значения из плана реверса (§12.3 Э0).
    def btn_map(nsr):
        return {r["Caption"]: r["Subs"][0]["CmdCode"] for r in buttons if r["NsrName"] == nsr and r["Caption"]}

    # ★ Подписи здесь РУССКИЕ: после слияния с донором таблица хранит русские строки
    #   (английские оригиналы — «Sell Items» / «Buy Items» / «Item Repair» / «Store Items» /
    #   «Retrieve Items»). Коды команд не менялись — их и проверяем через подпись.
    for nsr, expect in (("ian", {"Продать вещи": 30002, "Купить вещи": 30001, "Ремонт вещи": 30069}),
                        ("rachel", {"Хранить вещи": 30011, "Взять вещи": 30012})):
        got = btn_map(nsr)
        for cap, code in expect.items():
            if got.get(cap) != code:
                bad += 1
                log("!! контроль %s: «%s» → %s, ожидалось %d" % (nsr, cap, got.get(cap), code))
        log("контроль %s: %s" % (nsr, got))
    return bad


# ── заливка DataTable (только под UE) ───────────────────────────────────────────────────────────
def fill_dt(name, row_struct_path, json_text):
    # ★ ДЕДУП Д10: сам шаг «RowStruct → DataTable → заливка → сохранение» на порт ОДИН
    #   (`asda_dt_common.fill_dt`). Здесь остаётся только своё: пакет, логгер, подсказка про
    #   заголовок C++ и признак успеха.
    import unreal
    from asda_dt_common import fill_dt as _fill
    dt = _fill(unreal, PKG, name, row_struct_path, json_text, log,
               fmt="json", missing_hint="Public/Npc/AsdaNpcDialogTypes.h")
    return dt is not None


# ═════════════════════ ★ ДОБАВКИ ПОРТА: КНОПКИ, КОТОРЫХ В ОРИГИНАЛЕ НЕТ ═════════════════════════
#   Оригинальные данные `Nsr.bsz` правим не файлом, а ЗДЕСЬ — иначе следующий переимпорт молча
#   сотрёт правку (файлы `NpcNsrButton.json` / `NpcNsrHeader.json` перезаписываются целиком).
#
#   ★ 146npc «Робот "Пре-Пре 19"» — кнопка МАГАЗИНА БУСТЕРОВ (окно 15300, код команды 30034).
#     Зачем: в оригинале у окна 15300 живого входа НЕТ вообще — код диалога 30034 в данных не
#     встречается ни у одного из 187 NPC, а сегменты «Бустер» и «Набор» премиум-меню клиент сам
#     гасит при инициализации (`sub_931C4A(pPremiumMenuBtn, 1)` и `(…, 3)`,
#     `AsdaStory_ida.c:420670-420671`). Решение владельца (2026-08-28): дать окну живой вход.
#     ★ Подпись «Магазин Бустеров» ЗАДАЛ ВЛАДЕЛЕЦ (2026-08-28). В таблицах игры готовой такой строки НЕТ:
#     есть лишь 99400 «Бустер» (сегмент меню) и фразы 99002/99003, где «магазин бустеров» идёт внутри предложения. `State = 1` ⇒ команда исполняется без проверки условия
#     (`RunMenuButton`, `AsdaNpcDialogSubsystem.cpp:1236`), поэтому `Cond` нейтрален.
EXTRA_BUTTONS = [
    #   ★ 132npc «Парикмахер Юки» — кнопка САЛОНА ПРИЧЁСОК (окно 15000, код команды 30029).
    #     Причина та же, что у робота: код 30029 в данных диалогов не встречается, а сегмент 2
    #     премиум-меню в оригинале ненажимаем — живого входа у окна не было. Решение владельца
    #     (2026-08-28). Подпись — из таблицы игры, строка 99402 «Салон причесок» (она же стояла на
    #     сегменте премиум-меню), ничего не выдумано.
    {
        "NsrName": "132npc",
        "Caption": "Салон причесок",   # loc 99402
        "CmdCode": 30029,
    },
    {
        "NsrName": "146npc",
        "Caption": "Магазин Бустеров",   # ★ подпись задал владелец
        "CmdCode": 30034,
    },
    #   ★ 131npc — кнопка ЭЛИТНОГО МАГАЗИНА (окно 6000 `SHELLITEM_STORE_WIN`, код команды 30030). Решение владельца
    #     26.09.2026. Основание в ДАННЫХ оригинала: код 30030 стоит у кнопки 0 ТОГО ЖЕ NPC 131 в файле
    #     `Data/NPC/Episode/BOOK/131NPC.NSR` (одинаковый во всех клиентах, 1 089 992 б; разобран этим же `parse_nsr`:
    #     npcId 131, menuCount 2 — кнопка 0 «アイテム購入» [State 1, 30030], кнопка 1 «装備アイテム取引» [State 1, 30032]),
    #     но этот файл клиент НЕ читает (папка BOOK — только `%s.QST`; сценарии — только `Nsr.bsz`), поэтому в игре
    #     кнопки не было ни у кого. Ветка команды в эталоне живая: `CGameUI_HandleControlCommand`, game_main.c:10081-10084
    #     (порт — `AsdaNpcDialogSubsystem.cpp`, case 30030). Кнопка 1 (30032, обмен предметами) НЕ переносится: окна
    #     обмена `CExchangeItem` в порту нет.
    #     Подпись — ИЗ ТАБЛИЦЫ ИГРЫ: подпись BOOK-файла японская («Покупка предметов», русской пары нет ни в одном
    #     клиенте), поэтому взята строка 30100 «Элитный магазин» — имя этого же окна в списке горячих клавиш
    #     («Открыть/закрыть окно» 30090…); ничего не выдумано.
    {
        "NsrName": "131npc",
        "Caption": "Элитный магазин",   # loc 30100
        "CmdCode": 30030,
    },
    #   ★ [ПОРТ] ella (NPC 107 «Командир рыцарей Элла») — кнопка РАСШИРЕНИЯ КЛАНА (окно 14450 `CLAN_INCREASE_BOX`,
    #     код команды 30027 → опкод 4136). Решение владельца 27.09.2026 (отчёт `Reports/owner_0927/clan_btn_6099.md`).
    #     В оригинале кнопки нет ни в одном наборе диалогов (пять деревьев данных, `who_clan.md` §«Что осталось»),
    #     хотя ветка команды и сервер живые (`CGameUI_HandleControlCommand`, порт — `AsdaNpcDialogSubsystem.cpp`,
    #     case 30027). Элла — единственный NPC с кнопками клана (30016 «Распустить», 30015 «Основать»).
    #     ★ МЕСТО: сразу за видимыми кнопками (`AfterVisible`), а НЕ в конец списка. У Эллы в данных 4 кнопки при
    #     `MenuCount = 3`: четвёртая («Переместиться в Долину Ночи», 30031) в оригинале СКРЫТА (эталон показывает первые
    #     `menuCount`). Дописать в конец значило бы поднять `MenuCount` до 5 и открыть скрытую кнопку. Поэтому новая
    #     кнопка встаёт в слот 3, скрытая уезжает в слот 4 (ключ `ella_3` → `ella_4`) и остаётся скрытой (`MenuCount = 4`;
    #     после добавки LostValley ниже — слот 5, `MenuCount = 5`).
    #     Подпись — строка порта 990349 «Расширить клан» (`RawAssets/Text/GameText.csv`, диапазон 99xxxx): готовой строки
    #     со смыслом «расширить клан» в таблицах игры нет (есть лишь фразы-нотисы 12451…12454). Импортёр берёт текст из
    #     таблицы по `CaptionLoc`: источник один, но в `DT_NpcNsrButton.Caption` лежит СНИМОК на момент импорта (27.09,
    #     МЕЛК-3 ревизии) — после правки 990349/605 в `GameText.csv` перезалить подписи точечно: `sync_extra_captions()`
    #     этого файла (только поле `Caption` строк-добавок, остальные строки DT не трогает). `State = 1` ⇒ без условия; гейты (клан → «Друг» →
    #     число расширений) делает само окно (`UAsdaClanSubsystem::OpenIncreaseWindow`), как у эталона.
    #     ОТКАТ: удалить эту запись и перезалить `DT_NpcNsrButton` / `DT_NpcNsrHeader`.
    {
        "NsrName": "ella",
        "CaptionLoc": 990349,            # «Расширить клан» (строка порта)
        "CmdCode": 30027,
        "AfterVisible": True,
    },
    #   ★ [ПОРТ] ella (NPC 107) — ТЕЛЕПОРТ В ЗАБЫТУЮ ЛОЩИНУ (LostValley, карта 5), код команды 30037 → опкод 5477, слово 5.
    #     Решение владельца 27.09.2026 «игрок должен просто входить в данжи» (отчёт `Reports/owner_0927/lostvalley_entry.md`).
    #     Вход в оригинале — ТОЛЬКО NPC-телепорт: полевых порталов на карту 5 нет ни в одном `*_PortalConfig.Ini` сервера;
    #     ветка 30037 живая (`CGameUI_HandleControlCommand`, game_main.c:10111 → `netSendReqWord_op5477(5)`; порт —
    #     `AsdaNpcDialogSubsystem.cpp`, case 30037), сервер принимает (`RecvContinent_PortalMove`, Total_PortalConfig
    #     `[PORTAL_INFO_5] Use=1`, прибытие 195,470; цена `MapWarpCost[3][5] = 0`). Но НИ ОДНА кнопка ни в одном наборе
    #     `.nsr` на 30037 не ссылается — привязки к NPC в этой сборке данных нет. Элла — телепортист Alpen (81,431),
    #     рядом с точкой возврата из LostValley (80,425).
    #     ★ Скрытая 4-я кнопка Эллы «Переместиться в Долину Ночи» — НЕ этот вход: 30031 param 2 → `WarpListTable` строка 2 →
    #     карта 4 (Night_Valley, 188,465). Её не трогаем, она остаётся скрытой (уезжает в слот 5).
    #     Подпись — ИЗ ТАБЛИЦЫ ИГРЫ: loc 605 «Забытая Лощина» (таблица имён карт 600+mapId); у телепортистов оригинала подпись
    #     кнопки — имя карты («Альпия» у 55npc), ничего не выдумано. `State = 1` ⇒ без условия (у 30037 в эталоне гейтов нет).
    #     ОТКАТ: удалить эту запись и перезалить `DT_NpcNsrButton` / `DT_NpcNsrHeader` (скрытая вернётся в слот 4).
    {
        "NsrName": "ella",
        "CaptionLoc": 991201,            # ★ 06.11.2026 решение владельца: «В Забытую Лощину» (было loc 605, голое имя карты)
        "CmdCode": 30037,
        "AfterVisible": True,
    },
]

# Таблица строк игры (источник `DT_GameText`) — отсюда берутся подписи добавок с `CaptionLoc`.
GAMETEXT_CSV = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Text", "GameText.csv")


def _gametext(loc_id):
    """Строка `GameText.csv` по id; нет строки — исключение (подпись кнопки пустой быть не должна)."""
    import csv
    with open(GAMETEXT_CSV, encoding="utf-8-sig", newline="") as f:
        for row in csv.DictReader(f):
            if row.get("Key") == str(loc_id):
                return row.get("SourceString", "")
    raise KeyError("GameText.csv: строки %d нет — подпись добавки не задана" % loc_id)


def _neutral_cond(cmd_code):
    """Условие «без ограничений»: ровно та же форма, что у записей из `Nsr.bsz`."""
    return {
        "Opcode": 0, "Spare4": cmd_code, "LevelMin": 0, "LevelMax": 0, "Spare10": 0,
        "ClassId": -1, "ClassMode": -1, "QuestFreeA": -1, "QuestDone": -1, "QuestActive": -1,
        "QuestFreeB": -1, "QuestStage": -1,
        "ObjIds": [-1, -1, -1, -1, -1], "ObjCounts": [0, 0, 0, 0, 0],
        "ObjFlagNone": 0, "ObjFlagAny": 0,
        "ReqItemIds": [-1, -1, -1, -1, -1], "ReqItemCounts": [0, 0, 0, 0, 0],
        "HaveItemId": -1, "BagTabA": -1, "BagTabB": -1, "BagSlot136": 0,
        "Misc140": 0, "BagSlot142": 0, "Misc144": 0,
    }


def apply_extra_buttons(headers, buttons):
    """Дописывает кнопки порта и поднимает `MenuCount` у их NPC. Идемпотентно."""
    for ex in EXTRA_BUTTONS:
        nsr = ex["NsrName"]
        mine = [b for b in buttons if b.get("NsrName") == nsr]
        if any(s.get("CmdCode") == ex["CmdCode"] for b in mine for s in b.get("Subs", [])):
            log("добавка: %s уже несёт код %d — пропуск" % (nsr, ex["CmdCode"]))
            continue
        caption = ex["Caption"] if "Caption" in ex else _gametext(ex["CaptionLoc"])   # подпись: литерал или строка таблицы
        idx = len(mine)                      # кнопки нумеруются подряд от нуля
        pos = len(buttons)                   # место в списке (порядок строк JSON)
        hdr = next((h for h in headers if h.get("NsrName") == nsr), None)   # шапка сценария (MenuCount)
        menu_was = int(hdr.get("MenuCount", 0)) if hdr else 0                # видимых кнопок до добавки
        if ex.get("AfterVisible"):
            idx = min(menu_was if hdr else len(mine), len(mine))
        # ★★ 27.09.2026 (clan_btn_6099_fix, МЕЛК-2 ревизии) ПРЕДОХРАНИТЕЛЬ ЛИМИТА — ДО любой правки данных.
        #   [ОРИГИНАЛ] `uiQuestList_populateSteps@0xDA2600` (ui_core.c:61593): `if ((unsigned)menuCount < 6)` → `switch (i)` с
        #   case 0…4 (пять контролов `NPC_SHOP_BUT00…04`); при menuCount ≥ 6 НЕ показывается НИ ОДНОЙ кнопки и не строятся
        #   шаги страницы (`sub_920EB3`/`listPanel_refreshSlotStates` внутри того же `if`). Порт повторяет это 1:1
        #   (`FillMenuButtons`: `MenuCount >= 6 → return`). Поэтому видимых — не больше MENU_VISIBLE_MAX (5), а записей кнопок
        #   у сценария — не больше BTN_COUNT (6, массив `+0x2430` блока NSR). Превышение = отказ импорта, а не тихая потеря меню NPC.
        menu_new = idx + 1 if ex.get("AfterVisible") else max(menu_was, idx + 1)   # MenuCount после добавки
        if menu_new > MENU_VISIBLE_MAX:
            raise ValueError("добавка %s код %d: MenuCount станет %d > %d — эталон при MenuCount >= 6 скрывает ВСЕ кнопки NPC "
                             "(uiQuestList_populateSteps@0xDA2600)" % (nsr, ex["CmdCode"], menu_new, MENU_VISIBLE_MAX))
        if len(mine) + 1 > BTN_COUNT:
            raise ValueError("добавка %s код %d: кнопок станет %d > %d — у блока NSR ровно %d слотов кнопок"
                             % (nsr, ex["CmdCode"], len(mine) + 1, BTN_COUNT, BTN_COUNT))
        if ex.get("AfterVisible"):
            # ★ Вставка сразу за видимыми (`MenuCount`): скрытые кнопки данных сдвигаются на слот вправо и остаются
            #   скрытыми, их содержимое не меняется — только номер слота (он же ключ строки DataTable).
            for b in mine:
                if b["ButtonIndex"] >= idx:
                    b["ButtonIndex"] += 1
                    b["SlotIndex"] = b["ButtonIndex"]
                    b["Name"] = "%s_%d" % (nsr, b["ButtonIndex"])
            hidden = [i for i, b in enumerate(buttons) if b.get("NsrName") == nsr and b["ButtonIndex"] > idx]
            pos = min(hidden) if hidden else (max(i for i, b in enumerate(buttons) if b.get("NsrName") == nsr) + 1 if mine else len(buttons))
        buttons.insert(pos, {
            "NsrName": nsr, "ButtonIndex": idx, "SlotIndex": idx, "Caption": caption,
            "Subs": [{
                "State": 1, "TalkId": 0, "Flag": 0, "CmdCode": ex["CmdCode"], "Param": -1,
                "Cond": _neutral_cond(ex["CmdCode"]),
            }],
            "Name": "%s_%d" % (nsr, idx),
        })
        # ★ Кнопки показываются в числе первых `MenuCount` штук и только пока их меньше шести
        #   (`FillMenuButtons`, `AsdaNpcDialogSubsystem.cpp:996`) — счётчик обязателен.
        for h in headers:
            if h.get("NsrName") == nsr and (ex.get("AfterVisible") or h.get("MenuCount", 0) <= idx):
                h["MenuCount"] = idx + 1
        log("добавка: %s кнопка %d «%s» → код %d (MenuCount %d → %d из %d возможных, кнопок %d из %d)"
            % (nsr, idx, caption, ex["CmdCode"], menu_was, menu_new, MENU_VISIBLE_MAX, len(mine) + 1, BTN_COUNT))


# Пакет строк кнопок диалога (снимок подписей добавок живёт здесь).
DT_BUTTON_PATH = PKG + "/DT_NpcNsrButton"


def sync_extra_captions(dry_run=False):
    """★ 27.09.2026 (clan_btn_6099_fix, МЕЛК-3 ревизии). ТОЧЕЧНАЯ перезаливка ПОДПИСЕЙ добавок с `CaptionLoc`.

    `apply_extra_buttons` кладёт в `DT_NpcNsrButton.Caption` СНИМОК строки `GameText.csv` на момент импорта. После правки
    строки (990349 «Расширить клан», 605 «Забытая Лощина») снимок устаревает — здесь он обновляется без переимпорта:
    экспорт ЖИВОЙ таблицы → замена ТОЛЬКО поля `Caption` у строк-добавок (NPC + код команды), где текст разошёлся →
    заливка → повторный экспорт и сверка «изменились ровно эти ключи». Ручные правки владельца в прочих строках и полях
    берутся из самой таблицы и переживают. Нет расхождений — таблица НЕ сохраняется. Только в редакторе (нужен `unreal`).
    Возврат: число провалов (0 — успех)."""
    import unreal
    dt = unreal.EditorAssetLibrary.load_asset(DT_BUTTON_PATH)
    if not dt:
        log("!! подписи добавок: нет таблицы %s" % DT_BUTTON_PATH)
        return 1
    before = {r["Name"]: r for r in json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(dt))}
    rows = {k: dict(v) for k, v in before.items()}   # рабочая копия строк живой таблицы
    want = {}                                          # ключ строки → подпись из GameText.csv
    for ex in EXTRA_BUTTONS:
        if "CaptionLoc" not in ex:
            continue
        text = _gametext(ex["CaptionLoc"])
        keys = [k for k, r in before.items() if r.get("NsrName") == ex["NsrName"]
                and any(sb.get("CmdCode") == ex["CmdCode"] for sb in r.get("Subs", []))]
        if len(keys) != 1:
            log("!! подписи добавок: %s код %d — строк в DT %d (ждём 1): %s" % (ex["NsrName"], ex["CmdCode"], len(keys), keys))
            return 1
        k = keys[0]
        log("подпись добавки %s (loc %d): в DT «%s», в GameText «%s» — %s"
            % (k, ex["CaptionLoc"], before[k].get("Caption"), text, "совпадает" if before[k].get("Caption") == text else "РАЗОШЛАСЬ"))
        if before[k].get("Caption") != text:
            want[k] = text
    if not want:
        log("ЗАВЕРШЕНО: подписи добавок — снимок в DT актуален, запись не нужна (изменено 0, провалов 0)")
        return 0
    if dry_run:
        log("ЗАВЕРШЕНО: подписи добавок — пробный прогон, к перезаливке %d: %s (провалов 0)" % (len(want), sorted(want)))
        return 0
    for k, text in want.items():
        rows[k]["Caption"] = text
    pkg_file = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_content_dir() + DT_BUTTON_PATH[len("/Game/"):] + ".uasset")
    mtime_was = os.path.getmtime(pkg_file) if os.path.isfile(pkg_file) else 0.0
    # ★ ИСПРАВЛЕНИЕ дедуп №2 В2 Т4: заливка → сохранение через общий шаг — провал заливки = НЕ сохранять (раньше
    #   сохранялось при любом исходе); маркер ASDA_WRITES_ASSET для проверки даты файла обёрткой.
    from asda_dt_common import fill_and_save as _fill_and_save
    problems = _fill_and_save(unreal, dt, json.dumps(list(rows.values()), ensure_ascii=False), log, fmt="json")   # True — залито и записано
    saved = problems
    after = {r["Name"]: r for r in json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(
        unreal.EditorAssetLibrary.load_asset(DT_BUTTON_PATH)))}
    changed = sorted(k for k in set(before) | set(after) if before.get(k) != after.get(k))
    # ★ Сохранение молча не проходит при соседнем `-game` (закон asset-save-blocked-by-running-game) — сверяем дату файла.
    mtime_now = os.path.getmtime(pkg_file) if os.path.isfile(pkg_file) else 0.0
    ok = changed == sorted(want) and all(after[k].get("Caption") == want[k] for k in want) and mtime_now > mtime_was
    log("подписи добавок: заливка прошла %s; сохранено %s; дата файла %s; изменились ключи %s"
        % (problems, saved, "обновилась" if mtime_now > mtime_was else "!! НЕ обновилась", changed))
    log("ЗАВЕРШЕНО: подписи добавок — изменено %d, провалов %d" % (len(changed), 0 if ok else 1))
    return 0 if ok else 1


def main(csv_only=False):
    os.makedirs(OUT_DIR, exist_ok=True)
    files, headers, buttons, pages, ru_stats = read_all()
    apply_extra_buttons(headers, buttons)

    j_h = json_rows(headers, KEY_HEADER)
    j_b = json_rows(buttons, KEY_BUTTON)
    j_p = json_rows(pages, KEY_PAGE)
    for fn, txt in (("NpcNsrHeader.json", j_h), ("NpcNsrButton.json", j_b), ("NpcNsrPage.json", j_p)):
        with open(os.path.join(OUT_DIR, fn), "w", encoding="utf-8", newline="") as f:
            f.write(txt)

    codes = {}
    for r in buttons:
        for s in r["Subs"]:
            if s["State"]:
                codes[s["CmdCode"]] = codes.get(s["CmdCode"], 0) + 1
    log("файлов .NSR %d → шапок %d, кнопок %d, страниц %d" % (len(files), len(headers), len(buttons), len(pages)))
    for tag, human in (("title", "заголовки сценариев"), ("btn", "подписи кнопок меню"),
                       ("page", "тексты страниц"), ("step", "подписи шагов")):
        got, tot = ru_stats[tag]
        log("РУС: %-22s переведено %d из %d (без пары %d)" % (human, got, tot, tot - got))
    log("различных кодов команд: %d → %s" % (len(codes), sorted(codes)))

    if verify(headers, buttons, pages) == 0:
        log("СВЕРКА: расхождений 0")

    if csv_only:
        return
    try:
        import unreal  # noqa: F401
    except Exception:
        log("UE недоступен — записаны только JSON в %s" % OUT_DIR)
        return
    # ★ 29.10.2026: точечный переимпорт — ASDA_NSR_ONLY=DT_NpcNsrPage[,…]; пусто — все три таблицы.
    only = {x.strip() for x in _os.environ.get("ASDA_NSR_ONLY", "").split(",") if x.strip()}
    for name, rs, txt in (("DT_NpcNsrHeader", RS_HEADER, j_h), ("DT_NpcNsrButton", RS_BUTTON, j_b), ("DT_NpcNsrPage", RS_PAGE, j_p)):
        if not only or name in only:
            fill_dt(name, rs, txt)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main(csv_only=("--csv-only" in _sys.argv))
